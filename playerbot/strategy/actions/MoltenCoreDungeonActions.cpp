
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"
#include "Maps/GridMap.h"
#include <algorithm>
#include <cmath>
#include <map>
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

    // Floor height of the fight platform. Rock pillars and the entrance shelf sit above this.
    constexpr float RAGNAROS_FLOOR_Z = -229.6f;

    // Dry floor measured from mmaps/4093033.mmtile around the scripted spawn (838.31, -831.47).
    // Index i is i * 10 degrees from +X. The lava pool is a spiral, so the safe radius changes
    // with angle. inner == 0 is the cut where that arm is only lava (about 120 to 160 degrees).
    struct SpiralSample
    {
        float inner;
        float outer;
        float z;
    };

    SpiralSample const RAGNAROS_SPIRAL[36] =
    {
        {8.0f, 30.5f, -229.5f}, {8.0f, 29.0f, -229.5f}, {8.0f, 28.5f, -229.5f}, {8.0f, 32.5f, -229.5f},
        {8.0f, 32.0f, -229.5f}, {8.0f, 28.0f, -229.5f}, {8.0f, 24.5f, -229.5f}, {8.0f, 23.0f, -229.5f},
        {8.0f, 23.0f, -229.5f}, {8.0f, 42.0f, -229.5f}, {26.0f, 40.0f, -229.9f}, {29.5f, 44.5f, -228.8f},
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
        {17.5f, 27.0f, -229.1f}, {18.0f, 31.5f, -229.1f}, {20.0f, 35.0f, -229.5f}, {19.5f, 35.5f, -229.9f},
        {20.0f, 38.0f, -229.6f}, {19.0f, 33.5f, -230.1f}, {19.0f, 35.5f, -230.1f}, {23.0f, 34.0f, -228.8f},
        {20.5f, 33.5f, -229.2f}, {18.5f, 32.0f, -229.7f}, {18.5f, 32.0f, -229.1f}, {18.0f, 34.0f, -229.1f},
        {16.5f, 35.0f, -229.1f}, {25.5f, 35.5f, -228.6f}, {8.5f, 35.5f, -230.0f}, {8.0f, 34.0f, -230.0f},
        {8.0f, 33.0f, -230.0f}, {8.0f, 32.0f, -230.0f}, {8.0f, 31.0f, -230.0f}
    };

    struct SpiralNode
    {
        float angle;
        float inner;
        float outer;
        float radius;
        float z;
    };

    struct SpiralChain
    {
        SpiralNode nodes[36];
        int count = 0;
        float length = 0.0f;
        float cum[36] = {};
    };

    float WrapAngle(float angle)
    {
        float const turn = 2.0f * M_PI_F;
        angle = std::fmod(angle, turn);
        if (angle < 0.0f)
            angle += turn;
        return angle;
    }

    float AngleDelta(float from, float to)
    {
        float delta = WrapAngle(to) - WrapAngle(from);
        if (delta > M_PI_F)
            delta -= 2.0f * M_PI_F;
        if (delta < -M_PI_F)
            delta += 2.0f * M_PI_F;
        return delta;
    }

    SpiralChain const& GetSpiralChain()
    {
        static SpiralChain chain;
        static bool ready = false;
        if (ready)
            return chain;

        ready = true;
        int start = 0;
        bool sawGap = false;
        for (int i = 0; i < 36; ++i)
        {
            if (RAGNAROS_SPIRAL[i].inner <= 0.0f)
                sawGap = true;
            else if (sawGap)
            {
                start = i;
                break;
            }
        }

        int previous = -1;
        for (int step = 0; step < 36; ++step)
        {
            int const index = (start + step) % 36;
            SpiralSample const& sample = RAGNAROS_SPIRAL[index];
            if (sample.inner <= 0.0f)
                break;

            float const radius = (sample.inner + sample.outer) * 0.5f;
            if (previous >= 0 && fabs(radius - chain.nodes[chain.count - 1].radius) > 14.0f)
                break;

            SpiralNode& node = chain.nodes[chain.count];
            node.angle = index * (10.0f * M_PI_F / 180.0f);
            node.inner = sample.inner;
            node.outer = sample.outer;
            node.radius = radius;
            node.z = sample.z;
            if (chain.count == 0)
                chain.cum[0] = 0.0f;
            else
            {
                SpiralNode const& before = chain.nodes[chain.count - 1];
                float const dx = cos(node.angle) * node.radius - cos(before.angle) * before.radius;
                float const dy = sin(node.angle) * node.radius - sin(before.angle) * before.radius;
                chain.length += sqrt(dx * dx + dy * dy);
                chain.cum[chain.count] = chain.length;
            }

            ++chain.count;
            previous = index;
        }

        sLog.outBasic("Ragnaros spiral stand line: %d dry samples, length %.0f, lava cut skipped",
            chain.count, chain.length);
        return chain;
    }

    void WriteSpiralPoint(Creature* boss, float angle, float radius, float pointZ, float& x, float& y, float& z)
    {
        x = boss->GetPositionX() + cos(angle) * radius;
        y = boss->GetPositionY() + sin(angle) * radius;
        z = pointZ;
    }

    // Stay inside the overlap of the two measured bands, then step melee toward the boss and ranged outward.
    void RadiusOnSegment(SpiralNode const& from, SpiralNode const& to, float t, bool melee, float& radius, float& z)
    {
        float const low = std::max(from.inner, to.inner) + 2.5f;
        float const high = std::min(from.outer, to.outer) - 2.5f;
        float const center = from.radius + (to.radius - from.radius) * t;
        z = from.z + (to.z - from.z) * t;
        if (low > high)
        {
            radius = (std::max(from.inner, to.inner) + std::min(from.outer, to.outer)) * 0.5f;
            return;
        }

        float const clamped = std::min(std::max(center, low), high);
        radius = melee ? std::max(low, clamped - 4.0f) : std::min(high, clamped + 4.0f);
    }

    bool PointAlongSpiral(Creature* boss, float fraction, bool melee, float& x, float& y, float& z)
    {
        SpiralChain const& chain = GetSpiralChain();
        if (chain.count < 2 || chain.length < 1.0f)
            return false;

        fraction = std::min(std::max(fraction, 0.0f), 0.999f);
        float const target = fraction * chain.length;
        int segment = 0;
        for (; segment < chain.count - 2; ++segment)
        {
            if (chain.cum[segment + 1] >= target)
                break;
        }

        float const span = chain.cum[segment + 1] - chain.cum[segment];
        float const t = span > 0.1f ? (target - chain.cum[segment]) / span : 0.0f;
        SpiralNode const& from = chain.nodes[segment];
        SpiralNode const& to = chain.nodes[segment + 1];
        float radius = 0.0f;
        float pointZ = 0.0f;
        RadiusOnSegment(from, to, t, melee, radius, pointZ);
        float const angle = from.angle + AngleDelta(from.angle, to.angle) * t;
        WriteSpiralPoint(boss, angle, radius, pointZ, x, y, z);
        return true;
    }

    bool PointAtSpiralAngle(Creature* boss, float angle, bool melee, float& x, float& y, float& z)
    {
        SpiralChain const& chain = GetSpiralChain();
        if (chain.count < 2)
            return false;

        angle = WrapAngle(angle);
        for (int i = 0; i < chain.count - 1; ++i)
        {
            SpiralNode const& from = chain.nodes[i];
            SpiralNode const& to = chain.nodes[i + 1];
            float const span = AngleDelta(from.angle, to.angle);
            float const along = AngleDelta(from.angle, angle);
            if (span <= 0.0f || along < -0.01f || along > span + 0.01f)
                continue;

            float t = along / span;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;

            float radius = 0.0f;
            float pointZ = 0.0f;
            RadiusOnSegment(from, to, t, melee, radius, pointZ);
            WriteSpiralPoint(boss, from.angle + span * t, radius, pointZ, x, y, z);
            return true;
        }

        SpiralNode const& first = chain.nodes[0];
        SpiralNode const& last = chain.nodes[chain.count - 1];
        SpiralNode const& nearest = fabs(AngleDelta(angle, first.angle)) <= fabs(AngleDelta(angle, last.angle)) ? first : last;
        float const low = nearest.inner + 2.5f;
        float const high = nearest.outer - 2.5f;
        float const radius = low <= high ? (melee ? low : high) : nearest.radius;
        WriteSpiralPoint(boss, nearest.angle, radius, nearest.z, x, y, z);
        return true;
    }

    bool AboveFloor(Player* bot, Creature* /*boss*/)
    {
        return bot->GetPositionZ() > RAGNAROS_FLOOR_Z + RAGNAROS_FLOOR_ABOVE;
    }

    bool OnInnerRing(PlayerbotAI* ai, Player* bot)
    {
        return ai->IsTank(bot) || (!ai->IsRanged(bot) && !ai->IsHeal(bot));
    }

    bool OutOfSlot(Player* bot, float x, float y, float z)
    {
        if (fabs(bot->GetPositionZ() - z) > 3.0f)
            return true;
        if (PositionInMagma(WorldPosition(bot)))
            return true;

        float const dx = bot->GetPositionX() - x;
        float const dy = bot->GetPositionY() - y;
        return dx * dx + dy * dy > 9.0f;
    }

    bool TeleportOntoFloor(PlayerbotAI* ai, Player* bot, Unit* face, float x, float y, float z)
    {
        ai->StopMoving();
        return bot->TeleportTo(bot->GetMapId(), x, y, z, face ? bot->GetAngle(face) : bot->GetOrientation());
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

        bool const melee = !ai || OnInnerRing(ai, bot);
        float const fraction = (index + 0.5f) / float(members.size());
        return PointAlongSpiral(boss, fraction, melee, x, y, z);
    }

    bool SonStandPoint(Player* bot, Unit* son, float& x, float& y, float& z)
    {
        Creature* boss = FindRagnaros(bot);
        if (!boss)
            return false;

        float const angle = atan2(son->GetPositionY() - boss->GetPositionY(), son->GetPositionX() - boss->GetPositionX());
        return PointAtSpiralAngle(boss, angle, true, x, y, z);
    }

    struct LineProjection
    {
        float arc = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float distance = 0.0f;
    };

    bool PointAtArc(Creature* boss, float arc, bool melee, float& x, float& y, float& z)
    {
        SpiralChain const& chain = GetSpiralChain();
        if (chain.count < 2)
            return false;

        if (arc < 0.0f)
            arc = 0.0f;
        if (arc > chain.length)
            arc = chain.length;

        for (int i = 0; i < chain.count - 1; ++i)
        {
            if (i < chain.count - 2 && chain.cum[i + 1] < arc)
                continue;

            float const span = chain.cum[i + 1] - chain.cum[i];
            float const t = span > 0.1f ? (arc - chain.cum[i]) / span : 0.0f;
            float radius = 0.0f;
            float pointZ = 0.0f;
            RadiusOnSegment(chain.nodes[i], chain.nodes[i + 1], std::min(std::max(t, 0.0f), 1.0f), melee, radius, pointZ);
            float const angle = chain.nodes[i].angle + AngleDelta(chain.nodes[i].angle, chain.nodes[i + 1].angle) * std::min(std::max(t, 0.0f), 1.0f);
            WriteSpiralPoint(boss, angle, radius, pointZ, x, y, z);
            return true;
        }

        return false;
    }

    bool ProjectOntoLine(Creature* boss, float x, float y, bool melee, LineProjection& hit)
    {
        SpiralChain const& chain = GetSpiralChain();
        if (chain.count < 2)
            return false;

        hit.distance = 1.0e9f;
        bool found = false;
        for (int i = 0; i < chain.count - 1; ++i)
        {
            float const span = chain.cum[i + 1] - chain.cum[i];
            for (int sample = 0; sample <= 4; ++sample)
            {
                float const t = sample / 4.0f;
                float radius = 0.0f;
                float pointZ = 0.0f;
                RadiusOnSegment(chain.nodes[i], chain.nodes[i + 1], t, melee, radius, pointZ);
                float const angle = chain.nodes[i].angle + AngleDelta(chain.nodes[i].angle, chain.nodes[i + 1].angle) * t;
                float px = 0.0f;
                float py = 0.0f;
                float pz = 0.0f;
                WriteSpiralPoint(boss, angle, radius, pointZ, px, py, pz);
                float const dx = px - x;
                float const dy = py - y;
                float const distance = sqrt(dx * dx + dy * dy);
                if (distance < hit.distance)
                {
                    hit.distance = distance;
                    hit.x = px;
                    hit.y = py;
                    hit.z = pz;
                    hit.arc = chain.cum[i] + t * span;
                    found = true;
                }
            }
        }

        return found;
    }

    // In the lava, on a pillar, or far enough from the line that a straight step would cross the pool.
    bool NeedsLineRescue(Player* bot, Creature* boss, bool melee)
    {
        if (PositionInMagma(WorldPosition(bot)))
            return true;
        if (AboveFloor(bot, boss))
            return true;

        LineProjection here;
        if (!ProjectOntoLine(boss, bot->GetPositionX(), bot->GetPositionY(), melee, here))
            return true;

        return here.distance > 12.0f;
    }

    bool NextLinePoint(Creature* boss, Player* bot, float destX, float destY, float destZ, bool melee, float& x, float& y, float& z)
    {
        float const dx = bot->GetPositionX() - destX;
        float const dy = bot->GetPositionY() - destY;
        if (dx * dx + dy * dy <= 9.0f && fabs(bot->GetPositionZ() - destZ) <= 3.0f)
            return false;

        LineProjection here;
        LineProjection dest;
        if (!ProjectOntoLine(boss, bot->GetPositionX(), bot->GetPositionY(), melee, here))
            return false;
        if (!ProjectOntoLine(boss, destX, destY, melee, dest))
            return false;

        if (here.distance > 6.0f)
        {
            x = here.x;
            y = here.y;
            z = here.z;
            return true;
        }

        float const step = 10.0f;
        float const delta = dest.arc - here.arc;
        if (fabs(delta) <= step)
        {
            x = destX;
            y = destY;
            z = destZ;
            return true;
        }

        float targetArc = here.arc + (delta > 0.0f ? step : -step);
        if (!PointAtArc(boss, targetArc, melee, x, y, z))
            return false;

        float const stepDx = bot->GetPositionX() - x;
        float const stepDy = bot->GetPositionY() - y;
        if (stepDx * stepDx + stepDy * stepDy < 6.25f)
            return PointAtArc(boss, targetArc + (delta > 0.0f ? step : -step), melee, x, y, z);

        return true;
    }

    struct IssuedLineStep
    {
        float x = 0.0f;
        float y = 0.0f;
        float distance = 0.0f;
        time_t when = 0;
    };

    std::map<uint32, IssuedLineStep>& LineSteps()
    {
        static std::map<uint32, IssuedLineStep> steps;
        return steps;
    }

    // True while the last walk order is still the point we would issue, and the bot has not arrived or stuck.
    bool HoldIssuedStep(Player* bot, float x, float y)
    {
        auto it = LineSteps().find(bot->GetGUIDLow());
        if (it == LineSteps().end())
            return false;

        float const orderDx = it->second.x - x;
        float const orderDy = it->second.y - y;
        if (orderDx * orderDx + orderDy * orderDy > 16.0f)
            return false;

        float const dx = bot->GetPositionX() - x;
        float const dy = bot->GetPositionY() - y;
        float const distance = sqrt(dx * dx + dy * dy);
        if (distance < 2.5f)
            return false;

        if (time(nullptr) >= it->second.when + 2 && distance > it->second.distance - 1.0f)
            return false;

        return true;
    }

    void RememberStep(Player* bot, float x, float y)
    {
        float const dx = bot->GetPositionX() - x;
        float const dy = bot->GetPositionY() - y;
        LineSteps()[bot->GetGUIDLow()] = { x, y, sqrt(dx * dx + dy * dy), time(nullptr) };
    }

    void ForgetStep(Player* bot)
    {
        LineSteps().erase(bot->GetGUIDLow());
    }

    enum class LineOrder
    {
        Stay,
        Walk,
        Rescue
    };

    struct LineStep
    {
        LineOrder order = LineOrder::Stay;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    LineStep PlanLineStep(Player* bot, Creature* boss, float destX, float destY, float destZ, bool melee)
    {
        LineStep step;
        step.x = destX;
        step.y = destY;
        step.z = destZ;

        if (NeedsLineRescue(bot, boss, melee))
        {
            step.order = LineOrder::Rescue;
            return step;
        }

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!NextLinePoint(boss, bot, destX, destY, destZ, melee, x, y, z))
            return step;

        if (HoldIssuedStep(bot, x, y))
            return step;

        step.order = LineOrder::Walk;
        step.x = x;
        step.y = y;
        step.z = z;
        return step;
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

    // A tank with a Son of Flame teleports to that add's side of the ring instead of this slot.
    if (ai->IsTank(bot) && RagnarosSonsAlive(ai) && FindRagnarosSonToTank(ai))
        return false;

    float x, y, z;
    if (!RagnarosRingPoint(bot, x, y, z))
        return false;

    bool const melee = OnInnerRing(ai, bot);
    return PlanLineStep(bot, boss, x, y, z, melee).order != LineOrder::Stay;
}

bool RagnarosSpreadAction::Execute(Event& /*event*/)
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    float x, y, z;
    if (!boss || !RagnarosRingPoint(bot, x, y, z))
        return false;

    bool const melee = OnInnerRing(ai, bot);
    LineStep const step = PlanLineStep(bot, boss, x, y, z, melee);
    if (step.order == LineOrder::Rescue)
    {
        ForgetStep(bot);
        return TeleportOntoFloor(ai, bot, boss, x, y, z);
    }
    if (step.order != LineOrder::Walk)
        return false;

    if (!MoveTo(bot->GetMapId(), step.x, step.y, step.z, false, true, true, true))
        return false;

    RememberStep(bot, step.x, step.y);
    return true;
}

bool RagnarosStepAsideAction::isUseful()
{
    if (!BotInRagnarosRoom(bot))
        return false;

    float dangerX, dangerY, dangerRange;
    if (!DangerPoint(ai, bot, dangerX, dangerY, dangerRange))
        return false;

    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!boss)
        return false;

    float bestX = bot->GetPositionX();
    float bestY = bot->GetPositionY();
    float bestZ = bot->GetPositionZ();
    float bestDistance = -1.0f;
    float const angle = atan2(bot->GetPositionY() - boss->GetPositionY(), bot->GetPositionX() - boss->GetPositionX());
    bool const melee = OnInnerRing(ai, bot);
    for (float turn : { 0.7f, -0.7f, 1.4f, -1.4f })
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!PointAtSpiralAngle(boss, angle + turn, melee, x, y, z))
            continue;

        float const ddx = x - dangerX;
        float const ddy = y - dangerY;
        float const distance = sqrt(ddx * ddx + ddy * ddy);
        if (distance > bestDistance)
        {
            bestDistance = distance;
            bestX = x;
            bestY = y;
            bestZ = z;
        }
    }

    if (bestDistance < 0.0f)
        return false;

    return PlanLineStep(bot, boss, bestX, bestY, bestZ, melee).order != LineOrder::Stay;
}

bool RagnarosStepAsideAction::Execute(Event& /*event*/)
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    float dangerX, dangerY, dangerRange;
    if (!boss || !DangerPoint(ai, bot, dangerX, dangerY, dangerRange))
        return false;

    float const angle = atan2(bot->GetPositionY() - boss->GetPositionY(), bot->GetPositionX() - boss->GetPositionX());
    bool const melee = OnInnerRing(ai, bot);

    float bestX = bot->GetPositionX();
    float bestY = bot->GetPositionY();
    float bestZ = bot->GetPositionZ();
    float bestDistance = -1.0f;
    for (float turn : { 0.7f, -0.7f, 1.4f, -1.4f })
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!PointAtSpiralAngle(boss, angle + turn, melee, x, y, z))
            continue;

        float const ddx = x - dangerX;
        float const ddy = y - dangerY;
        float const distance = sqrt(ddx * ddx + ddy * ddy);
        if (distance > bestDistance)
        {
            bestDistance = distance;
            bestX = x;
            bestY = y;
            bestZ = z;
        }
    }

    if (bestDistance < 0.0f)
        return false;

    LineStep const step = PlanLineStep(bot, boss, bestX, bestY, bestZ, melee);
    if (step.order == LineOrder::Rescue)
    {
        ForgetStep(bot);
        return TeleportOntoFloor(ai, bot, boss, bestX, bestY, bestZ);
    }
    if (step.order != LineOrder::Walk)
        return false;

    if (!MoveTo(bot->GetMapId(), step.x, step.y, step.z, false, true, true, true))
        return false;

    RememberStep(bot, step.x, step.y);
    return true;
}

bool AttackRagnarosBossAction::isUseful()
{
    if (ai->GetMainTank() != bot)
        return false;

    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!boss || BossSubmerged(boss))
        return false;

    float x, y, z;
    if (RagnarosRingPoint(bot, x, y, z) && OutOfSlot(bot, x, y, z))
        return false;

    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return target != boss;
}

bool AttackRagnarosBossAction::Execute(Event& event)
{
    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!boss)
        return false;

    float x, y, z;
    if (RagnarosRingPoint(bot, x, y, z) && OutOfSlot(bot, x, y, z))
        return false;

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

    if (ai->IsTank(bot))
    {
        float x, y, z;
        Creature* boss = FindRagnarosPlatformBoss(bot);
        if (boss && son->GetVictim() != bot && SonStandPoint(bot, son, x, y, z) && OutOfSlot(bot, x, y, z))
            return PlanLineStep(bot, boss, x, y, z, true).order != LineOrder::Stay;
    }
    else if (bot->GetDistance2d(son->GetPositionX(), son->GetPositionY()) > StopRangeFor(ai, bot))
        return false;

    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return target != son;
}

bool AttackRagnarosSonAction::Execute(Event& event)
{
    Creature* son = ai->IsTank(bot) ? FindRagnarosSonToTank(ai) : FindAdd();
    Creature* boss = FindRagnarosPlatformBoss(bot);
    if (!son || !boss)
        return false;

    if (ai->IsTank(bot) && son->GetVictim() != bot)
    {
        float x, y, z;
        if (SonStandPoint(bot, son, x, y, z) && OutOfSlot(bot, x, y, z))
        {
            LineStep const step = PlanLineStep(bot, boss, x, y, z, true);
            if (step.order == LineOrder::Rescue)
            {
                ForgetStep(bot);
                return TeleportOntoFloor(ai, bot, boss, x, y, z);
            }
            if (step.order != LineOrder::Walk)
                return false;

            if (!MoveTo(bot->GetMapId(), step.x, step.y, step.z, false, true, true, true))
                return false;

            RememberStep(bot, step.x, step.y);
            return true;
        }
    }

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, son);
}
