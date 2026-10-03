#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonTriggers.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"

using namespace ai;

bool OnyxiaDeepBreathTrigger::IsActive()
{
    float x, y, z;
    return GetOnyxiaBreathDodgePoint(bot, x, y, z);
}

bool OnyxiaOutOfLairTrigger::IsActive()
{
    float x, y, z;
    return GetOnyxiaLairReturnPoint(bot, x, y, z);
}

bool OnyxiaWhelpTrigger::IsActive()
{
    if (!ShouldAttackOnyxiaWhelp(ai))
        return false;

    Unit* whelp = FindOnyxiaWhelpInRoom(bot);
    Unit* current = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return whelp && current != whelp;
}

bool OnyxiaTankSpotTrigger::IsActive()
{
    float x, y, z;
    return GetOnyxiaTankPoint(ai, x, y, z);
}

bool OnyxiaHoldDpsTrigger::IsActive()
{
    return ShouldHoldOnyxiaDps(ai);
}

bool OnyxiaWhelpLureTrigger::IsActive()
{
    float x, y, z;
    return GetOnyxiaWhelpLurePoint(ai, x, y, z);
}

bool OnyxiaPositionTrigger::IsActive()
{
    float x, y, z;
    return GetOnyxiaPositionPoint(ai, x, y, z);
}

bool OnyxiaFearWardTrigger::IsActive()
{
    if (bot->getClass() != CLASS_PRIEST)
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || GetOnyxiaPhase(onyxia) != 3)
        return false;

    return FindOnyxiaFearWardTarget(ai) != nullptr;
}

bool OnyxiaTremorTotemTrigger::IsActive()
{
    if (bot->getClass() != CLASS_SHAMAN || !ai->HasSpell("tremor totem"))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || GetOnyxiaPhase(onyxia) != 3)
        return false;

    return !AI_VALUE2(bool, "has totem", "tremor totem");
}

bool OnyxiaMainTankTauntTrigger::IsActive()
{
    return ShouldOnyxiaMainTankTaunt(ai);
}
