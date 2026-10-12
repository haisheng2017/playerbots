
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"
#include "Maps/GridMap.h"
#include <algorithm>
#include <cmath>
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/GuidPositionValues.h"
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
    constexpr float MAJORDOMO_MT_MELEE = 12.0f;              // Main tank is "on" the boss inside this range
    constexpr float MAJORDOMO_PLATFORM_X = 758.0892f;
    constexpr float MAJORDOMO_PLATFORM_Y = -1176.712f;
    constexpr float MAJORDOMO_PLATFORM_Z = -118.6403f;
    constexpr float MAJORDOMO_LAVA_ROOM = 100.0f;

    bool PositionInMagma(const WorldPosition& point)
    {
        const TerrainInfo* terrain = point.getTerrain();
        if (!terrain || !terrain->CanCheckLiquidLevel(point.getX(), point.getY()))
            return false;

        GridMapLiquidData data;
        uint32 const status = terrain->getLiquidStatus(point.getX(), point.getY(), point.getZ(), MAP_ALL_LIQUIDS, &data);
        if (!(status & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)))
            return false;

        return data.type_flags == MAP_LIQUID_TYPE_MAGMA || data.type_flags == MAP_LIQUID_TYPE_SLIME;
    }

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

    bool MajordomoElitesRemain(Player* bot)
    {
        std::list<Creature*> elites;
        ListMajordomoAdds(bot, MAJORDOMO_ELITE, elites);
        for (Creature* elite : elites)
        {
            if (elite->IsAlive() && !elite->IsPolymorphed())
                return true;
        }

        return false;
    }

    std::vector<Player*> ListOffTanks(PlayerbotAI* ai, Player* bot)
    {
        std::vector<Player*> tanks;
        auto consider = [&](Player* member)
        {
            if (!member || !member->IsInWorld() || member->IsBeingTeleported() || !member->IsAlive())
                return;
            if (member->GetMapId() != bot->GetMapId())
                return;
            if (!ai->IsTank(member) || ai->GetMainTank() == member)
                return;

            tanks.push_back(member);
        };

        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
                consider(gref->getSource());
        }
        else
            consider(bot);

        std::sort(tanks.begin(), tanks.end(), [](Player* a, Player* b) { return a->GetGUIDLow() < b->GetGUIDLow(); });
        tanks.erase(std::unique(tanks.begin(), tanks.end()), tanks.end());
        return tanks;
    }

    // One off-tank per add, including the skull. Adds hitting a non-tank are handed out first.
    Creature* SelectSpreadAdd(PlayerbotAI* ai, Player* bot, uint32 entry)
    {
        std::list<Creature*> adds;
        ListMajordomoAdds(bot, entry, adds);

        std::vector<Creature*> loose;
        std::vector<Creature*> rest;
        for (Creature* add : adds)
        {
            if (add->IsPolymorphed())
                continue;

            Unit* victim = add->GetVictim();
            if (victim && victim->GetTypeId() == TYPEID_PLAYER && !ai->IsTank((Player*)victim))
                loose.push_back(add);
            else
                rest.push_back(add);
        }

        auto byGuid = [](Creature* a, Creature* b) { return a->GetGUIDLow() < b->GetGUIDLow(); };
        std::sort(loose.begin(), loose.end(), byGuid);
        std::sort(rest.begin(), rest.end(), byGuid);

        std::vector<Creature*> pool = loose;
        pool.insert(pool.end(), rest.begin(), rest.end());
        if (pool.empty())
            return nullptr;

        for (Creature* add : pool)
        {
            if (add->GetVictim() == bot)
                return add;
        }

        // Leave an add another off-tank is already holding. Free tanks split what is still on a non-tank.
        std::vector<Creature*> open;
        for (Creature* add : pool)
        {
            Unit* victim = add->GetVictim();
            if (victim && victim->GetTypeId() == TYPEID_PLAYER && ai->IsTank((Player*)victim))
                continue;
            open.push_back(add);
        }
        if (open.empty())
            return nullptr;

        std::vector<Player*> freeTanks;
        for (Player* tank : ListOffTanks(ai, bot))
        {
            bool holding = false;
            for (Creature* add : pool)
            {
                if (add->GetVictim() == tank)
                    holding = true;
            }
            if (!holding)
                freeTanks.push_back(tank);
        }
        if (freeTanks.empty())
            return open.front();

        uint32 index = 0;
        for (; index < freeTanks.size(); ++index)
        {
            if (freeTanks[index] == bot)
                break;
        }
        if (index >= freeTanks.size())
            return nullptr;

        return open[index % open.size()];
    }

    bool BossNeedsCover(PlayerbotAI* ai, Player* bot, Creature* boss)
    {
        Player* mainTank = ai->GetMainTank();
        if (!mainTank || !mainTank->IsAlive() || mainTank->GetMapId() != bot->GetMapId())
            return false;

        // Teleported off the platform, or still running back.
        if (mainTank->GetDistance(boss) > MAJORDOMO_MT_MELEE)
            return true;

        // Threat wipe: somebody else has him even though the main tank is in melee.
        return boss->GetVictim() != mainTank;
    }

    Player* ChooseCoverTank(PlayerbotAI* ai, Player* bot, Creature* boss)
    {
        if (Unit* victim = boss->GetVictim())
        {
            if (victim->GetTypeId() == TYPEID_PLAYER)
            {
                Player* player = (Player*)victim;
                if (player->IsAlive() && ai->IsTank(player) && ai->GetMainTank() != player)
                    return player;
            }
        }

        Player* closest = nullptr;
        float closestDistance = 0.0f;
        for (Player* tank : ListOffTanks(ai, bot))
        {
            float const distance = tank->GetDistance(boss);
            if (!closest || distance < closestDistance)
            {
                closest = tank;
                closestDistance = distance;
            }
        }

        return closest;
    }

    Creature* SelectEliteToTank(PlayerbotAI* ai, Player* bot)
    {
        // Healers, including the kill target, once the elites are dead. Until then, the elites.
        uint32 const entry = MajordomoElitesRemain(bot) ? MAJORDOMO_ELITE : MAJORDOMO_HEALER;
        return SelectSpreadAdd(ai, bot, entry);
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

namespace
{
    constexpr uint32 MC_AQUAL_QUINTESSENCE_ITEM = 17333;
    constexpr uint32 MC_ETERNAL_QUINTESSENCE_ITEM = 22754;

    void ClearPendingMCDouse(PlayerbotAI* ai)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        SET_AI_VALUE2(int32, "manual int", "mc douse pending rune", 0);
        SET_AI_VALUE2(int32, "manual int", "mc douse pending item", 0);
        SET_AI_VALUE2(int32, "manual int", "mc douse pending count", 0);
        SET_AI_VALUE2(std::string, "manual string", "mc douse pending name", "");
    }
}

GameObject* DouseMCRuneActionBase::GetTargetRune()
{
    // Parse the qualifier the same way UseItemIdAction::Execute does
    std::vector<std::string> params = getMultiQualifiers(getQualifier(), ",");
    if (params.size() < 2)
        return nullptr;

    std::list<GuidPosition> guidPs = AI_VALUE(std::list<GuidPosition>, params[1]);
    if (guidPs.empty())
        return nullptr;

    GuidPosition guidP = *guidPs.begin();
    if (!guidP.IsGameObject())
        return nullptr;

    return guidP.GetGameObject(bot->GetInstanceId());
}

bool DouseMCRuneActionBase::isUseful()
{
    if (!UseItemIdAction::isUseful())
        return false;

    GameObject* go = GetTargetRune();
    if (!go)
        return false;

    return MoltenCoreRuneEncounterState(bot, go->GetEntry()) == MC_RUNE_BOSS_DEAD;
}

bool DouseMCRuneActionBase::Execute(Event& event)
{
    GameObject* go = GetTargetRune();
    if (!go || !go->GetGOInfo())
        return false;

    // The rune script only counts a use after the boss is dead. Using it earlier still
    // flags the GO in-use, and the cast itself can be cancelled after it has started.
    if (MoltenCoreRuneEncounterState(bot, go->GetEntry()) != MC_RUNE_BOSS_DEAD)
        return false;

    uint32 itemId = GetItemId();
    uint32 countBefore = bot->GetItemCount(itemId, false);
    std::string runeName = go->GetGOInfo()->name;
    uint32 runeEntry = go->GetEntry();

    if (!UseItemIdAction::Execute(event))
        return false;

    // The line is said later, once this cast has actually counted.
    SET_AI_VALUE2(int32, "manual int", "mc douse pending rune", int32(runeEntry));
    SET_AI_VALUE2(int32, "manual int", "mc douse pending item", int32(itemId));
    SET_AI_VALUE2(int32, "manual int", "mc douse pending count", int32(countBefore));
    SET_AI_VALUE2(std::string, "manual string", "mc douse pending name", runeName);
    return true;
}

bool SayMCRuneDousedAction::Execute(Event& /*event*/)
{
    int32 runeEntry = AI_VALUE2(int32, "manual int", "mc douse pending rune");
    int32 itemId = AI_VALUE2(int32, "manual int", "mc douse pending item");
    int32 countBefore = AI_VALUE2(int32, "manual int", "mc douse pending count");
    std::string runeName = AI_VALUE2(std::string, "manual string", "mc douse pending name");
    ClearPendingMCDouse(ai);

    if (runeEntry <= 0 || MoltenCoreRuneEncounterState(bot, uint32(runeEntry)) != MC_RUNE_DOUSED)
        return false;

    // Aqual is consumed when the spell lands. Eternal is not, but it takes the item cooldown.
    // A cast that was interrupted, or that lost the race to another bot, does neither.
    bool landed = false;
    if (uint32(itemId) == MC_AQUAL_QUINTESSENCE_ITEM)
        landed = int32(bot->GetItemCount(MC_AQUAL_QUINTESSENCE_ITEM, false)) < countBefore;
    else if (uint32(itemId) == MC_ETERNAL_QUINTESSENCE_ITEM)
        landed = !sServerFacade.IsSpellReady(bot, MC_DOUSE_SPELL, MC_ETERNAL_QUINTESSENCE_ITEM);

    if (!landed)
        return false;

    if (runeName.empty())
        ai->Say("I have doused the fire.");
    else
        ai->Say("I have doused the fire [" + runeName + "]");

    return true;
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

    bool MajordomoBotShouldCoverBoss(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld() || !sServerFacade.IsAlive(bot))
            return false;
        if (!ai->IsTank(bot) || ai->GetMainTank() == bot)
            return false;

        Creature* boss = FindNearestCreatureOfEntries(bot, {MAJORDOMO_ENTRY});
        if (!boss || !boss->IsAlive())
            return false;
        if (!BossNeedsCover(ai, bot, boss))
            return false;

        return ChooseCoverTank(ai, bot, boss) == bot;
    }

    bool MajordomoMainTankShouldTaunt(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld() || ai->GetMainTank() != bot)
            return false;

        Creature* boss = FindNearestCreatureOfEntries(bot, {MAJORDOMO_ENTRY});
        if (!boss || !boss->IsAlive())
            return false;

        // Anyone else, tank or not.
        return boss->GetVictim() != bot;
    }

    bool MajordomoBotInLava(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMapId() != 409)
            return false;

        float const dx = bot->GetPositionX() - MAJORDOMO_PLATFORM_X;
        float const dy = bot->GetPositionY() - MAJORDOMO_PLATFORM_Y;
        if (dx * dx + dy * dy > MAJORDOMO_LAVA_ROOM * MAJORDOMO_LAVA_ROOM)
            return false;

        return PositionInMagma(WorldPosition(bot));
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
    if (MajordomoBotShouldCoverBoss(ai))
        return false;

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

bool MajordomoMainTankTauntAction::isUseful()
{
    if (!MajordomoMainTankShouldTaunt(ai))
        return false;

    Creature* boss = FindNearestCreatureOfEntries(bot, {12018});
    if (!boss)
        return false;

    const char* spells[] = { "taunt", "mocking blow", "growl", "hand of reckoning", "righteous defense" };
    for (const char* spell : spells)
    {
        if (ai->CanCastSpell(spell, boss, 0))
            return true;
    }

    return false;
}

bool MajordomoMainTankTauntAction::Execute(Event& /*event*/)
{
    Creature* boss = FindNearestCreatureOfEntries(bot, {12018});
    if (!boss)
        return false;

    SET_AI_VALUE(ObjectGuid, "current target", boss->GetObjectGuid());

    const char* spells[] = { "taunt", "mocking blow", "growl", "hand of reckoning", "righteous defense" };
    for (const char* spell : spells)
    {
        if (ai->CanCastSpell(spell, boss, 0) && ai->CastSpell(spell, boss))
            return true;
    }

    return false;
}

bool AttackMajordomoCoverAction::isUseful()
{
    return MajordomoBotShouldCoverBoss(ai);
}

bool AttackMajordomoCoverAction::Execute(Event& event)
{
    Creature* boss = FindNearestCreatureOfEntries(bot, {12018});
    if (!boss)
        return false;

    // Pull him off a non-tank, or off the main tank while that tank is still out of melee.
    // Once the main tank is back in range, his own taunt takes the boss and this stops.
    Unit* victim = boss->GetVictim();
    bool taunt = victim != bot;
    if (victim && victim->GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = (Player*)victim;
        if (ai->IsTank(player) && ai->GetMainTank() != player)
            taunt = false;
        if (player == ai->GetMainTank() && player->GetDistance(boss) <= MAJORDOMO_MT_MELEE)
            taunt = false;
    }
    if (taunt)
    {
        const char* spells[] = { "taunt", "mocking blow", "growl", "hand of reckoning", "righteous defense" };
        for (const char* spell : spells)
        {
            if (ai->CanCastSpell(spell, boss, 0) && ai->CastSpell(spell, boss))
                break;
        }
    }

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, boss);
}

bool MajordomoLeaveLavaAction::isPossible()
{
    if (MovementAction::isPossible())
        return ai->CanMove();

    return false;
}

bool MajordomoLeaveLavaAction::Execute(Event& /*event*/)
{
    WorldPosition from(bot);
    WorldPosition goal(bot->GetMapId(), MAJORDOMO_PLATFORM_X, MAJORDOMO_PLATFORM_Y, MAJORDOMO_PLATFORM_Z);
    if (Creature* boss = FindNearestCreatureOfEntries(bot, {12018}))
    {
        WorldPosition bossPosition(boss);
        if (boss->IsAlive() && !PositionInMagma(bossPosition))
            goal = bossPosition;
    }

    float const dx = goal.getX() - from.getX();
    float const dy = goal.getY() - from.getY();
    float dist = sqrt(dx * dx + dy * dy);
    if (dist < 1.0f)
        dist = 1.0f;

    for (float step = 5.0f; step < dist && step <= 40.0f; step += 5.0f)
    {
        WorldPosition point(bot->GetMapId(),
            from.getX() + dx / dist * step,
            from.getY() + dy / dist * step,
            from.getZ());
        point.setZ(point.getHeight());
        if (PositionInMagma(point))
            continue;
        if (!bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()))
            continue;

        return MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ(), false, IsReaction(), true, true);
    }

    return MoveTo(bot->GetMapId(), goal.getX(), goal.getY(), goal.getZ(), false, IsReaction(), false, true);
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

namespace
{
    constexpr uint32 RAGNAROS_ENTRY = 11502;
    constexpr uint32 RAGNAROS_SON_ENTRY = 12143;
    constexpr uint32 RAGNAROS_FLAME_ENTRY = 13148;
    constexpr float RAGNAROS_ROOM_RANGE = 80.0f;
    constexpr float RAGNAROS_FLOOR_ABOVE = 4.0f;
    constexpr float RAGNAROS_RANGED_RING = 35.0f;
    constexpr float RAGNAROS_SLOT_TOLERANCE = 4.0f;
    constexpr float RAGNAROS_FLAME_RANGE = 13.0f;

    Creature* FindRagnaros(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || bot->GetMapId() != 409)
            return nullptr;

        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, RAGNAROS_ENTRY, RAGNAROS_ROOM_RANGE);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(bot, searcher, RAGNAROS_ROOM_RANGE);

        Creature* nearest = nullptr;
        float nearestDistance = RAGNAROS_ROOM_RANGE;
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

        return nearest;
    }

    bool BossSubmerged(Creature* boss)
    {
        return boss && boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
    }

    bool AboveFloor(Player* bot, Creature* boss)
    {
        return bot->GetPositionZ() > boss->GetPositionZ() + RAGNAROS_FLOOR_ABOVE;
    }

    float InnerRing(Creature* boss)
    {
        float radius = boss->GetCombatReach() + 3.0f;
        if (radius < 12.0f)
            radius = 12.0f;
        if (radius > 20.0f)
            radius = 20.0f;
        return radius;
    }

    bool OnInnerRing(PlayerbotAI* ai, Player* bot)
    {
        return ai->IsTank(bot) || (!ai->IsRanged(bot) && !ai->IsHeal(bot));
    }

    float PlaceZ(Creature* boss, float x, float y)
    {
        float const floor = boss->GetPositionZ();
        WorldPosition point(boss->GetMapId(), x, y, floor + 2.0f);
        float const height = point.getHeight();
        if (height > floor + RAGNAROS_FLOOR_ABOVE || height < floor - 5.0f)
            return floor + 1.0f;
        return height;
    }

    void ListRoomMembers(Player* bot, Creature* boss, std::vector<Player*>& out)
    {
        auto consider = [&](Player* member)
        {
            if (!member || !member->IsInWorld() || !member->IsAlive() || member->IsBeingTeleported())
                return;
            if (member->GetMapId() != bot->GetMapId())
                return;
            if (member != bot && member->GetDistance(boss) > RAGNAROS_ROOM_RANGE)
                return;
            out.push_back(member);
        };

        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
                consider(gref->getSource());
        }
        else
            consider(bot);

        std::sort(out.begin(), out.end(), [](Player* a, Player* b) { return a->GetGUIDLow() < b->GetGUIDLow(); });
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    bool ComputeRingPoint(Player* bot, float& x, float& y, float& z)
    {
        Creature* boss = FindRagnaros(bot);
        if (!boss)
            return false;

        PlayerbotAI* ai = bot->GetPlayerbotAI();
        std::vector<Player*> members;
        ListRoomMembers(bot, boss, members);
        if (members.empty())
            members.push_back(bot);

        uint32 index = 0;
        for (; index < members.size(); ++index)
        {
            if (members[index] == bot)
                break;
        }
        if (index >= members.size())
            index = 0;

        float const angle = (index + 0.5f) * 2.0f * M_PI_F / members.size();
        float const radius = (ai && OnInnerRing(ai, bot)) ? InnerRing(boss) : RAGNAROS_RANGED_RING;
        x = boss->GetPositionX() + cos(angle) * radius;
        y = boss->GetPositionY() + sin(angle) * radius;
        z = PlaceZ(boss, x, y);
        return true;
    }

    std::vector<Creature*> ListSons(PlayerbotAI* ai)
    {
        std::vector<Creature*> sons;
        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        for (const ObjectGuid& guid : possibleTargets)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT || unit->GetEntry() != RAGNAROS_SON_ENTRY)
                continue;
            sons.push_back((Creature*)unit);
        }

        std::sort(sons.begin(), sons.end(), [](Creature* a, Creature* b) { return a->GetGUIDLow() < b->GetGUIDLow(); });
        return sons;
    }

    std::vector<Player*> ListSonTanks(PlayerbotAI* ai, Player* bot, Creature* boss)
    {
        std::vector<Player*> tanks;
        bool const submerged = BossSubmerged(boss);
        auto consider = [&](Player* member)
        {
            if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                return;
            if (!ai->IsTank(member))
                return;
            if (!submerged && ai->GetMainTank() == member)
                return;
            tanks.push_back(member);
        };

        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
                consider(gref->getSource());
        }
        else
            consider(bot);

        std::sort(tanks.begin(), tanks.end(), [](Player* a, Player* b) { return a->GetGUIDLow() < b->GetGUIDLow(); });
        tanks.erase(std::unique(tanks.begin(), tanks.end()), tanks.end());
        return tanks;
    }

    Creature* SelectSonToTank(PlayerbotAI* ai, Player* bot)
    {
        Creature* boss = FindRagnaros(bot);
        if (!boss)
            return nullptr;
        if (!BossSubmerged(boss) && ai->GetMainTank() == bot)
            return nullptr;

        std::vector<Creature*> sons = ListSons(ai);
        if (sons.empty())
            return nullptr;

        std::vector<Creature*> loose;
        std::vector<Creature*> rest;
        for (Creature* son : sons)
        {
            Unit* victim = son->GetVictim();
            if (victim && victim->GetTypeId() == TYPEID_PLAYER && !ai->IsTank((Player*)victim))
                loose.push_back(son);
            else
                rest.push_back(son);
        }

        std::vector<Creature*> pool = loose;
        pool.insert(pool.end(), rest.begin(), rest.end());

        for (Creature* son : pool)
        {
            if (son->GetVictim() == bot)
                return son;
        }

        std::vector<Creature*> open;
        for (Creature* son : pool)
        {
            Unit* victim = son->GetVictim();
            if (victim && victim->GetTypeId() == TYPEID_PLAYER && ai->IsTank((Player*)victim))
                continue;
            open.push_back(son);
        }
        if (open.empty())
            return nullptr;

        std::vector<Player*> freeTanks;
        for (Player* tank : ListSonTanks(ai, bot, boss))
        {
            bool holding = false;
            for (Creature* son : pool)
            {
                if (son->GetVictim() == tank)
                    holding = true;
            }
            if (!holding)
                freeTanks.push_back(tank);
        }
        if (freeTanks.empty())
            return open.front();

        uint32 index = 0;
        for (; index < freeTanks.size(); ++index)
        {
            if (freeTanks[index] == bot)
                break;
        }
        if (index >= freeTanks.size())
            return nullptr;

        return open[index % open.size()];
    }

    bool DangerPoint(PlayerbotAI* ai, Player* bot, float& dangerX, float& dangerY, float& dangerRange)
    {
        if (Creature* flame = FindNearestCreatureOfEntries(bot, {RAGNAROS_FLAME_ENTRY}))
        {
            if (bot->GetDistance(flame) <= RAGNAROS_FLAME_RANGE)
            {
                dangerX = flame->GetPositionX();
                dangerY = flame->GetPositionY();
                dangerRange = RAGNAROS_FLAME_RANGE;
                return true;
            }
        }

        AiObjectContext* context = ai->GetAiObjectContext();
        const std::list<HazardPosition>& hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
        const HazardPosition* closest = nullptr;
        float closestDistance = 9999.0f;
        for (const HazardPosition& hazard : hazards)
        {
            float const distance = bot->GetDistance(hazard.first.getX(), hazard.first.getY(), hazard.first.getZ());
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closest = &hazard;
            }
        }

        if (closest && closestDistance <= closest->second)
        {
            dangerX = closest->first.getX();
            dangerY = closest->first.getY();
            dangerRange = closest->second;
            return true;
        }

        return false;
    }

    float StopRangeFor(PlayerbotAI* ai, Player* bot)
    {
        if (ai->IsRanged(bot) || ai->IsHeal(bot))
            return 28.0f;
        return 6.0f;
    }

    float WrapAngle(float delta)
    {
        while (delta > M_PI_F)
            delta -= 2.0f * M_PI_F;
        while (delta < -M_PI_F)
            delta += 2.0f * M_PI_F;
        return delta;
    }

    // The straight segment passes through the lava around Ragnaros.
    bool SegmentHitsLava(float x1, float y1, float x2, float y2, float cx, float cy, float radius)
    {
        float const vx = x2 - x1;
        float const vy = y2 - y1;
        float const len2 = vx * vx + vy * vy;
        float t = len2 > 0.01f ? ((cx - x1) * vx + (cy - y1) * vy) / len2 : 0.0f;
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;

        float const px = x1 + vx * t - cx;
        float const py = y1 + vy * t - cy;
        return px * px + py * py < radius * radius;
    }

    bool ApproachPoint(Player* bot, Creature* boss, Unit* target, float stopRange, float& x, float& y, float& z)
    {
        float dx = bot->GetPositionX() - target->GetPositionX();
        float dy = bot->GetPositionY() - target->GetPositionY();
        float dist = sqrt(dx * dx + dy * dy);
        if (dist < 1.0f)
        {
            dx = bot->GetPositionX() - boss->GetPositionX();
            dy = bot->GetPositionY() - boss->GetPositionY();
            dist = sqrt(dx * dx + dy * dy);
            if (dist < 1.0f)
                dist = 1.0f;
        }

        x = target->GetPositionX() + dx / dist * stopRange;
        y = target->GetPositionY() + dy / dist * stopRange;

        float const fromCenterX = x - boss->GetPositionX();
        float const fromCenterY = y - boss->GetPositionY();
        float const fromCenter = sqrt(fromCenterX * fromCenterX + fromCenterY * fromCenterY);
        float const inner = InnerRing(boss);
        if (fromCenter < inner && fromCenter > 0.5f)
        {
            float const scale = inner / fromCenter;
            x = boss->GetPositionX() + fromCenterX * scale;
            y = boss->GetPositionY() + fromCenterY * scale;
        }

        z = PlaceZ(boss, x, y);
        return true;
    }

    // Walk the arc outside the lava. A chord to a Son of Flame on the far side cuts through the pit.
    void NextPlatformPoint(Player* bot, Creature* boss, float destX, float destY, float& x, float& y, float& z)
    {
        float const cx = boss->GetPositionX();
        float const cy = boss->GetPositionY();
        float const bx = bot->GetPositionX();
        float const by = bot->GetPositionY();
        float const inner = InnerRing(boss);

        if (!SegmentHitsLava(bx, by, destX, destY, cx, cy, inner))
        {
            x = destX;
            y = destY;
            z = PlaceZ(boss, x, y);
            return;
        }

        float const botAngle = atan2(by - cy, bx - cx);
        float const destAngle = atan2(destY - cy, destX - cx);
        float const delta = WrapAngle(destAngle - botAngle);
        float const step = 0.45f;
        float const turn = fabs(delta) < step ? delta : (delta > 0.0f ? step : -step);

        float radius = sqrt((bx - cx) * (bx - cx) + (by - cy) * (by - cy));
        if (radius < inner)
            radius = inner;
        if (radius > 40.0f)
            radius = 40.0f;

        if (fabs(delta) < 0.35f)
        {
            float destRadius = sqrt((destX - cx) * (destX - cx) + (destY - cy) * (destY - cy));
            if (destRadius < inner)
                destRadius = inner;
            if (destRadius > 40.0f)
                destRadius = 40.0f;
            radius = destRadius;
        }

        x = cx + cos(botAngle + turn) * radius;
        y = cy + sin(botAngle + turn) * radius;
        z = PlaceZ(boss, x, y);
    }
}

namespace ai
{
    Creature* FindRagnarosPlatformBoss(Player* bot)
    {
        return FindRagnaros(bot);
    }

    bool BotInRagnarosRoom(Player* bot)
    {
        return FindRagnaros(bot) != nullptr;
    }

    bool RagnarosBotAboveFloor(Player* bot)
    {
        Creature* boss = FindRagnaros(bot);
        return boss && AboveFloor(bot, boss);
    }

    bool RagnarosRingPoint(Player* bot, float& x, float& y, float& z)
    {
        return ComputeRingPoint(bot, x, y, z);
    }

    bool TryRagnarosRingLanding(Player* bot, float& x, float& y, float& z)
    {
        return ComputeRingPoint(bot, x, y, z);
    }

    Creature* FindRagnarosSonToTank(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot || !ai->IsTank(bot))
            return nullptr;
        return SelectSonToTank(ai, bot);
    }

    bool RagnarosSonsAlive(PlayerbotAI* ai)
    {
        return !ListSons(ai).empty();
    }
}

bool RagnarosSpreadAction::isUseful()
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!boss)
        return false;

    // Chasing a Son of Flame owns the movement. Healers, and tanks with nothing left to hold, stay on the ring.
    if (RagnarosSonsAlive(ai) && !ai->IsHeal(bot))
    {
        if (!ai->IsTank(bot) || FindRagnarosSonToTank(ai))
            return false;
    }

    if (RagnarosBotAboveFloor(bot))
        return true;

    float x, y, z;
    if (!RagnarosRingPoint(bot, x, y, z))
        return false;

    float const dx = bot->GetPositionX() - x;
    float const dy = bot->GetPositionY() - y;
    return dx * dx + dy * dy > RAGNAROS_SLOT_TOLERANCE * RAGNAROS_SLOT_TOLERANCE;
}

bool RagnarosSpreadAction::Execute(Event& /*event*/)
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    float x, y, z;
    if (!boss || !RagnarosRingPoint(bot, x, y, z))
        return false;

    if (RagnarosBotAboveFloor(bot))
    {
        ai->StopMoving();
        return bot->TeleportTo(bot->GetMapId(), x, y, z, bot->GetAngle(boss));
    }

    float stepX, stepY, stepZ;
    NextPlatformPoint(bot, boss, x, y, stepX, stepY, stepZ);
    return MoveTo(bot->GetMapId(), stepX, stepY, stepZ, false, false, true, true);
}

bool RagnarosStepAsideAction::isUseful()
{
    if (!BotInRagnarosRoom(bot))
        return false;

    float dangerX, dangerY, dangerRange;
    return DangerPoint(ai, bot, dangerX, dangerY, dangerRange);
}

bool RagnarosStepAsideAction::Execute(Event& /*event*/)
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    float dangerX, dangerY, dangerRange;
    if (!boss || !DangerPoint(ai, bot, dangerX, dangerY, dangerRange))
        return false;

    float const dx = bot->GetPositionX() - boss->GetPositionX();
    float const dy = bot->GetPositionY() - boss->GetPositionY();
    float radius = sqrt(dx * dx + dy * dy);
    if (radius < 12.0f)
        radius = 12.0f;
    if (radius > 40.0f)
        radius = 40.0f;

    float const angle = atan2(dy, dx);
    float bestX = bot->GetPositionX();
    float bestY = bot->GetPositionY();
    float bestDistance = -1.0f;
    for (float turn : { 0.8f, -0.8f, 1.6f, -1.6f })
    {
        float const x = boss->GetPositionX() + cos(angle + turn) * radius;
        float const y = boss->GetPositionY() + sin(angle + turn) * radius;
        float const ddx = x - dangerX;
        float const ddy = y - dangerY;
        float const distance = sqrt(ddx * ddx + ddy * ddy);
        if (distance > bestDistance)
        {
            bestDistance = distance;
            bestX = x;
            bestY = y;
        }
    }

    float const z = PlaceZ(boss, bestX, bestY);
    if (AboveFloor(bot, boss))
    {
        ai->StopMoving();
        return bot->TeleportTo(bot->GetMapId(), bestX, bestY, z, bot->GetAngle(boss));
    }

    return MoveTo(bot->GetMapId(), bestX, bestY, z, false, true, true, true);
}

bool AttackRagnarosBossAction::isUseful()
{
    if (ai->GetMainTank() != bot)
        return false;

    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!boss || BossSubmerged(boss))
        return false;

    if (RagnarosBotAboveFloor(bot))
        return true;

    if (!boss->CanReachWithMeleeAttack(bot))
        return true;

    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return target != boss;
}

bool AttackRagnarosBossAction::Execute(Event& event)
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!boss)
        return false;

    if (RagnarosBotAboveFloor(bot))
    {
        float x, y, z;
        if (!RagnarosRingPoint(bot, x, y, z))
            return false;

        ai->StopMoving();
        return bot->TeleportTo(bot->GetMapId(), x, y, z, bot->GetAngle(boss));
    }

    if (!boss->CanReachWithMeleeAttack(bot))
    {
        float x, y, z;
        if (!RagnarosRingPoint(bot, x, y, z))
            return false;

        float const dx = bot->GetPositionX() - x;
        float const dy = bot->GetPositionY() - y;
        if (dx * dx + dy * dy > RAGNAROS_SLOT_TOLERANCE * RAGNAROS_SLOT_TOLERANCE)
        {
            float stepX, stepY, stepZ;
            NextPlatformPoint(bot, boss, x, y, stepX, stepY, stepZ);
            return MoveTo(bot->GetMapId(), stepX, stepY, stepZ, false, false, true, true);
        }
    }

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, boss);
}

bool AttackRagnarosSonAction::isUseful()
{
    if (ai->IsHeal(bot))
        return false;

    Creature* son = ai->IsTank(bot) ? FindRagnarosSonToTank(ai) : FindAdd();
    if (!son)
        return false;

    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (boss && AboveFloor(bot, boss))
        return true;

    if (bot->GetDistance2d(son->GetPositionX(), son->GetPositionY()) > StopRangeFor(ai, bot))
        return true;

    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return target != son;
}

bool AttackRagnarosSonAction::Execute(Event& event)
{
    Creature* son = ai->IsTank(bot) ? FindRagnarosSonToTank(ai) : FindAdd();
    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!son || !boss)
        return false;

    float const stop = StopRangeFor(ai, bot);
    if (AboveFloor(bot, boss) || bot->GetDistance2d(son->GetPositionX(), son->GetPositionY()) > stop)
    {
        float x, y, z;
        if (!ApproachPoint(bot, boss, son, stop, x, y, z))
            return false;

        if (AboveFloor(bot, boss))
        {
            ai->StopMoving();
            return bot->TeleportTo(bot->GetMapId(), x, y, z, bot->GetAngle(son));
        }

        float stepX, stepY, stepZ;
        NextPlatformPoint(bot, boss, x, y, stepX, stepY, stepZ);
        return MoveTo(bot->GetMapId(), stepX, stepY, stepZ, false, false, true, true);
    }

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, son);
}
