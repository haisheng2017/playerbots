
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
