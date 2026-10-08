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