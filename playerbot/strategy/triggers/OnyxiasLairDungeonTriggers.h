#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class OnyxiasLairEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        OnyxiasLairEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter onyxias lair", "onyxias lair", 249) {}
    };

    class OnyxiasLairLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        OnyxiasLairLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave onyxias lair", "onyxias lair", 249) {}
    };

    class OnyxiaStartFightTrigger : public StartBossFightTrigger
    {
    public:
        OnyxiaStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start onyxia fight", "onyxia", 10184) {}
    };

    class OnyxiaEndFightTrigger : public EndBossFightTrigger
    {
    public:
        OnyxiaEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end onyxia fight", "onyxia", 10184) {}
    };

    class OnyxiaDeepBreathTrigger : public Trigger
    {
    public:
        OnyxiaDeepBreathTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia deep breath", 1) {}
        bool IsActive() override;
    };

    class OnyxiaOutOfLairTrigger : public Trigger
    {
    public:
        OnyxiaOutOfLairTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia out of lair", 1) {}
        bool IsActive() override;
    };

    class OnyxiaWhelpTrigger : public Trigger
    {
    public:
        OnyxiaWhelpTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia whelp", 1) {}
        bool IsActive() override;
    };

    class OnyxiaPositionTrigger : public Trigger
    {
    public:
        OnyxiaPositionTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia move to position", 1) {}
        bool IsActive() override;
    };

    class OnyxiaFearWardTrigger : public Trigger
    {
    public:
        OnyxiaFearWardTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia fear ward", 1) {}
        bool IsActive() override;
    };

    class OnyxiaTremorTotemTrigger : public Trigger
    {
    public:
        OnyxiaTremorTotemTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia tremor totem", 1) {}
        bool IsActive() override;
    };

    class OnyxiaMainTankTauntTrigger : public Trigger
    {
    public:
        OnyxiaMainTankTauntTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia main tank taunt", 1) {}
        bool IsActive() override;
    };

    class OnyxiaTankSpotTrigger : public Trigger
    {
    public:
        OnyxiaTankSpotTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia tank spot", 1) {}
        bool IsActive() override;
    };

    class OnyxiaHoldDpsTrigger : public Trigger
    {
    public:
        OnyxiaHoldDpsTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia hold dps", 1) {}
        bool IsActive() override;
    };

    class OnyxiaWhelpLureTrigger : public Trigger
    {
    public:
        OnyxiaWhelpLureTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia whelp lure", 1) {}
        bool IsActive() override;
    };
}