#pragma once

#include "playerbot/strategy/Multiplier.h"

class Action;

namespace ai
{
    class PreventMoveAwayFromCreatureOnReachToCastMultiplier : public Multiplier
    {
    public:
        PreventMoveAwayFromCreatureOnReachToCastMultiplier(PlayerbotAI* ai) : Multiplier(ai, "cast spell after reach") {}

    public:
        virtual float GetValue(Action* action) override;
    };

    // Keeps the main tank on the boss while the encounter's adds are still up.
    // Suppresses the target switch of "tank assist" so the main tank never abandons
    // the boss for an add (Molten Core: Sulfuron's priests, Majordomo's Flamewakers).
    class KeepMainTankOnBossMultiplier : public Multiplier
    {
    public:
        KeepMainTankOnBossMultiplier(PlayerbotAI* ai, std::vector<uint32> addEntries)
            : Multiplier(ai, "keep main tank on boss"), addEntries(addEntries) {}

    public:
        virtual float GetValue(Action* action) override;

    private:
        std::vector<uint32> addEntries;
    };

    // While the encounter's adds are alive the raid must stay on them.
    // When a skull is parked on the boss the RTI-priority of "dps assist"/"tank assist"
    // would yank DPS and off-tanks off the adds into an endless target swap:
    // hold those assists back until the skull moves or the adds are dead.
    class BossRtiAssistMultiplier : public Multiplier
    {
    public:
        BossRtiAssistMultiplier(PlayerbotAI* ai, uint32 bossEntry, std::vector<uint32> addEntries)
            : Multiplier(ai, "boss rti assist"), bossEntry(bossEntry), addEntries(addEntries) {}

    public:
        virtual float GetValue(Action* action) override;

    private:
        uint32 bossEntry;
        std::vector<uint32> addEntries;
    };

    // Garr-style fights: while the boss and his adds are both up the raid runs entirely on
    // the fight strategy's own target assignments - the main tank holds the boss, the
    // off-tanks hold the adds and everyone else damages the boss. The generic assists
    // would keep re-targeting (least-HP add, skull, ...) and unravel those assignments,
    // so hold them back:
    // - "tank assist" for every tank but the main tank (the fight action drives them)
    // - "dps assist"/"dps aoe" for the non-tanks (their damage stays on the boss)
    class KeepRaidOnBossMultiplier : public Multiplier
    {
    public:
        KeepRaidOnBossMultiplier(PlayerbotAI* ai, uint32 bossEntry, std::vector<uint32> addEntries)
            : Multiplier(ai, "keep raid on boss"), bossEntry(bossEntry), addEntries(addEntries) {}

    public:
        virtual float GetValue(Action* action) override;

    private:
        uint32 bossEntry;
        std::vector<uint32> addEntries;
    };
}
