#pragma once
#include "DungeonTriggers.h"
#include "GenericTriggers.h"

namespace ai
{
    class MoltenCoreEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        MoltenCoreEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter molten core", "molten core", 409) {}
    };

    class MoltenCoreLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        MoltenCoreLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave molten core", "molten core", 409) {}
    };

    class MagmadarStartFightTrigger : public StartBossFightTrigger
    {
    public:
        MagmadarStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start magmadar fight", "magmadar", 11982) {}
    };

    class MagmadarEndFightTrigger : public EndBossFightTrigger
    {
    public:
        MagmadarEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end magmadar fight", "magmadar", 11982) {}
    };

    class MagmadarLavaBombTrigger : public CloseToGameObjectHazardTrigger
    {
    public:
        MagmadarLavaBombTrigger(PlayerbotAI* ai) : CloseToGameObjectHazardTrigger(ai, "magmadar lava bomb", 177704, 5.0f, 60) {}
    };

    class MagmadarTooCloseTrigger : public CloseToCreatureTrigger
    {
    public:
        MagmadarTooCloseTrigger(PlayerbotAI* ai) : CloseToCreatureTrigger(ai, "magmadar too close", 11982, 30.0f) {}
    };

    class BaronGeddonStartFightTrigger : public StartBossFightTrigger
    {
    public:
        BaronGeddonStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start baron geddon fight", "baron geddon", 12056) {}
    };

    class BaronGeddonEndFightTrigger : public EndBossFightTrigger
    {
    public:
        BaronGeddonEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end baron geddon fight", "baron geddon", 12056) {}
    };

    class ShazzrahStartFightTrigger : public StartBossFightTrigger
    {
    public:
        ShazzrahStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start shazzrah fight", "shazzrah", 12264) {}
    };

    class ShazzrahEndFightTrigger : public EndBossFightTrigger
    {
    public:
        ShazzrahEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end shazzrah fight", "shazzrah", 12264) {}
    };

    class ShazzrahTooCloseTrigger : public CloseToCreatureTrigger
    {
    public:
        ShazzrahTooCloseTrigger(PlayerbotAI* ai) : CloseToCreatureTrigger(ai, "shazzrah too close", 12264, 30.0f) {}
    };

    class SulfuronStartFightTrigger : public StartBossFightTrigger
    {
    public:
        SulfuronStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start sulfuron fight", "sulfuron", 12098) {}
    };

    class SulfuronEndFightTrigger : public EndBossFightTrigger
    {
    public:
        SulfuronEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end sulfuron fight", "sulfuron", 12098) {}
    };

    class MajordomoStartFightTrigger : public StartBossFightTrigger
    {
    public:
        MajordomoStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start majordomo fight", "majordomo", 12018) {}
    };

    class MajordomoEndFightTrigger : public EndBossFightTrigger
    {
    public:
        MajordomoEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end majordomo fight", "majordomo", 12018) {}
    };

    class MajordomoTooCloseTrigger : public CloseToCreatureTrigger
    {
    public:
        MajordomoTooCloseTrigger(PlayerbotAI* ai) : CloseToCreatureTrigger(ai, "majordomo too close", 12018, 15.0f) {}
    };

    // Baron Geddon (12056): while the Inferno aura is up he pulses a fire nova around himself
    class BaronGeddonInfernoTrigger : public Trigger
    {
    public:
        BaronGeddonInfernoTrigger(PlayerbotAI* ai) : Trigger(ai, "baron geddon inferno", 1) {}
        bool IsActive() override;
    };

    // Baron Geddon detonates Armageddon at 2% health: everyone run away from him
    class BaronGeddonArmageddonTrigger : public Trigger
    {
    public:
        BaronGeddonArmageddonTrigger(PlayerbotAI* ai) : Trigger(ai, "baron geddon armageddon", 1) {}
        bool IsActive() override;
    };

    // Baron Geddon: this bot carries Living Bomb and explodes unless it stays away from others
    class BaronGeddonLivingBombTrigger : public Trigger
    {
    public:
        BaronGeddonLivingBombTrigger(PlayerbotAI* ai) : Trigger(ai, "baron geddon living bomb", 1) {}
        bool IsActive() override;
    };

    // True while a non-tank keeps a boss as its target even though the encounter's
    // real targets (his healing/buffing adds) are still up: Majordomo and Sulfuron
    class BossWithAddsTargetTrigger : public Trigger
    {
    public:
        BossWithAddsTargetTrigger(PlayerbotAI* ai, std::string name, uint32 bossEntry, std::vector<uint32> addEntries)
            : Trigger(ai, name, 1), bossEntry(bossEntry), addEntries(addEntries) {}
        bool IsActive() override;

    private:
        uint32 bossEntry;
        std::vector<uint32> addEntries;
    };

    // Majordomo cannot be killed at all: the fight is won when his adds die
    class MajordomoTargetTrigger : public BossWithAddsTargetTrigger
    {
    public:
        MajordomoTargetTrigger(PlayerbotAI* ai) : BossWithAddsTargetTrigger(ai, "majordomo target", 12018, {11663, 11664}) {}
    };

    // Sulfuron's Flamewaker priests heal each other and the boss: they must die first
    class SulfuronTargetTrigger : public BossWithAddsTargetTrigger
    {
    public:
        SulfuronTargetTrigger(PlayerbotAI* ai) : BossWithAddsTargetTrigger(ai, "sulfuron target", 12098, {11662}) {}
    };

    // The pull-back for the main tank: while the adds are up he belongs on the boss,
    // even when a one-shot player command (attack my target) or the pull put him on an add
    class MainTankOffBossTrigger : public Trigger
    {
    public:
        MainTankOffBossTrigger(PlayerbotAI* ai, std::string name, uint32 bossEntry, std::vector<uint32> addEntries)
            : Trigger(ai, name, 1), bossEntry(bossEntry), addEntries(addEntries) {}
        bool IsActive() override;

    private:
        uint32 bossEntry;
        std::vector<uint32> addEntries;
    };

    class SulfuronMainTankOffBossTrigger : public MainTankOffBossTrigger
    {
    public:
        SulfuronMainTankOffBossTrigger(PlayerbotAI* ai) : MainTankOffBossTrigger(ai, "sulfuron main tank off boss", 12098, {11662}) {}
    };

    class MajordomoMainTankOffBossTrigger : public MainTankOffBossTrigger
    {
    public:
        MajordomoMainTankOffBossTrigger(PlayerbotAI* ai) : MainTankOffBossTrigger(ai, "majordomo main tank off boss", 12018, {11663, 11664}) {}
    };

    class GarrStartFightTrigger : public StartBossFightTrigger
    {
    public:
        GarrStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start garr fight", "garr", 12057) {}
    };

    class GarrEndFightTrigger : public EndBossFightTrigger
    {
    public:
        GarrEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end garr fight", "garr", 12057) {}
    };

    // Garr (12057): the Firesworn (12099) are held by the off-tanks and banished by the
    // warlocks instead of being killed - killing one only erupts it (19497) and enrages
    // the boss (19515->19516), so everyone else keeps their damage on Garr himself
    class GarrTargetTrigger : public Trigger
    {
    public:
        GarrTargetTrigger(PlayerbotAI* ai) : Trigger(ai, "garr target", 1) {}
        bool IsActive() override;
    };

    // An off-tank currently holding none of the living Firesworn while some are still up
    class GarrOffTankTrigger : public Trigger
    {
    public:
        GarrOffTankTrigger(PlayerbotAI* ai) : Trigger(ai, "garr off tank", 1) {}
        bool IsActive() override;
    };

    // Firesworn alive: pull the main tank back onto Garr after a one-shot player command (or a bad pull)
    class GarrMainTankOffBossTrigger : public MainTankOffBossTrigger
    {
    public:
        GarrMainTankOffBossTrigger(PlayerbotAI* ai) : MainTankOffBossTrigger(ai, "garr main tank off boss", 12057, {12099}) {}
    };

    class FireProtectionPotionReadyTrigger : public ItemBuffReadyTrigger
    {
    public:
        FireProtectionPotionReadyTrigger(PlayerbotAI* ai) : ItemBuffReadyTrigger(ai, "fire protection potion ready", 13457, 17543) {}
    };

    class MCRuneInSightTrigger : public ValueTrigger
    {
    public:
        MCRuneInSightTrigger(PlayerbotAI* ai) : ValueTrigger(ai, "mc rune in sight", 1)
        {
            qualifier = "and::{"
                "or::{action possible::use id::17333,action possible::use id::22754},"
                "has object::go usable filter::go trapped filter::entry filter::{gos in sight,mc runes},"
                "not::has object::entry filter::{gos close,mc runes}"
                "}";
        }
    };

    class MCRuneCloseTrigger : public ValueTrigger
    {
    public:
        MCRuneCloseTrigger(PlayerbotAI* ai) : ValueTrigger(ai, "mc rune close", 1) { qualifier = "has object::go usable filter::entry filter::{gos close,mc runes}"; }
    };

    class MCQuintessenceMissingTrigger : public Trigger
    {
    public:
        // Check interval of 60 s throttles the refill that would otherwise re-arm the
        // douse loop every tick next to an already-doused rune (the GO never goes away)
        MCQuintessenceMissingTrigger(PlayerbotAI* ai) : Trigger(ai, "mc quintessence missing", 60) {}
        bool IsActive() override;
    };
}