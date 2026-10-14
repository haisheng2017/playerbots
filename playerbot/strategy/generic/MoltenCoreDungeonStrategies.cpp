
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonStrategies.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/Action.h"

using namespace ai;

namespace
{
    // While this strategy is on, generic assist would peel the raid off the kill target
    // or walk an off-tank onto a sheep. Shields are handled by the action names they punish.
    class MajordomoFightMultiplier : public Multiplier
    {
    public:
        MajordomoFightMultiplier(PlayerbotAI* ai) : Multiplier(ai, "majordomo fight") {}

        float GetValue(ai::Action* action) override
        {
            if (!action)
                return 1.0f;

            const std::string& name = action->getName();
            if (!ai->IsTank(bot) && !ai->IsHeal(bot) && (name == "dps assist" || name == "dps aoe"))
                return 0.0f;

            // AoE on the pack breaks polymorph. Heals are also tagged as area threat, so healers stay exempt.
            if (!ai->IsHeal(bot) && action->getThreatType() == ActionThreatType::ACTION_THREAT_AOE)
                return 0.0f;

            if (ai->IsTank(bot) && ai->GetMainTank() != bot && name == "tank assist")
                return 0.0f;

            // The main tank's taunt is only for Majordomo, via "majordomo main tank taunt".
            if (ai->GetMainTank() == bot &&
                (name == "taunt" || name == "mocking blow" || name == "growl" ||
                 name == "hand of reckoning" || name == "righteous defense"))
                return 0.0f;

            Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
            if (!target || !target->IsAlive())
                return 1.0f;

            if (target->GetEntry() != 11663 && target->GetEntry() != 11664)
                return 1.0f;

            const bool aimedAtCurrent = action->GetTargetName() == "current target";
            if (target->HasAura(20619))
            {
                // Magic Reflection bounces spells. White melee is not a spell and keeps swinging.
                if (name == "shoot" || name == "pet attack" || name == "reach spell")
                    return 0.0f;

                if (aimedAtCurrent && action->getThreatType() != ActionThreatType::ACTION_THREAT_NONE)
                    return 0.0f;
            }

            if (target->HasAura(21075))
            {
                if (name == "pet attack")
                    return 0.0f;

                if (ai->IsMelee(bot) && (name == "melee" || name == "reach melee"))
                    return 0.0f;

                if (ai->IsMelee(bot) && aimedAtCurrent && action->getThreatType() != ActionThreatType::ACTION_THREAT_NONE)
                    return 0.0f;
            }

            return 1.0f;
        }
    };
}

void MoltenCoreDungeonStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "majordomo lava",
        NextAction::array(0, new NextAction("majordomo leave lava", 110.0f), NULL)));
}

void MoltenCoreDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "majordomo lava",
        NextAction::array(0, new NextAction("majordomo leave lava", 110.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start magmadar fight",
        NextAction::array(0, new NextAction("enable magmadar fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start baron geddon fight",
        NextAction::array(0, new NextAction("enable baron geddon fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start shazzrah fight",
        NextAction::array(0, new NextAction("enable shazzrah fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start sulfuron fight",
        NextAction::array(0, new NextAction("enable sulfuron fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start majordomo fight",
        NextAction::array(0, new NextAction("enable majordomo fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start garr fight",
        NextAction::array(0,
            new NextAction("enable garr fight strategy", 100.0f),
            new NextAction("enable garr warlock cc", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start ragnaros fight",
        NextAction::array(0,
            new NextAction("enable ragnaros fight strategy", 100.0f),
            new NextAction("enable ragnaros warlock cc", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mc rune doused",
        NextAction::array(0, new NextAction("say mc rune doused", 20.0f), NULL)));
}

void MoltenCoreDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    /*
    triggers.push_back(new TriggerNode(
        "val::and::{"
        "action possible::use id::17333,"
        "has object::go usable filter::go trapped filter::entry filter::{gos in sight,mc runes},"
        "not::has object::entry filter::{gos close,mc runes}"
        "}",
        NextAction::array(0, new NextAction("move to::entry filter::{gos in sight,mc runes}", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "val::has object::go usable filter::entry filter::{gos close,mc runes}",
        NextAction::array(0, new NextAction("use id::{17333,entry filter::{gos close,mc runes}}", 1.0f), NULL)));
        */

    triggers.push_back(new TriggerNode(
        "majordomo lava",
        NextAction::array(0, new NextAction("majordomo leave lava", 110.0f), NULL)));

    // Dousing item refill: keep 1 Aqual Quintessence in the bot's bags (douse once, refill one)
    triggers.push_back(new TriggerNode(
        "mc quintessence missing",
        NextAction::array(0, new NextAction("refresh mc quintessence", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mc rune in sight",
        NextAction::array(0, new NextAction("move to mc rune", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mc rune close",
        NextAction::array(0,
            new NextAction("douse mc rune eternal", 2.0f),
            new NextAction("douse mc rune aqual", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mc rune doused",
        NextAction::array(0, new NextAction("say mc rune doused", 20.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "magmadar too close",
            NextAction::array(0, new NextAction("move away from magmadar", 100.0f), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end magmadar fight",
        NextAction::array(0, new NextAction("disable magmadar fight strategy", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end magmadar fight",
        NextAction::array(0, new NextAction("disable magmadar fight strategy", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "magmadar lava bomb",
        NextAction::array(0, new NextAction("move away from hazard", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        multipliers.push_back(new PreventMoveAwayFromCreatureOnReachToCastMultiplier(ai));
    }
}

void BaronGeddonFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Inferno: the boss pulses a fire nova around himself while the aura is up
    triggers.push_back(new TriggerNode(
        "baron geddon inferno",
        NextAction::array(0, new NextAction("move away from baron geddon inferno", 100.0f), NULL)));

    // Armageddon: at 2% health the boss detonates, everyone runs away from him
    triggers.push_back(new TriggerNode(
        "baron geddon armageddon",
        NextAction::array(0, new NextAction("move away from baron geddon armageddon", 100.0f), NULL)));
}

void BaronGeddonFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end baron geddon fight",
        NextAction::array(0, new NextAction("disable baron geddon fight strategy", 100.0f), NULL)));
}

void BaronGeddonFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end baron geddon fight",
        NextAction::array(0, new NextAction("disable baron geddon fight strategy", 100.0f), NULL)));
}

void BaronGeddonFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    // Living Bomb: the carrier runs away from the raid until the bomb goes off
    triggers.push_back(new TriggerNode(
        "baron geddon living bomb",
        NextAction::array(0, new NextAction("baron geddon living bomb escape", 100.0f), NULL)));
}

void ShazzrahFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Ranged and healers stay 31 yd out to avoid the periodic Arcane Explosion (~30 yd),
    // and to get knocked out again when Gate of Shazzrah teleports the boss next to them
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "shazzrah too close",
            NextAction::array(0, new NextAction("move away from shazzrah", 100.0f), NULL)));
    }
}

void ShazzrahFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end shazzrah fight",
        NextAction::array(0, new NextAction("disable shazzrah fight strategy", 100.0f), NULL)));
}

void ShazzrahFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end shazzrah fight",
        NextAction::array(0, new NextAction("disable shazzrah fight strategy", 100.0f), NULL)));
}

void ShazzrahFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        multipliers.push_back(new PreventMoveAwayFromCreatureOnReachToCastMultiplier(ai));
    }
}

void SulfuronFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // The Flamewaker priests heal each other and the boss: everyone except the
    // main tank sticks to them until they drop (the main tank holds the boss)
    triggers.push_back(new TriggerNode(
        "sulfuron target",
        NextAction::array(0, new NextAction("attack sulfuron add", 100.0f), NULL)));

    // Pull the main tank back onto the boss after a one-shot player command (or a bad pull)
    triggers.push_back(new TriggerNode(
        "sulfuron main tank off boss",
        NextAction::array(0, new NextAction("attack sulfuron boss", 100.0f), NULL)));
}

void SulfuronFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    // Never let "tank assist" pull the main tank off the boss while his priests are alive
    multipliers.push_back(new KeepMainTankOnBossMultiplier(ai, {11662}));

    // Hold the assists back only while a skull parked on the boss would yank the raid off the priests
    multipliers.push_back(new BossRtiAssistMultiplier(ai, 12098, {11662}));
}

void SulfuronFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end sulfuron fight",
        NextAction::array(0, new NextAction("disable sulfuron fight strategy", 100.0f), NULL)));
}

void SulfuronFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end sulfuron fight",
        NextAction::array(0, new NextAction("disable sulfuron fight strategy", 100.0f), NULL)));
}

void MajordomoFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Majordomo cannot die. Ranged and healers step out of his melee; the fight is won by killing adds.
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "majordomo too close",
            NextAction::array(0, new NextAction("move away from majordomo", 100.0f), NULL)));
    }

    // One sheep per mage, on a healer the raid is not killing. Outranks the kill-target swap.
    if (bot->getClass() == CLASS_MAGE)
    {
        triggers.push_back(new TriggerNode(
            "majordomo polymorph healer",
            NextAction::array(0, new NextAction("polymorph majordomo healer", 101.0f), NULL)));
    }

    // Damage dealers stack on one add: skull, else the elite closest to Majordomo, else a healer.
    triggers.push_back(new TriggerNode(
        "majordomo target",
        NextAction::array(0, new NextAction("attack majordomo add", 100.0f), NULL)));

    // Taunt Majordomo whenever he is not on the main tank, even if another tank is holding him.
    triggers.push_back(new TriggerNode(
        "majordomo main tank taunt",
        NextAction::array(0, new NextAction("majordomo main tank taunt", 104.0f), NULL)));

    // One off-tank holds the boss while the main tank is teleported or has lost him.
    triggers.push_back(new TriggerNode(
        "majordomo cover boss",
        NextAction::array(0, new NextAction("attack majordomo cover", 102.0f), NULL)));

    // Off-tanks each hold one add, including the skull, so nothing is left on a non-tank.
    triggers.push_back(new TriggerNode(
        "majordomo off tank",
        NextAction::array(0, new NextAction("attack majordomo elite", 100.0f), NULL)));

    // Pull the main tank back onto Majordomo after a teleport, a threat wipe, or a bad swap.
    triggers.push_back(new TriggerNode(
        "majordomo main tank off boss",
        NextAction::array(0, new NextAction("attack majordomo boss", 100.0f), NULL)));

    // Returns false, so heals and the move-away above still run on the same tick.
    if (!ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "majordomo shield",
            NextAction::array(0, new NextAction("majordomo shield hold", 95.0f), NULL)));
    }
}

void MajordomoFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end majordomo fight",
        NextAction::array(0, new NextAction("disable majordomo fight strategy", 100.0f), NULL)));
}

void MajordomoFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end majordomo fight",
        NextAction::array(0, new NextAction("disable majordomo fight strategy", 100.0f), NULL)));
}

void MajordomoFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "majordomo lava",
        NextAction::array(0, new NextAction("majordomo leave lava", 110.0f), NULL)));
}

void MajordomoFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        multipliers.push_back(new PreventMoveAwayFromCreatureOnReachToCastMultiplier(ai));
    }

    // Never let "tank assist" pull the main tank off Majordomo while his adds are alive
    multipliers.push_back(new KeepMainTankOnBossMultiplier(ai, {11663, 11664}));

    // Hold the assists back only while a skull parked on the boss would yank the raid off the adds
    multipliers.push_back(new BossRtiAssistMultiplier(ai, 12018, {11663, 11664}));

    // Kill order, off-tank assignments, and the two shields
    multipliers.push_back(new MajordomoFightMultiplier(ai));
}

void GarrFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // The raid's damage stays on Garr while he lives: the Firesworn get held by the
    // off-tanks and banished by the warlocks - killing one only erupts it (19497)
    // and enrages the boss (19515->19516)
    triggers.push_back(new TriggerNode(
        "garr target",
        NextAction::array(0, new NextAction("attack garr", 100.0f), NULL)));

    // Pull the main tank back onto Garr after a one-shot player command (or a bad pull)
    triggers.push_back(new TriggerNode(
        "garr main tank off boss",
        NextAction::array(0, new NextAction("attack garr boss", 100.0f), NULL)));

    // The off-tanks hold the Firesworn the warlocks did not banish
    triggers.push_back(new TriggerNode(
        "garr off tank",
        NextAction::array(0, new NextAction("attack garr firesworn", 100.0f), NULL)));
}

void GarrFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end garr fight",
        NextAction::array(0,
            new NextAction("disable garr fight strategy", 100.0f),
            new NextAction("disable garr warlock cc", 100.0f), NULL)));
}

void GarrFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end garr fight",
        NextAction::array(0,
            new NextAction("disable garr fight strategy", 100.0f),
            new NextAction("disable garr warlock cc", 100.0f), NULL)));
}

void GarrFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    // Never let "tank assist" pull the main tank off Garr while his Firesworn are alive
    multipliers.push_back(new KeepMainTankOnBossMultiplier(ai, {12099}));

    // While boss and Firesworn are both up the fight's assignments own the targeting:
    // off-tanks stay on their adds and the raid stays on the boss
    multipliers.push_back(new KeepRaidOnBossMultiplier(ai, 12057, {12099}));
}

void RagnarosFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Lava patches and the Might of Ragnaros flame: step along the ring.
    triggers.push_back(new TriggerNode(
        "ragnaros lava burst",
        NextAction::array(0, new NextAction("ragnaros step aside", 110.0f), NULL)));
    triggers.push_back(new TriggerNode(
        "ragnaros flame near",
        NextAction::array(0, new NextAction("ragnaros step aside", 110.0f), NULL)));

    // Sons of Flame, including the main tank once Ragnaros has submerged.
    // Straight line on the platform, above the ring so a tank leaves the slot to pick one up.
    triggers.push_back(new TriggerNode(
        "ragnaros son target",
        NextAction::array(0, new NextAction("attack ragnaros son", 104.0f), NULL)));

    // Spread around the boss on the floor. Rocks teleport back down to the slot.
    triggers.push_back(new TriggerNode(
        "ragnaros spread",
        NextAction::array(0, new NextAction("ragnaros spread", 102.0f), NULL)));

    // Magma Blast: nobody in melee range means random players eat ~6k fire every
    // 2.5s - pull the main tank back whenever he slipped off (e.g. knocked away)
    triggers.push_back(new TriggerNode(
        "ragnaros main tank off boss",
        NextAction::array(0, new NextAction("attack ragnaros boss", 100.0f), NULL)));
}

void RagnarosFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end ragnaros fight",
        NextAction::array(0,
            new NextAction("disable ragnaros fight strategy", 100.0f),
            new NextAction("disable ragnaros warlock cc", 100.0f), NULL)));
}

void RagnarosFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end ragnaros fight",
        NextAction::array(0,
            new NextAction("disable ragnaros fight strategy", 100.0f),
            new NextAction("disable ragnaros warlock cc", 100.0f), NULL)));
}

void RagnarosFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    // Lava Burst and the flame: step along the ring, same as in combat.
    triggers.push_back(new TriggerNode(
        "ragnaros lava burst",
        NextAction::array(0, new NextAction("ragnaros step aside", 110.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "ragnaros flame near",
        NextAction::array(0, new NextAction("ragnaros step aside", 110.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "ragnaros spread",
        NextAction::array(0, new NextAction("ragnaros spread", 102.0f), NULL)));
}

namespace
{
    // Combat chase in this room is the ring and the straight-line son/boss walk.
    // Generic reach would search the rock navmesh every tick.
    class RagnarosPlatformMultiplier : public Multiplier
    {
    public:
        RagnarosPlatformMultiplier(PlayerbotAI* ai) : Multiplier(ai, "ragnaros platform") {}

        float GetValue(ai::Action* action) override
        {
            if (!action)
                return 1.0f;

            const std::string& name = action->getName();
            // Standing on the ring is a teleport. Walking from here steps into the lava
            // and keeps issuing a new spline every tick.
            if (name == "reach melee" || name == "reach spell" || name == "follow" || name == "flee" ||
                name == "move away from ragnaros" || name == "move away from ragnaros flame" ||
                name == "move away from hazard")
                return 0.0f;

            return 1.0f;
        }
    };
}

void RagnarosFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new RagnarosPlatformMultiplier(ai));
}
