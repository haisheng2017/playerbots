
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonTriggers.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

namespace
{
    constexpr uint32 MC_AQUAL_QUINTESSENCE = 17333;    // Aqual Quintessence: consumed on use, no cooldown
    constexpr uint32 MC_AQUAL_QUINTESSENCE_TARGET = 1; // Keep 1 in bags: douse once, refill one
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
