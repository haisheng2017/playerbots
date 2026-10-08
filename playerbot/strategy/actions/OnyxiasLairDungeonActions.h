#pragma once
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "AttackAction.h"

namespace ai
{
    class OnyxiasLairEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiasLairEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable onyxias lair strategy", "+onyxias lair") {}
    };

    class OnyxiasLairDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiasLairDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable onyxias lair strategy", "-onyxias lair") {}
    };

    class OnyxiaEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiaEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable onyxia fight strategy", "+onyxia") {}
    };

    class OnyxiaDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiaDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable onyxia fight strategy", "-onyxia") {}
    };

    class OnyxiaMoveAwayFromBreathAction : public MovementAction
    {
    public:
        OnyxiaMoveAwayFromBreathAction(PlayerbotAI* ai) : MovementAction(ai, "move away from onyxia breath") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaMoveBackIntoLairAction : public MovementAction
    {
    public:
        OnyxiaMoveBackIntoLairAction(PlayerbotAI* ai) : MovementAction(ai, "move back into onyxia lair") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaMoveToPositionAction : public MovementAction
    {
    public:
        OnyxiaMoveToPositionAction(PlayerbotAI* ai) : MovementAction(ai, "move to onyxia position") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaAttackWhelpAction : public AttackAction
    {
    public:
        OnyxiaAttackWhelpAction(PlayerbotAI* ai) : AttackAction(ai, "attack onyxia whelp") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaFearWardAction : public Action
    {
    public:
        OnyxiaFearWardAction(PlayerbotAI* ai) : Action(ai, "onyxia fear ward") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaMainTankTauntAction : public Action
    {
    public:
        OnyxiaMainTankTauntAction(PlayerbotAI* ai) : Action(ai, "onyxia main tank taunt") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaMoveToTankSpotAction : public MovementAction
    {
    public:
        OnyxiaMoveToTankSpotAction(PlayerbotAI* ai) : MovementAction(ai, "move to onyxia tank spot") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaHoldDpsAction : public Action
    {
    public:
        OnyxiaHoldDpsAction(PlayerbotAI* ai) : Action(ai, "onyxia hold dps") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class OnyxiaLureWhelpAction : public MovementAction
    {
    public:
        OnyxiaLureWhelpAction(PlayerbotAI* ai) : MovementAction(ai, "lure onyxia whelp") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    Unit* FindOnyxia(Player* bot);
    Unit* FindNearestOnyxiaWhelp(Player* bot);
    Unit* FindOnyxiaWhelpInRoom(Player* bot);
    int GetOnyxiaPhase(Unit* onyxia);
    bool IsOnyxiaMainTank(PlayerbotAI* ai);
    bool ShouldOnyxiaMainTankTaunt(PlayerbotAI* ai);
    bool ShouldAttackOnyxiaWhelp(PlayerbotAI* ai);
    bool ShouldHoldOnyxiaDps(PlayerbotAI* ai);
    bool IsInsideOnyxiaWhelpPit(float x, float y);
    bool GetOnyxiaBreathDodgePoint(Player* bot, float& x, float& y, float& z);
    bool GetOnyxiaLairReturnPoint(Player* bot, float& x, float& y, float& z);
    bool GetOnyxiaPositionPoint(PlayerbotAI* ai, float& x, float& y, float& z);
    bool GetOnyxiaTankPoint(PlayerbotAI* ai, float& x, float& y, float& z);
    bool GetOnyxiaWhelpLurePoint(PlayerbotAI* ai, float& x, float& y, float& z);
    Unit* FindOnyxiaFearWardTarget(PlayerbotAI* ai);
}
