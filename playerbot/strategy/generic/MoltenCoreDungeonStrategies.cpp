
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonStrategies.h"
#include "DungeonMultipliers.h"

using namespace ai;

void MoltenCoreDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
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
    // Majordomo cannot die (death prevention) and periodically teleports players next to him:
    // non-tanks pushed back out to 16 yd, the encounter is won by killing the Flamewaker adds
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "majordomo too close",
            NextAction::array(0, new NextAction("move away from majordomo", 100.0f), NULL)));
    }

    // Everyone except the main tank stays off the boss himself while his adds are up
    // (the main tank holds Majordomo in place; he cannot be killed at all)
    triggers.push_back(new TriggerNode(
        "majordomo target",
        NextAction::array(0, new NextAction("attack majordomo add", 100.0f), NULL)));

    // Pull the main tank back onto Majordomo after a one-shot player command (or a bad pull)
    triggers.push_back(new TriggerNode(
        "majordomo main tank off boss",
        NextAction::array(0, new NextAction("attack majordomo boss", 100.0f), NULL)));
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
}
