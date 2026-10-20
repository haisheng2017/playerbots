#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonStrategies.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"

using namespace ai;

namespace
{
    class OnyxiaFightMultiplier : public Multiplier
    {
    public:
        OnyxiaFightMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia fight") {}

        float GetValue(Action* action) override
        {
            if (!action)
                return 1.0f;

            const std::string& name = action->getName();
            if (ShouldHoldOnyxiaDps(ai))
            {
                if (name == "dps assist" || name == "tank assist" || name == "melee" || name == "shoot" || name == "pet attack" ||
                    name == "reach melee" || name == "reach spell")
                    return 0.0f;

                if (action->getThreatType() != ActionThreatType::ACTION_THREAT_NONE)
                    return 0.0f;
            }

            if ((name == "dps assist" || name == "tank assist") && ShouldAttackOnyxiaWhelp(ai))
                return 0.0f;

            Unit* whelpTarget = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
            if (whelpTarget && whelpTarget->GetEntry() == 11262 &&
                IsInsideOnyxiaWhelpPit(whelpTarget->GetPositionX(), whelpTarget->GetPositionY()))
            {
                if (name == "reach melee" || name == "reach spell" || name == "melee" || name == "shoot" ||
                    name == "dps assist" || name == "tank assist")
                    return 0.0f;
            }

            if (name == "taunt" || name == "mocking blow" || name == "growl" || name == "hand of reckoning" || name == "righteous defense")
            {
                Unit* onyxia = FindOnyxia(bot);
                bool airborne = onyxia && GetOnyxiaPhase(onyxia) == 2;
                if (airborne || !IsOnyxiaMainTank(ai))
                    return 0.0f;
            }

            if (name == "reach melee" || name == "reach spell")
            {
                Unit* onyxia = FindOnyxia(bot);
                if (onyxia && GetOnyxiaPhase(onyxia) == 2)
                {
                    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
                    bool onWhelp = target && target->GetEntry() == 11262;
                    if (!onWhelp)
                    {
                        if (name == "reach melee")
                            return 0.0f;

                        // Ranged chase her so they can keep casting. Healers stay on their sector.
                        if (ai->IsRanged(bot) && !ai->IsHeal(bot))
                            return 1.0f;

                        if (!target || target->GetEntry() == 10184)
                            return 0.0f;
                    }
                }
            }

            return 1.0f;
        }
    };
}

void OnyxiasLairDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start onyxia fight",
        NextAction::array(0, new NextAction("enable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "onyxia deep breath",
        NextAction::array(0, new NextAction("move away from onyxia breath", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia out of lair",
        NextAction::array(0, new NextAction("move back into onyxia lair", 95.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia main tank taunt",
        NextAction::array(0, new NextAction("onyxia main tank taunt", 94.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia tank spot",
        NextAction::array(0, new NextAction("move to onyxia tank spot", 80.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia hold dps",
        NextAction::array(0, new NextAction("onyxia hold dps", 70.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia fear ward",
        NextAction::array(0, new NextAction("onyxia fear ward", 91.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia tremor totem",
        NextAction::array(0, new NextAction("tremor totem", 91.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia whelp",
        NextAction::array(0, new NextAction("attack onyxia whelp", 65.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia whelp lure",
        NextAction::array(0, new NextAction("lure onyxia whelp", 64.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "onyxia move to position",
        NextAction::array(0, new NextAction("move to onyxia position", 31.0f), NULL)));
}

void OnyxiaFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end onyxia fight",
        NextAction::array(0, new NextAction("disable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end onyxia fight",
        NextAction::array(0, new NextAction("disable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "onyxia deep breath",
        NextAction::array(0, new NextAction("move away from onyxia breath", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new OnyxiaFightMultiplier(ai));
}

void OnyxiaFightStrategy::OnStrategyAdded(BotState state)
{
    if (state == BotState::BOT_STATE_COMBAT)
    {
        // Standing behind her is the tail swipe. Side slots replace that.
        if (ai->HasStrategy("behind", BotState::BOT_STATE_COMBAT))
        {
            ai->ChangeStrategy("-behind", BotState::BOT_STATE_COMBAT);
            m_removedBehind = true;
        }
    }

    // Chase jumps cut across the room and through the whelp pits.
    if (state <= BotState::BOT_STATE_REACTION && ai->HasStrategy("chase jump", state))
    {
        ai->ChangeStrategy("-chase jump", state);
        m_removedChaseJump |= uint8(1) << uint8(state);
    }
}

void OnyxiaFightStrategy::OnStrategyRemoved(BotState state)
{
    if (state == BotState::BOT_STATE_COMBAT && m_removedBehind)
    {
        ai->ChangeStrategy("+behind", BotState::BOT_STATE_COMBAT);
        m_removedBehind = false;
    }

    uint8 const bit = uint8(1) << uint8(state);
    if (state <= BotState::BOT_STATE_REACTION && (m_removedChaseJump & bit))
    {
        ai->ChangeStrategy("+chase jump", state);
        m_removedChaseJump &= uint8(~bit);
    }
}
