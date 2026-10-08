
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"

using namespace ai;

namespace
{
    constexpr uint32 MC_AQUAL_QUINTESSENCE = 17333;    // Aqual Quintessence: consumed on use, no cooldown
    constexpr uint32 MC_AQUAL_QUINTESSENCE_TARGET = 1; // Keep 1 in bags: douse once, refill one
}

bool RefreshMCQuintessenceAction::isPossible()
{
    return bot->IsInWorld() && !bot->IsBeingTeleported()
        && bot->GetMapId() == 409
        && bot->GetItemCount(MC_AQUAL_QUINTESSENCE, false) < MC_AQUAL_QUINTESSENCE_TARGET;
}

bool RefreshMCQuintessenceAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    if (bot->GetItemCount(MC_AQUAL_QUINTESSENCE, false) >= MC_AQUAL_QUINTESSENCE_TARGET)
        return false;

    // Refill 1: if the bags are full the action fails and the trigger retries next check (self-healing)
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, MC_AQUAL_QUINTESSENCE, 1) != EQUIP_ERR_OK)
    {
        if (requester)
        {
            ai->TellPlayerNoFacing(requester, "My bags are full, cannot carry Aqual Quintessence for dousing.",
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, true); // noRepeat prevents spam
        }
        return false;
    }

    Item* item = bot->StoreNewItem(dest, MC_AQUAL_QUINTESSENCE, true);
    if (!item)
        return false;

    bot->SendNewItem(item, 1, true, false);
    return true;
}

std::string DouseMCRuneActionBase::GetTargetRuneName()
{
    // Parse the qualifier the same way UseItemIdAction::Execute does, to get the target rune name
    std::vector<std::string> params = getMultiQualifiers(getQualifier(), ",");
    if (params.size() < 2)
        return "";

    std::list<GuidPosition> guidPs = AI_VALUE(std::list<GuidPosition>, params[1]);
    if (guidPs.empty())
        return "";

    GuidPosition guidP = *guidPs.begin();
    if (!guidP.IsGameObject())
        return "";

    GameObject* go = guidP.GetGameObject(bot->GetInstanceId());
    if (!go || !go->GetGOInfo())
        return "";

    return go->GetGOInfo()->name;
}

bool DouseMCRuneActionBase::Execute(Event& event)
{
    // The rune GO leaves the usable list once doused, so record the target rune name first
    std::string runeName = GetTargetRuneName();

    bool result = UseItemIdAction::Execute(event);

    if (result)
    {
        if (runeName.empty())
            ai->Say("I have doused the fire.");
        else
            ai->Say("I have doused the fire [" + runeName + "]");
    }

    return result;
}
