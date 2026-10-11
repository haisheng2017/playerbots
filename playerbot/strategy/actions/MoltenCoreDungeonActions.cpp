
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

using namespace ai;

namespace
{
    constexpr uint32 MC_AQUAL_QUINTESSENCE = 17333;    // Aqual Quintessence: consumed on use, no cooldown
    constexpr uint32 MC_AQUAL_QUINTESSENCE_TARGET = 1; // Keep 1 in bags: douse once, refill one

    // Baron Geddon
    constexpr uint32 GEDDON_ENTRY = 12056;
    constexpr float  GEDDON_LIVING_BOMB_SAFE_DISTANCE = 15.0f; // Explosion-safe distance from other players and the boss

    // Majordomo Executus / Sulfuron: the adds to kill instead of the boss while they live
    constexpr float BOSS_ADD_SEARCH_RANGE = 60.0f;

    // Nearest alive creature out of the given entries, the encounter's real targets
    Creature* FindNearestCreatureOfEntries(Player* bot, const std::vector<uint32>& entries)
    {
        Creature* nearest = nullptr;
        float nearestDistance = BOSS_ADD_SEARCH_RANGE;

        for (uint32 entry : entries)
        {
            std::list<Unit*> units;
            MaNGOS::AllCreaturesOfEntryInRangeCheck u_check(bot, entry, BOSS_ADD_SEARCH_RANGE);
            MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, u_check);
            Cell::VisitAllObjects(bot, searcher, BOSS_ADD_SEARCH_RANGE);

            for (Unit* unit : units)
            {
                Creature* creature = (Creature*)unit;
                if (!creature || !creature->IsAlive())
                    continue;

                float const distance = bot->GetDistance(creature);
                if (distance <= nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = creature;
                }
            }
        }

        return nearest;
    }

    constexpr uint32 MAJORDOMO_ENTRY = 12018;
    constexpr uint32 MAJORDOMO_HEALER = 11663;
    constexpr uint32 MAJORDOMO_ELITE = 11664;
    constexpr uint32 MAJORDOMO_MAGIC_REFLECTION = 20619;     // 10s, 50% spell reflect, on every add
    constexpr uint32 MAJORDOMO_DAMAGE_SHIELD = 21075;        // 10s, melee hits on an add splash back
    constexpr uint32 MAJORDOMO_POLYMORPH_IMMUNITY = 21087;   // Healers, once half the adds are dead
    constexpr uint32 POLYMORPH_HEAL_AURA = 12939;            // Shared by every polymorph rank
    constexpr float MAJORDOMO_ADD_RANGE = 60.0f;

    void ListMajordomoAdds(Player* bot, uint32 entry, std::list<Creature*>& out)
    {
        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck u_check(bot, entry, MAJORDOMO_ADD_RANGE);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, u_check);
        Cell::VisitAllObjects(bot, searcher, MAJORDOMO_ADD_RANGE);

        for (Unit* unit : units)
        {
            Creature* creature = (Creature*)unit;
            if (creature && creature->IsAlive())
                out.push_back(creature);
        }
    }

    bool BotOwnsSheep(PlayerbotAI* ai, Unit* unit)
    {
        if (ai->HasAura(POLYMORPH_HEAL_AURA, unit, true))
            return true;

        return ai->HasAura("polymorph", unit, false, true)
            || ai->HasAura("polymorph: pig", unit, false, true)
            || ai->HasAura("polymorph: turtle", unit, false, true);
    }

    // Same answer for every bot: skull, else the elite standing closest to Majordomo, else a healer
    Creature* SelectKillTarget(PlayerbotAI* ai, Player* bot)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        if (Unit* rti = ai->GetUnit(AI_VALUE(ObjectGuid, "rti target")))
        {
            if (rti->IsAlive() && !rti->IsPolymorphed() && rti->GetTypeId() == TYPEID_UNIT
                && (rti->GetEntry() == MAJORDOMO_HEALER || rti->GetEntry() == MAJORDOMO_ELITE))
                return (Creature*)rti;
        }

        Creature* boss = FindNearestCreatureOfEntries(bot, {MAJORDOMO_ENTRY});

        Creature* best = nullptr;
        float bestDistance = MAJORDOMO_ADD_RANGE;
        std::list<Creature*> elites;
        ListMajordomoAdds(bot, MAJORDOMO_ELITE, elites);
        for (Creature* add : elites)
        {
            if (add->IsPolymorphed())
                continue;

            float const distance = boss ? boss->GetDistance(add) : bot->GetDistance(add);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = add;
            }
        }

        if (best)
            return best;

        // No elite left to kill. Healers are the adds, still skipping anything sheeped.
        bestDistance = MAJORDOMO_ADD_RANGE;
        std::list<Creature*> healers;
        ListMajordomoAdds(bot, MAJORDOMO_HEALER, healers);
        for (Creature* add : healers)
        {
            if (add->IsPolymorphed())
                continue;

            float const distance = boss ? boss->GetDistance(add) : bot->GetDistance(add);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = add;
            }
        }

        return best;
    }

    bool MageOwnsSheep(PlayerbotAI* ai, Player* bot)
    {
        std::list<Creature*> healers;
        ListMajordomoAdds(bot, MAJORDOMO_HEALER, healers);
        for (Creature* healer : healers)
        {
            if (BotOwnsSheep(ai, healer))
                return true;
        }

        return false;
    }

    Creature* SelectSheepTarget(PlayerbotAI* ai, Player* bot)
    {
        if (bot->getClass() != CLASS_MAGE || MageOwnsSheep(ai, bot))
            return nullptr;

        Creature* kill = SelectKillTarget(ai, bot);
        std::list<Creature*> healers;
        ListMajordomoAdds(bot, MAJORDOMO_HEALER, healers);

        // Spread mages across different healers. A mage can only keep one polymorph.
        Creature* best = nullptr;
        uint32 bestScore = 0;
        for (Creature* healer : healers)
        {
            if (healer->IsPolymorphed() || healer->HasAura(MAJORDOMO_POLYMORPH_IMMUNITY))
                continue;

            if (kill && healer->GetObjectGuid() == kill->GetObjectGuid())
                continue;

            uint32 const score = healer->GetGUIDLow() ^ bot->GetGUIDLow();
            if (!best || score < bestScore)
            {
                best = healer;
                bestScore = score;
            }
        }

        return best;
    }

    Creature* SelectEliteToTank(PlayerbotAI* ai, Player* bot)
    {
        std::list<Creature*> elites;
        ListMajordomoAdds(bot, MAJORDOMO_ELITE, elites);

        Creature* loose = nullptr;
        float looseDistance = MAJORDOMO_ADD_RANGE;
        Creature* leastThreat = nullptr;
        float minThreat = 0.0f;

        for (Creature* elite : elites)
        {
            if (elite->IsPolymorphed())
                continue;

            // An elite chewing on a clothie gets a tank before one that is already held
            Unit* victim = elite->GetVictim();
            if (victim && victim->GetTypeId() == TYPEID_PLAYER && !ai->IsTank((Player*)victim))
            {
                float const distance = bot->GetDistance(elite);
                if (distance <= looseDistance)
                {
                    looseDistance = distance;
                    loose = elite;
                }
            }

            float const threat = sServerFacade.GetThreatManager(elite).getThreat(bot);
            if (!leastThreat || threat < minThreat)
            {
                leastThreat = elite;
                minThreat = threat;
            }
        }

        return loose ? loose : leastThreat;
    }

    bool ShouldHoldForShield(PlayerbotAI* ai, Player* bot)
    {
        // Heals are not cast at the adds. Interrupting a healer here would cancel a heal.
        if (ai->IsHeal(bot))
            return false;

        AiObjectContext* context = ai->GetAiObjectContext();
        Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
        if (!target || !target->IsAlive())
            return false;

        if (target->GetEntry() != MAJORDOMO_HEALER && target->GetEntry() != MAJORDOMO_ELITE)
            return false;

        // Reflection bounces spells, including melee specials. White swings are not spells.
        if (target->HasAura(MAJORDOMO_MAGIC_REFLECTION))
            return true;

        // Damage Shield punishes melee swings and a hunter pet's bites. Casters keep nuking.
        if (target->HasAura(MAJORDOMO_DAMAGE_SHIELD) && (ai->IsMelee(bot) || bot->GetPet()))
            return true;

        return false;
    }
}

bool RefreshMCQuintessenceAction::isPossible()
{
    return bot->IsInWorld() && !bot->IsBeingTeleported()
        && bot->GetMapId() == 409
        && bot->GetItemCount(MC_AQUAL_QUINTESSENCE, false) < MC_AQUAL_QUINTESSENCE_TARGET;
}

bool RefreshMCQuintessenceAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    if (bot->GetItemCount(MC_AQUAL_QUINTESSENCE, false) >= MC_AQUAL_QUINTESSENCE_TARGET)
        return false;

    // Refill 1: if the bags are full the action fails and the trigger retries next check (self-healing)
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, MC_AQUAL_QUINTESSENCE, 1) != EQUIP_ERR_OK)
    {
        if (requester)
        {
            ai->TellPlayerNoFacing(requester, "My bags are full, cannot carry Aqual Quintessence for dousing.",
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, true); // noRepeat prevents spam
        }
        return false;
    }

    Item* item = bot->StoreNewItem(dest, MC_AQUAL_QUINTESSENCE, true);
    if (!item)
        return false;

    bot->SendNewItem(item, 1, true, false);
    return true;
}

std::string DouseMCRuneActionBase::GetTargetRuneName()
{
    // Parse the qualifier the same way UseItemIdAction::Execute does, to get the target rune name
    std::vector<std::string> params = getMultiQualifiers(getQualifier(), ",");
    if (params.size() < 2)
        return "";

    std::list<GuidPosition> guidPs = AI_VALUE(std::list<GuidPosition>, params[1]);
    if (guidPs.empty())
        return "";

    GuidPosition guidP = *guidPs.begin();
    if (!guidP.IsGameObject())
        return "";

    GameObject* go = guidP.GetGameObject(bot->GetInstanceId());
    if (!go || !go->GetGOInfo())
        return "";

    return go->GetGOInfo()->name;
}

bool DouseMCRuneActionBase::Execute(Event& event)
{
    // The rune GO leaves the usable list once doused, so record the target rune name first
    std::string runeName = GetTargetRuneName();

    bool result = UseItemIdAction::Execute(event);

    if (result)
    {
        if (runeName.empty())
            ai->Say("I have doused the fire.");
        else
            ai->Say("I have doused the fire [" + runeName + "]");
    }

    return result;
}

bool BaronGeddonLivingBombEscapeAction::isPossible()
{
    if (MovementAction::isPossible())
    {
        return ai->CanMove();
    }

    return false;
}

bool BaronGeddonLivingBombEscapeAction::Execute(Event& event)
{
    const WorldPosition initialPosition(bot);

    // Everything the bomb must not go off next to: nearby alive group members and the boss himself
    std::list<WorldPosition> awayFrom;

    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->getSource();
            if (!member || member == bot || member->IsBeingTeleported() || !sServerFacade.IsAlive(member) ||
                member->GetMapId() != bot->GetMapId())
                continue;

            if (bot->IsWithinDist(member, 2.0f * GEDDON_LIVING_BOMB_SAFE_DISTANCE))
                awayFrom.push_back(WorldPosition(member));
        }
    }

    if (bot->IsInWorld() && !bot->IsBeingTeleported())
    {
        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck u_check(bot, GEDDON_ENTRY, 2.0f * GEDDON_LIVING_BOMB_SAFE_DISTANCE);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, u_check);
        Cell::VisitAllObjects(bot, searcher, 2.0f * GEDDON_LIVING_BOMB_SAFE_DISTANCE);
        for (Unit* unit : units)
        {
            Creature* creature = (Creature*)unit;
            if (creature && creature->IsAlive())
                awayFrom.push_back(WorldPosition(creature));
        }
    }

    // Sweep points around the bot and keep the one that ends up furthest from everyone collected above
    const uint8 attempts = 16;
    const float angleStart = frand(0.0f, 2.0f * M_PI_F);
    const float angleIncrement = (float)((2.0f * M_PI) / attempts);

    WorldPosition bestPoint;
    float bestScore = -1.0f;

    for (uint8 i = 0; i < attempts; i++)
    {
        const float angle = angleStart + angleIncrement * i;
        WorldPosition point = initialPosition + WorldPosition(0,
            GEDDON_LIVING_BOMB_SAFE_DISTANCE * cos(angle),
            GEDDON_LIVING_BOMB_SAFE_DISTANCE * sin(angle), 0);
        point.setZ(point.getHeight());

        if (!bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight())
            || !initialPosition.canPathTo(point, bot))
            continue;

        // Score a point by its distance to the closest thing we must stay away from
        float score = 9999.0f;
        for (const WorldPosition& position : awayFrom)
        {
            float const distance = position.distance(point);
            if (distance < score)
                score = distance;
        }

        if (awayFrom.empty())
            score = GEDDON_LIVING_BOMB_SAFE_DISTANCE;

        if (score > bestScore)
        {
            bestScore = score;
            bestPoint = point;
        }
    }

    if (bestScore < 0.0f)
        return false;

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_COMBAT))
        bot->SummonCreature(15631, bestPoint.getX(), bestPoint.getY(), bestPoint.getZ(), 0.0f, TEMPSPAWN_TIMED_DESPAWN, 5000.0f);

    return MoveTo(bot->GetMapId(), bestPoint.getX(), bestPoint.getY(), bestPoint.getZ(), false, IsReaction(), false, true);
}

Creature* AttackBossAddAction::FindAdd()
{
    // Respect the raid leader's kill order: a skull on one of this fight's add focuses everyone on it
    if (Unit* rti = ai->GetUnit(AI_VALUE(ObjectGuid, "rti target")))
    {
        if (rti->IsAlive() && rti->GetTypeId() == TYPEID_UNIT)
        {
            for (uint32 entry : addEntries)
            {
                if (rti->GetEntry() == entry)
                    return (Creature*)rti;
            }
        }
    }

    return FindNearestCreatureOfEntries(bot, addEntries);
}

bool AttackBossAddAction::isUseful()
{
    return FindAdd() && ai->GetMainTank() != bot && !ai->IsHeal(bot);
}

bool AttackBossAddAction::Execute(Event& event)
{
    Unit* add = FindAdd();
    if (!add)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, add);
}

namespace ai
{
    Creature* FindMajordomoKillTarget(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld())
            return nullptr;

        return SelectKillTarget(ai, bot);
    }

    Creature* FindMajordomoSheepTarget(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld())
            return nullptr;

        return SelectSheepTarget(ai, bot);
    }

    Creature* FindMajordomoEliteToTank(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld())
            return nullptr;

        return SelectEliteToTank(ai, bot);
    }

    bool MajordomoMageOwnsSheep(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld())
            return false;

        return MageOwnsSheep(ai, bot);
    }

    bool MajordomoShouldHoldForShield(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld())
            return false;

        return ShouldHoldForShield(ai, bot);
    }
}

bool MajordomoAttackAddAction::isUseful()
{
    if (!bot->IsInWorld() || ai->GetMainTank() == bot || ai->IsHeal(bot) || ai->IsTank(bot))
        return false;

    if (FindMajordomoKillTarget(ai))
        return true;

    // Drop a sheep the bot is still swinging at once every other add is already controlled
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return target && target->IsPolymorphed();
}

bool MajordomoAttackAddAction::Execute(Event& event)
{
    Creature* kill = FindMajordomoKillTarget(ai);
    if (!kill)
    {
        Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
        if (target && target->IsPolymorphed())
        {
            bot->AttackStop();
            if (Pet* pet = bot->GetPet())
                pet->AttackStop();

            SET_AI_VALUE(ObjectGuid, "current target", ObjectGuid());
            bot->SetSelectionGuid(ObjectGuid());
        }

        return false;
    }

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, kill);
}

bool AttackMajordomoEliteAction::isUseful()
{
    return ai->IsTank(bot) && ai->GetMainTank() != bot && FindMajordomoEliteToTank(ai);
}

bool AttackMajordomoEliteAction::Execute(Event& event)
{
    Creature* elite = FindMajordomoEliteToTank(ai);
    if (!elite)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, elite);
}

bool MajordomoPolymorphHealerAction::isUseful()
{
    if (bot->getClass() != CLASS_MAGE)
        return false;

    Creature* healer = FindMajordomoSheepTarget(ai);
    return healer && ai->CanCastSpell("polymorph", healer, 0);
}

bool MajordomoPolymorphHealerAction::Execute(Event& /*event*/)
{
    Creature* healer = FindMajordomoSheepTarget(ai);
    if (!healer)
        return false;

    return ai->CastSpell("polymorph", healer);
}

bool MajordomoShieldHoldAction::isUseful()
{
    return MajordomoShouldHoldForShield(ai);
}

bool MajordomoShieldHoldAction::Execute(Event& /*event*/)
{
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    if (target && target->HasAura(20619))
    {
        bot->InterruptSpell(CURRENT_GENERIC_SPELL);
        bot->InterruptSpell(CURRENT_CHANNELED_SPELL);
        bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        bot->InterruptSpell(CURRENT_MELEE_SPELL);
    }

    // White swings are what Damage Shield punishes. Reflection leaves those alone.
    if (target && target->HasAura(21075) && ai->IsMelee(bot))
        bot->AttackStop();

    if (Pet* pet = bot->GetPet())
        pet->AttackStop();

    return false;
}

bool AttackBossEntryAction::isUseful()
{
    return ai->GetMainTank() == bot;
}

bool AttackBossEntryAction::Execute(Event& event)
{
    Creature* boss = FindNearestCreatureOfEntries(bot, {bossEntry});
    if (!boss)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, boss);
}

bool AttackGarrAction::isUseful()
{
    // The tanks hold the boss and the Firesworn, the healers do not attack:
    // this pin belongs to the raid's damage dealers only
    return FindNearestCreatureOfEntries(bot, {bossEntry}) && !ai->IsTank(bot) && !ai->IsHeal(bot);
}

Creature* AttackGarrFireswornAction::FindFiresworn()
{
    // The raid's attackable list already filters the banished Firesworn out
    std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");

    Creature* leastThreat = nullptr;
    float minThreat = 0.0f;

    for (const ObjectGuid& guid : possibleTargets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT || unit->GetEntry() != FIRESWORN_ENTRY)
            continue;

        // A Firesworn loose on a non-tank is about to kill someone: take that one first
        Unit* victim = unit->GetVictim();
        if (victim && victim->GetTypeId() == TYPEID_PLAYER && !ai->IsTank((Player*)victim))
            return (Creature*)unit;

        // Otherwise spread out over the adds: the one this off-tank has touched the least
        float threat = sServerFacade.GetThreatManager(unit).getThreat(bot);
        if (!leastThreat || threat < minThreat)
        {
            leastThreat = (Creature*)unit;
            minThreat = threat;
        }
    }

    return leastThreat;
}

bool AttackGarrFireswornAction::isUseful()
{
    return FindFiresworn() && ai->IsTank(bot) && ai->GetMainTank() != bot;
}

bool AttackGarrFireswornAction::Execute(Event& event)
{
    Unit* add = FindFiresworn();
    if (!add)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, add);
}
