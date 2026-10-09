#pragma once
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "UseItemAction.h"
#include "AttackAction.h"

namespace ai
{
    class MoltenCoreEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MoltenCoreEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable molten core strategy", "+molten core") {}
    };

    class MoltenCoreDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MoltenCoreDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable molten core strategy", "-molten core") {}
    };

    class MagmadarEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MagmadarEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable magmadar fight strategy", "+magmadar") {}
    };

    class MagmadarDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MagmadarDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable magmadar fight strategy", "-magmadar") {}
    };

    class MagmadarMoveAwayFromLavaBombAction : public MoveAwayFromHazard
    {
    public:
        MagmadarMoveAwayFromLavaBombAction(PlayerbotAI* ai) : MoveAwayFromHazard(ai, "move away from magmadar lava bomb") {}
    };

    class MagmadarMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        MagmadarMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from magmadar", 11982, 31.0f) {}
    };

    class BaronGeddonEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        BaronGeddonEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable baron geddon fight strategy", "+baron geddon") {}
    };

    class BaronGeddonDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        BaronGeddonDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable baron geddon fight strategy", "-baron geddon") {}
    };

    class ShazzrahEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        ShazzrahEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable shazzrah fight strategy", "+shazzrah") {}
    };

    class ShazzrahDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        ShazzrahDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable shazzrah fight strategy", "-shazzrah") {}
    };

    class ShazzrahMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        ShazzrahMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from shazzrah", 12264, 31.0f) {}
    };

    class SulfuronEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        SulfuronEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable sulfuron fight strategy", "+sulfuron") {}
    };

    class SulfuronDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        SulfuronDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable sulfuron fight strategy", "-sulfuron") {}
    };

    class MajordomoEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MajordomoEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable majordomo fight strategy", "+majordomo") {}
    };

    class MajordomoDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        MajordomoDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable majordomo fight strategy", "-majordomo") {}
    };

    class GarrEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GarrEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable garr fight strategy", "+garr") {}
    };

    class GarrDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        GarrDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable garr fight strategy", "-garr") {}
    };

    // Garr is the banish fight: every warlock keeps one Firesworn banished while it lasts.
    // "cc" is inert for the other classes here - their CCs cannot land on an elemental.
    class GarrEnableWarlockCcAction : public ChangeAllStrategyAction
    {
    public:
        GarrEnableWarlockCcAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable garr warlock cc", "+cc") {}
    };

    class GarrDisableWarlockCcAction : public ChangeAllStrategyAction
    {
    public:
        GarrDisableWarlockCcAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable garr warlock cc", "-cc") {}
    };

    class MajordomoMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        MajordomoMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from majordomo", 12018, 16.0f) {}
    };

    class BaronGeddonMoveAwayFromInfernoAction : public MoveAwayFromCreature
    {
    public:
        BaronGeddonMoveAwayFromInfernoAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from baron geddon inferno", 12056, 18.0f) {}
    };

    class BaronGeddonMoveAwayFromArmageddonAction : public MoveAwayFromCreature
    {
    public:
        BaronGeddonMoveAwayFromArmageddonAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from baron geddon armageddon", 12056, 35.0f) {}
    };

    // Living Bomb carrier (aura 20475): run away from other players and the boss until it goes off
    class BaronGeddonLivingBombEscapeAction : public MovementAction
    {
    public:
        BaronGeddonLivingBombEscapeAction(PlayerbotAI* ai) : MovementAction(ai, "baron geddon living bomb escape") {}
        bool Execute(Event& event) override;
        bool isPossible() override;
    };

    // Majordomo cannot be killed and Sulfuron's priests heal everything:
    // attack the encounter's adds instead of the boss himself while they live
    class AttackBossAddAction : public AttackAction
    {
    public:
        AttackBossAddAction(PlayerbotAI* ai, std::string name, std::vector<uint32> addEntries)
            : AttackAction(ai, name), addEntries(addEntries) {}

        bool Execute(Event& event) override;
        bool isUseful() override;

    protected:
        Creature* FindAdd();

    private:
        std::vector<uint32> addEntries;
    };

    class MajordomoAttackAddAction : public AttackBossAddAction
    {
    public:
        MajordomoAttackAddAction(PlayerbotAI* ai) : AttackBossAddAction(ai, "attack majordomo add", {11663, 11664}) {}
    };

    class SulfuronAttackAddAction : public AttackBossAddAction
    {
    public:
        SulfuronAttackAddAction(PlayerbotAI* ai) : AttackBossAddAction(ai, "attack sulfuron add", {11662}) {}
    };

    // The main tank belongs on the boss while the adds are up:
    // pulls him back after a one-shot player command or a pull gone sideways
    class AttackBossEntryAction : public AttackAction
    {
    public:
        AttackBossEntryAction(PlayerbotAI* ai, std::string name, uint32 bossEntry)
            : AttackAction(ai, name), bossEntry(bossEntry) {}

        bool Execute(Event& event) override;
        bool isUseful() override;

    protected:
        uint32 bossEntry;
    };

    class AttackSulfuronBossAction : public AttackBossEntryAction
    {
    public:
        AttackSulfuronBossAction(PlayerbotAI* ai) : AttackBossEntryAction(ai, "attack sulfuron boss", 12098) {}
    };

    class AttackMajordomoBossAction : public AttackBossEntryAction
    {
    public:
        AttackMajordomoBossAction(PlayerbotAI* ai) : AttackBossEntryAction(ai, "attack majordomo boss", 12018) {}
    };

    // The main tank's pull back onto Garr while his Firesworn are still up
    class AttackGarrBossAction : public AttackBossEntryAction
    {
    public:
        AttackGarrBossAction(PlayerbotAI* ai) : AttackBossEntryAction(ai, "attack garr boss", 12057) {}
    };

    // Garr: the whole raid's damage stays on the boss while he lives - the Firesworn
    // are tanked/banished, not killed. Usable by everyone but the tanks
    // (who hold the boss and the adds) and the healers (who do not attack)
    class AttackGarrAction : public AttackBossEntryAction
    {
    public:
        AttackGarrAction(PlayerbotAI* ai) : AttackBossEntryAction(ai, "attack garr", 12057) {}
        bool isUseful() override;
    };

    // An off-tank's Firesworn: the loose one chewing on a non-tank first, else the
    // one it has built the least personal threat on (so the off-tanks spread out)
    class AttackGarrFireswornAction : public AttackAction
    {
    public:
        AttackGarrFireswornAction(PlayerbotAI* ai) : AttackAction(ai, "attack garr firesworn") {}

        bool Execute(Event& event) override;
        bool isUseful() override;

    protected:
        Creature* FindFiresworn();

    private:
        static constexpr uint32 FIRESWORN_ENTRY = 12099;
    };

    class RagnarosEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        RagnarosEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable ragnaros fight strategy", "+ragnaros") {}
    };

    class RagnarosDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        RagnarosDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable ragnaros fight strategy", "-ragnaros") {}
    };

    // The Sons of Flame are banishable elementals: the warlocks keep a share of
    // them out of the fight while everyone else burns the rest (the garr pattern)
    class RagnarosEnableWarlockCcAction : public ChangeAllStrategyAction
    {
    public:
        RagnarosEnableWarlockCcAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable ragnaros warlock cc", "+cc") {}
    };

    class RagnarosDisableWarlockCcAction : public ChangeAllStrategyAction
    {
    public:
        RagnarosDisableWarlockCcAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable ragnaros warlock cc", "-cc") {}
    };

    class RagnarosMoveAwayAction : public MoveAwayFromCreature
    {
    public:
        RagnarosMoveAwayAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from ragnaros", 11502, 30.0f) {}
    };

    // Run away from the flame of Might of Ragnaros (~2000 fire damage around it)
    class RagnarosMoveAwayFromFlameAction : public MoveAwayFromCreature
    {
    public:
        RagnarosMoveAwayFromFlameAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "move away from ragnaros flame", 13148, 13.0f) {}
    };

    // The main tank's pull back onto Ragnaros: he is rooted but nukes random
    // players whenever nobody stood in his melee range long enough
    class AttackRagnarosBossAction : public AttackBossEntryAction
    {
    public:
        AttackRagnarosBossAction(PlayerbotAI* ai) : AttackBossEntryAction(ai, "attack ragnaros boss", 11502) {}
    };

    class AttackRagnarosSonAction : public AttackBossAddAction
    {
    public:
        AttackRagnarosSonAction(PlayerbotAI* ai) : AttackBossAddAction(ai, "attack ragnaros son", {12143}) {}
    };

    class MoveToMCRuneAction : public MoveToAction
    {
    public:
        MoveToMCRuneAction(PlayerbotAI* ai) : MoveToAction(ai, "move to mc rune") { qualifier = "entry filter::{gos in sight,mc runes}"; }
    };

    class DouseMCRuneActionBase : public UseItemIdAction
    {
    public:
        DouseMCRuneActionBase(PlayerbotAI* ai, std::string name) : UseItemIdAction(ai, name) {}
        bool Execute(Event& event) override;

    protected:
        std::string GetTargetRuneName();
    };

    class DouseMCRuneActionAqual : public DouseMCRuneActionBase
    {
    public:
        DouseMCRuneActionAqual(PlayerbotAI* ai) : DouseMCRuneActionBase(ai, "douse mc rune aqual") { qualifier = "{17333,entry filter::{gos close,mc runes}}"; }
    };

    class DouseMCRuneActionEternal : public DouseMCRuneActionBase
    {
    public:
        DouseMCRuneActionEternal(PlayerbotAI* ai) : DouseMCRuneActionBase(ai, "douse mc rune eternal") { qualifier = "{22754,entry filter::{gos close,mc runes}}"; }
    };

    class RefreshMCQuintessenceAction : public Action
    {
    public:
        RefreshMCQuintessenceAction(PlayerbotAI* ai) : Action(ai, "refresh mc quintessence") {}
        bool Execute(Event& event) override;
        bool isPossible() override;
    };
}