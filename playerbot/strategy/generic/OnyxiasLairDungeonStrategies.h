#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class OnyxiasLairDungeonStrategy : public Strategy
    {
    public:
        OnyxiasLairDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "onyxias lair"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class OnyxiaFightStrategy : public Strategy
    {
    public:
        OnyxiaFightStrategy(PlayerbotAI* ai) : Strategy(ai), m_removedBehind(false) {}
        std::string getName() override { return "onyxia"; }
        void OnStrategyAdded(BotState state) override;
        void OnStrategyRemoved(BotState state) override;

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;

        bool m_removedBehind;
    };
}