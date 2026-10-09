
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonTriggers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

using namespace ai;

namespace
{
    constexpr uint32 MC_AQUAL_QUINTESSENCE = 17333;    // Aqual Quintessence: consumed on use, no cooldown
    constexpr uint32 MC_AQUAL_QUINTESSENCE_TARGET = 1; // Keep 1 in bags: douse once, refill one

    // Baron Geddon
    constexpr uint32 GEDDON_ENTRY            = 12056;
    constexpr uint32 GEDDON_INFERNO_AURA     = 19695;  // Self aura: pulsing fire nova around the boss
    constexpr uint32 GEDDON_LIVING_BOMB     = 20475;  // Bomb aura on a random player, explodes on expiry
    constexpr float  GEDDON_INFERNO_RANGE    = 18.0f;  // Stay at least this far while Inferno is up
    constexpr float  GEDDON_ARMAGEDDON_HP   = 2.0f;   // Boss detonates Armageddon at or below this health %
    constexpr float  GEDDON_ARMAGEDDON_RANGE = 35.0f; // Run this far away from him then

    // Majordomo / Sulfuron: their adds heal each other, hitting the boss while they live is wasted
    constexpr float BOSS_ADD_TARGET_SEARCH_RANGE = 60.0f;

    // Garr: the boss with his ring of Firesworn adds
    constexpr uint32 GARR_ENTRY = 12057;
    constexpr uint32 GARR_FIRESWORN_ENTRY = 12099;

    // Closest alive creature of the given entry around the bot, or nullptr
    Creature* FindCreatureByEntryAround(Player* bot, uint32 entry, float range)
    {
        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck u_check(bot, entry, range);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, u_check);
        Cell::VisitAllObjects(bot, searcher, range);

        Creature* closest = nullptr;
        float closestDistance = range;
        for (Unit* unit : units)
        {
            Creature* creature = (Creature*)unit;
            if (!creature || !creature->IsAlive())
                continue;

            float const distance = bot->GetDistance(creature);
            if (distance <= closestDistance)
            {
                closestDistance = distance;
                closest = creature;
            }
        }

        return closest;
    }
}

bool MCQuintessenceMissingTrigger::IsActive()
{
    // Only active inside Molten Core (map 409), so manually enabling the molten core strategy elsewhere never hands out items
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMapId() != 409)
        return false;

    // Dead bots are handled by the dead engine, no refills
    if (!sServerFacade.IsAlive(bot))
        return false;

    // Count bags only (no bank), matching the HasItemCount(itemId,1) check in UseItemIdAction::isPossible
    return bot->GetItemCount(MC_AQUAL_QUINTESSENCE, false) < MC_AQUAL_QUINTESSENCE_TARGET;
}

bool BaronGeddonInfernoTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;

    // Anyone inside the Inferno radius while the aura is up has to step out of it
    Creature* geddon = FindCreatureByEntryAround(bot, GEDDON_ENTRY, GEDDON_INFERNO_RANGE);
    return geddon && geddon->HasAura(GEDDON_INFERNO_AURA);
}

bool BaronGeddonArmageddonTrigger::IsActive()
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;

    Creature* geddon = FindCreatureByEntryAround(bot, GEDDON_ENTRY, GEDDON_ARMAGEDDON_RANGE);
    return geddon && geddon->GetHealthPercent() <= GEDDON_ARMAGEDDON_HP;
}

bool BaronGeddonLivingBombTrigger::IsActive()
{
    return bot->IsInWorld() && sServerFacade.IsAlive(bot) && bot->HasAura(GEDDON_LIVING_BOMB);
}

bool BossWithAddsTargetTrigger::IsActive()
{
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return false;

    // The main tank keeps the boss; everyone else (including off-tanks) kills the adds
    if (ai->GetMainTank() == bot)
        return false;

    // Healers do not attack: their targeting stays with the healing engine
    if (ai->IsHeal(bot))
        return false;

    // While any of this encounter's adds is still breathing, they are the real targets
    bool addsAlive = false;
    for (uint32 entry : addEntries)
    {
        if (FindCreatureByEntryAround(bot, entry, BOSS_ADD_TARGET_SEARCH_RANGE))
        {
            addsAlive = true;
            break;
        }
    }

    if (!addsAlive)
        return false;

    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    if (!target)
    {
        // Empty-handed tanks normally spread over adds through "tank assist" instead;
        // only when a skull parked on the boss holds that assist back do they pick here
        if (ai->IsTank(bot))
        {
            Unit* rti = ai->GetUnit(AI_VALUE(ObjectGuid, "rti target"));
            return rti && rti->IsAlive() && rti->GetTypeId() == TYPEID_UNIT && rti->GetEntry() == bossEntry;
        }

        return true;
    }

    // A dead target leaves the bot idle while the assist actions may be held back
    if (!target->IsInWorld() || !target->IsAlive())
        return true;

    return target->GetTypeId() == TYPEID_UNIT && target->GetEntry() == bossEntry;
}

bool MainTankOffBossTrigger::IsActive()
{
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return false;

    // Only the main tank belongs on the boss during the add phase
    if (ai->GetMainTank() != bot)
        return false;

    bool addsAlive = false;
    for (uint32 entry : addEntries)
    {
        if (FindCreatureByEntryAround(bot, entry, BOSS_ADD_TARGET_SEARCH_RANGE))
        {
            addsAlive = true;
            break;
        }
    }

    if (!addsAlive)
        return false;

    // Only a living boss of this fight counts as "in place"; anything else pulls him back
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return !(target && target->IsInWorld() && target->IsAlive()
        && target->GetTypeId() == TYPEID_UNIT && target->GetEntry() == bossEntry);
}

bool GarrTargetTrigger::IsActive()
{
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return false;

    // The tanks run their own assignments (main tank on Garr, off-tanks on the Firesworn)
    // and healers do not attack at all
    if (ai->IsTank(bot) || ai->IsHeal(bot))
        return false;

    // The raid's damage stays on Garr for as long as he is up
    if (!FindCreatureByEntryAround(bot, GARR_ENTRY, BOSS_ADD_TARGET_SEARCH_RANGE))
        return false;

    // Anything but the living boss himself is a target to pull the bot back from
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return !(target && target->IsInWorld() && target->IsAlive()
        && target->GetTypeId() == TYPEID_UNIT && target->GetEntry() == GARR_ENTRY);
}

bool GarrOffTankTrigger::IsActive()
{
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return false;

    // Off-tanks only: the main tank holds Garr himself
    if (!ai->IsTank(bot) || ai->GetMainTank() == bot)
        return false;

    // Nothing to grab while no un-banished Firesworn is within reach
    bool fireswornUp = false;
    std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
    for (const ObjectGuid& guid : possibleTargets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetTypeId() == TYPEID_UNIT && unit->GetEntry() == GARR_FIRESWORN_ENTRY)
        {
            fireswornUp = true;
            break;
        }
    }

    if (!fireswornUp)
        return false;

    // Sticking with the Firesworn this off-tank is actually holding
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    if (target && target->IsInWorld() && target->IsAlive()
        && target->GetTypeId() == TYPEID_UNIT && target->GetEntry() == GARR_FIRESWORN_ENTRY
        && target->GetVictim() == bot)
        return false;

    return true;
}
