#include "playerbot/playerbot.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

using namespace ai;

float PreventMoveAwayFromCreatureOnReachToCastMultiplier::GetValue(Action* action)
{
    MoveAwayFromCreature* moveAwayAction = dynamic_cast<MoveAwayFromCreature*>(action);
    if (moveAwayAction)
    {
        const Action* lastExecutedAction = ai->GetLastExecutedAction(BotState::BOT_STATE_COMBAT);
        if (lastExecutedAction)
        {
            const ReachTargetAction* reachAction = dynamic_cast<const ReachTargetAction*>(lastExecutedAction);
            if (reachAction && !reachAction->GetSpellName().empty())
            {
                return 0.0f;
            }
        }
    }

    return 1.0f;
}

float KeepMainTankOnBossMultiplier::GetValue(Action* action)
{
    if (!action || action->getName() != "tank assist")
        return 1.0f;

    // Only the main tank is pinned to the boss
    if (ai->GetMainTank() != bot)
        return 1.0f;

    // While any of this encounter's adds is still alive the main tank stays on the boss
    std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
    for (const ObjectGuid& guid : possibleTargets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT)
            continue;

        for (uint32 entry : addEntries)
        {
            if (unit->GetEntry() == entry)
                return 0.0f;
        }
    }

    return 1.0f;
}

float BossRtiAssistMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    const std::string& name = action->getName();
    if (name != "dps assist" && name != "dps aoe" && name != "tank assist")
        return 1.0f;

    // Only relevant while the adds are up: once they are dead a skull on the boss is the
    // correct kill order and the generic assist takes over again on its own
    bool addsAlive = false;
    std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
    for (const ObjectGuid& guid : possibleTargets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT)
            continue;

        for (uint32 entry : addEntries)
        {
            if (unit->GetEntry() == entry)
            {
                addsAlive = true;
                break;
            }
        }

        if (addsAlive)
            break;
    }

    if (!addsAlive)
        return 1.0f;

    // Skull parked on the boss: hold the assist back so it does not yank anyone off the adds
    Unit* rti = ai->GetUnit(AI_VALUE(ObjectGuid, "rti target"));
    if (rti && rti->IsAlive() && rti->GetTypeId() == TYPEID_UNIT && rti->GetEntry() == bossEntry)
        return 0.0f;

    return 1.0f;
}

float KeepRaidOnBossMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    // Which generic switch would pull this bot off its fight assignment
    const std::string& name = action->getName();
    bool offTankAssist = name == "tank assist" && ai->IsTank(bot) && ai->GetMainTank() != bot;
    bool raidAssist = (name == "dps assist" || name == "dps aoe") && !ai->IsTank(bot);
    if (!offTankAssist && !raidAssist)
        return 1.0f;

    // The assignments only bind while the boss and at least one of his adds are up:
    // after that the generic assist takes over again on its own (e.g. cleaning up
    // the leftover adds once the boss has died)
    bool bossAlive = false;
    bool addsAlive = false;
    std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
    for (const ObjectGuid& guid : possibleTargets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT)
            continue;

        if (unit->GetEntry() == bossEntry)
            bossAlive = true;

        for (uint32 entry : addEntries)
        {
            if (unit->GetEntry() == entry)
                addsAlive = true;
        }
    }

    return (bossAlive && addsAlive) ? 0.0f : 1.0f;
}
