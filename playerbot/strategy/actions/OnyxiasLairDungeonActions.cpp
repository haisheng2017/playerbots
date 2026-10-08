#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonActions.h"
#include "UseItemAction.h"
#include "playerbot/WorldPosition.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include "MotionGenerators/MotionMaster.h"

#include <ctime>

using namespace ai;

namespace
{
    const uint32 NPC_ONYXIA = 10184;
    const uint32 NPC_ONYXIA_WHELP = 11262;
    const uint32 SPELL_DRAGON_HOVER = 18430;
    const uint32 SPELL_PACIFY_SELF = 19951;
    const uint32 SPELL_BREATH_ENTRANCE = 21131;

    const float LAIR_CENTER_X = -27.0481f;
    const float LAIR_CENTER_Y = -219.097f;
    const float LAIR_CENTER_Z = -89.2858f;
    const float LAIR_SAFE_RADIUS = 68.0f;
    const float LAIR_EDGE_RADIUS = 70.0f;
    // Floor of the crater is near -89. The entrance ramp climbs above this.
    const float LAIR_RAMP_Z = -78.0f;

    struct BreathCorridor
    {
        uint32 spellA;
        uint32 spellB;
        float x1, y1, x2, y2;
    };

    // Endpoints match the breath spell target chains in boss_onyxia.cpp.
    // North/south continues into the entrance tunnel.
    const BreathCorridor breathCorridors[] =
    {
        {17086, 18351,  20.73f, -215.24f, -130.79f, -213.42f},
        {18576, 18609, -37.74f, -243.67f,  -37.73f, -188.62f},
        {18564, 18584, -56.56f, -241.22f,    6.02f, -181.31f},
        {18596, 18617, -58.25f, -189.02f,   12.12f, -243.44f},
    };

    float Distance2d(float x1, float y1, float x2, float y2)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        return sqrt(dx * dx + dy * dy);
    }

    float DistanceToSegment(float px, float py, float x1, float y1, float x2, float y2)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len2 = dx * dx + dy * dy;
        if (len2 < 0.01f)
            return Distance2d(px, py, x1, y1);

        float t = ((px - x1) * dx + (py - y1) * dy) / len2;
        if (t < 0.0f)
            t = 0.0f;
        else if (t > 1.0f)
            t = 1.0f;

        return Distance2d(px, py, x1 + t * dx, y1 + t * dy);
    }

    void ClampToLair(float& x, float& y)
    {
        float dx = x - LAIR_CENTER_X;
        float dy = y - LAIR_CENTER_Y;
        float dist = sqrt(dx * dx + dy * dy);
        if (dist > LAIR_SAFE_RADIUS)
        {
            float scale = LAIR_SAFE_RADIUS / dist;
            x = LAIR_CENTER_X + dx * scale;
            y = LAIR_CENTER_Y + dy * scale;
        }
    }

    float LairFloorZ(Player* bot, float x, float y)
    {
        WorldPosition point(bot->GetMapId(), x, y, bot->GetPositionZ());
        float z = point.getHeight();
        if (z == 0.0f || z > LAIR_RAMP_Z || z < -120.0f)
            z = LAIR_CENTER_Z;
        return z;
    }

    Unit* FindCreature(Player* bot, uint32 entry, float range)
    {
        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, entry, range);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(bot, searcher, range);

        Unit* closest = nullptr;
        float best = range;
        for (Unit* unit : units)
        {
            if (!unit || !unit->IsAlive())
                continue;

            float distance = bot->GetDistance(unit);
            if (distance < best)
            {
                best = distance;
                closest = unit;
            }
        }

        return closest;
    }

    uint32 CurrentOnyxiaSpell(Unit* onyxia)
    {
        if (!onyxia)
            return 0;

        Spell* spells[] =
        {
            onyxia->GetCurrentSpell(CURRENT_GENERIC_SPELL),
            onyxia->GetCurrentSpell(CURRENT_CHANNELED_SPELL),
            onyxia->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)
        };

        for (Spell* spell : spells)
        {
            if (!spell || !spell->m_spellInfo || spell->getState() == SPELL_STATE_FINISHED)
                continue;

            return spell->m_spellInfo->Id;
        }

        return 0;
    }

    bool BreathLineForSpell(uint32 spellId, float& x1, float& y1, float& x2, float& y2)
    {
        for (const BreathCorridor& corridor : breathCorridors)
        {
            if (spellId == corridor.spellA || spellId == corridor.spellB)
            {
                x1 = corridor.x1;
                y1 = corridor.y1;
                x2 = corridor.x2;
                y2 = corridor.y2;
                return true;
            }
        }

        return false;
    }

    struct BreathFlight
    {
        float x1, y1, x2, y2;
    };

    // Hover points she actually flies between. Same order as the damage corridors above.
    const BreathFlight breathFlights[] =
    {
        { 24.16332f, -216.0808f, -66.3589f, -215.928f},
        {-15.00505f, -244.4841f, -16.70134f, -181.4501f},
        {-63.5156f,  -240.096f,   12.26687f, -181.1084f},
        { 10.2191f,  -247.912f,  -58.2509f,  -189.020f},
    };

    // Entrance tunnel is the west side. 12 o'clock is further east, about 40 yards from the center.
    const float TANK_12_X = 13.0f;
    const float TANK_12_Y = -217.0f;

    struct LatchedBreath
    {
        uint32 instanceId;
        time_t until;
        float x1, y1, x2, y2;
    };

    LatchedBreath latchedBreaths[8] = {};

    float SegmentParameter(float px, float py, float x1, float y1, float x2, float y2)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len2 = dx * dx + dy * dy;
        if (len2 < 0.01f)
            return 0.0f;

        return ((px - x1) * dx + (py - y1) * dy) / len2;
    }

    void RememberBreath(uint32 instanceId, float x1, float y1, float x2, float y2)
    {
        time_t now = time(nullptr);
        LatchedBreath* slot = nullptr;
        for (LatchedBreath& breath : latchedBreaths)
        {
            if (breath.instanceId == instanceId)
            {
                slot = &breath;
                break;
            }

            if (!slot && breath.until < now)
                slot = &breath;
        }

        if (!slot)
            slot = &latchedBreaths[0];

        slot->instanceId = instanceId;
        slot->until = now + 8;
        slot->x1 = x1;
        slot->y1 = y1;
        slot->x2 = x2;
        slot->y2 = y2;
    }

    bool ActiveBreath(uint32 instanceId, float& x1, float& y1, float& x2, float& y2)
    {
        time_t now = time(nullptr);
        for (const LatchedBreath& breath : latchedBreaths)
        {
            if (breath.instanceId != instanceId || breath.until < now)
                continue;

            x1 = breath.x1;
            y1 = breath.y1;
            x2 = breath.x2;
            y2 = breath.y2;
            return true;
        }

        return false;
    }

    bool AirborneBreathLine(Unit* onyxia, float& x1, float& y1, float& x2, float& y2)
    {
        if (!onyxia->HasAura(SPELL_DRAGON_HOVER) && !onyxia->HasAura(SPELL_PACIFY_SELF))
            return false;

        float ox = onyxia->GetPositionX();
        float oy = onyxia->GetPositionY();
        float destX = 0.0f;
        float destY = 0.0f;
        float destZ = 0.0f;
        bool moving = onyxia->GetMotionMaster() && onyxia->GetMotionMaster()->GetDestination(destX, destY, destZ);
        float travel = moving ? Distance2d(ox, oy, destX, destY) : 0.0f;

        for (size_t i = 0; i < sizeof(breathFlights) / sizeof(breathFlights[0]); ++i)
        {
            const BreathFlight& flight = breathFlights[i];
            const BreathCorridor& damage = breathCorridors[i];
            float dist = DistanceToSegment(ox, oy, flight.x1, flight.y1, flight.x2, flight.y2);
            float t = SegmentParameter(ox, oy, flight.x1, flight.y1, flight.x2, flight.y2);
            // A rim hover sits on an endpoint. The breath is the middle of that same line.
            bool crossing = dist <= 18.0f && t > 0.15f && t < 0.85f;
            bool flyingAcross = false;
            if (moving && travel > 30.0f)
            {
                float destDist = DistanceToSegment(destX, destY, flight.x1, flight.y1, flight.x2, flight.y2);
                flyingAcross = dist <= 18.0f && destDist <= 18.0f;
            }

            if (!crossing && !flyingAcross)
                continue;

            x1 = damage.x1;
            y1 = damage.y1;
            x2 = damage.x2;
            y2 = damage.y2;
            return true;
        }

        return false;
    }

    bool UpdateBreathLatch(Unit* onyxia, float& x1, float& y1, float& x2, float& y2)
    {
        float lineX1, lineY1, lineX2, lineY2;
        uint32 spellId = CurrentOnyxiaSpell(onyxia);
        if (BreathLineForSpell(spellId, lineX1, lineY1, lineX2, lineY2) || AirborneBreathLine(onyxia, lineX1, lineY1, lineX2, lineY2))
            RememberBreath(onyxia->GetInstanceId(), lineX1, lineY1, lineX2, lineY2);

        return ActiveBreath(onyxia->GetInstanceId(), x1, y1, x2, y2);
    }

    bool InDeepBreath(Player* bot, Unit* onyxia)
    {
        float x1, y1, x2, y2;
        if (!UpdateBreathLatch(onyxia, x1, y1, x2, y2))
            return false;

        return DistanceToSegment(bot->GetPositionX(), bot->GetPositionY(), x1, y1, x2, y2) <= 18.0f;
    }

    bool TrySelfSpell(PlayerbotAI* ai, const char* spell)
    {
        Player* bot = ai->GetBot();
        if (ai->HasAura(spell, bot))
            return false;

        if (!ai->CanCastSpell(spell, bot, 0))
            return false;

        return ai->CastSpell(spell, bot);
    }

    class FireProtectionUse : public UseFireProtectionPotionAction
    {
    public:
        FireProtectionUse(PlayerbotAI* ai) : UseFireProtectionPotionAction(ai) {}
        bool Run(Event& event) { return isPossible() && isUseful() && Execute(event); }
    };

    void SurviveOnyxiaBreath(PlayerbotAI* ai, Event& event)
    {
        Player* bot = ai->GetBot();
        Unit* onyxia = FindOnyxia(bot);
        bool keepsThreat = IsOnyxiaMainTank(ai) || (onyxia && onyxia->GetVictim() == bot && ai->IsTank(bot, true));

        FireProtectionUse fireProtection(ai);
        fireProtection.Run(event);

        const char* mitigation[] =
        {
            "shield wall",
            "last stand",
            "barkskin",
            "frenzied regeneration",
            "power word: shield",
            "fire ward",
            "ice barrier",
            "mana shield"
        };

        bool castSpell = false;
        for (const char* spell : mitigation)
        {
            if (!TrySelfSpell(ai, spell))
                continue;

            castSpell = true;
            break;
        }

        if (!castSpell && !keepsThreat)
        {
            if (!TrySelfSpell(ai, "divine shield"))
                TrySelfSpell(ai, "ice block");
        }

        if (bot->GetHealthPercent() >= 70.0f)
            return;

        UseHealthstoneAction healthstone(ai);
        if (healthstone.isPossible() && healthstone.isUseful() && healthstone.Execute(event))
            return;

        UseHealingPotionAction healingPotion(ai);
        if (healingPotion.isPossible() && healingPotion.isUseful())
            healingPotion.Execute(event);
    }

    // Side caves where whelps are summoned. Stay on the main floor.
    const float WHELP_PIT_RADIUS = 22.0f;
    const float whelpPits[][2] =
    {
        {-30.127f, -254.463f},
        {-30.817f, -177.106f},
    };

    const float TANK_FRONT_YARDS = 8.0f;
    const float HEADING_TOLERANCE = 25.0f * M_PI_F / 180.0f;

    bool InWhelpPit(float x, float y)
    {
        for (const auto& pit : whelpPits)
        {
            if (Distance2d(x, y, pit[0], pit[1]) < WHELP_PIT_RADIUS)
                return true;
        }
        return false;
    }

    void PushOutOfWhelpPits(float& x, float& y)
    {
        for (int pass = 0; pass < 4; ++pass)
        {
            bool pushed = false;
            for (const auto& pit : whelpPits)
            {
                float dist = Distance2d(x, y, pit[0], pit[1]);
                if (dist >= WHELP_PIT_RADIUS)
                    continue;

                float dx = LAIR_CENTER_X - x;
                float dy = LAIR_CENTER_Y - y;
                float len = sqrt(dx * dx + dy * dy);
                float step = (WHELP_PIT_RADIUS - dist) + 3.0f;
                if (len < 0.1f)
                {
                    x = LAIR_CENTER_X + step;
                    y = LAIR_CENTER_Y;
                }
                else
                {
                    x += dx / len * step;
                    y += dy / len * step;
                }
                pushed = true;
            }
            if (!pushed)
                break;
        }
    }

    void TwelveOClockAxis(float& hx, float& hy)
    {
        hx = TANK_12_X - LAIR_CENTER_X;
        hy = TANK_12_Y - LAIR_CENTER_Y;
        float hlen = sqrt(hx * hx + hy * hy);
        hx /= hlen;
        hy /= hlen;
    }

    float AngleDifference(float a, float b)
    {
        float diff = a - b;
        while (diff > M_PI_F)
            diff -= 2.0f * M_PI_F;
        while (diff < -M_PI_F)
            diff += 2.0f * M_PI_F;
        return fabs(diff);
    }

    bool HeadFacesTwelve(Unit* onyxia)
    {
        float hx, hy;
        TwelveOClockAxis(hx, hy);
        float faceX = onyxia->GetPositionX() + hx * TANK_FRONT_YARDS;
        float faceY = onyxia->GetPositionY() + hy * TANK_FRONT_YARDS;
        return AngleDifference(onyxia->GetOrientation(), onyxia->GetAngle(faceX, faceY)) <= HEADING_TOLERANCE;
    }

    bool CanTauntOnyxia(PlayerbotAI* ai, Unit* onyxia)
    {
        const char* spells[] = { "taunt", "mocking blow", "growl", "hand of reckoning", "righteous defense" };
        for (const char* spell : spells)
        {
            if (ai->CanCastSpell(spell, onyxia, 0))
                return true;
        }
        return false;
    }

    void CollectWhelps(Player* bot, std::list<Unit*>& out)
    {
        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, NPC_ONYXIA_WHELP, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);

        for (Unit* unit : units)
        {
            if (unit && unit->IsAlive())
                out.push_back(unit);
        }
    }

    Unit* NearestWhelp(Player* bot, bool insidePit)
    {
        std::list<Unit*> whelps;
        CollectWhelps(bot, whelps);

        Unit* closest = nullptr;
        float best = 100.0f;
        for (Unit* whelp : whelps)
        {
            bool inPit = InWhelpPit(whelp->GetPositionX(), whelp->GetPositionY());
            if (inPit != insidePit)
                continue;

            float distance = bot->GetDistance(whelp);
            if (distance < best)
            {
                best = distance;
                closest = whelp;
            }
        }
        return closest;
    }

    bool ClearsWhelps(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        return !IsOnyxiaMainTank(ai) && !ai->IsHeal(bot) && !ai->IsRanged(bot);
    }

    bool ShouldLureWhelp(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!ClearsWhelps(ai))
            return false;

        Unit* onyxia = FindOnyxia(bot);
        if (!onyxia || GetOnyxiaPhase(onyxia) != 2)
            return false;

        if (NearestWhelp(bot, false))
            return false;

        return NearestWhelp(bot, true) != nullptr;
    }

    void StopPetAttack(Player* bot)
    {
        Pet* pet = bot->GetPet();
        if (!pet)
            return;

        const uint32 command = (uint32(ACT_COMMAND) << 24) | COMMAND_FOLLOW;
        WorldPacket data(CMSG_PET_ACTION);
        data << pet->GetObjectGuid();
        data << command;
        data << ObjectGuid();
        bot->GetSession()->HandlePetAction(data);
    }

    bool PickRangedPhase2Point(Unit* onyxia, uint32 slot, float attackRange, float& x, float& y)
    {
        float stand = attackRange - 10.0f + (slot % 3) * 2.0f;
        if (stand < 15.0f)
            stand = 15.0f;
        if (stand > attackRange - 5.0f)
            stand = attackRange - 5.0f;

        float base = (slot % 8) * (M_PI_F / 4.0f);
        float distances[] = { stand, 14.0f };
        for (float distance : distances)
        {
            for (int i = 0; i < 8; ++i)
            {
                float angle = base + i * (M_PI_F / 4.0f);
                float tx = onyxia->GetPositionX() + cos(angle) * distance;
                float ty = onyxia->GetPositionY() + sin(angle) * distance;
                if (InWhelpPit(tx, ty))
                    continue;

                x = tx;
                y = ty;
                return true;
            }
        }
        return false;
    }

}

Unit* ai::FindOnyxia(Player* bot)
{
    return FindCreature(bot, NPC_ONYXIA, 200.0f);
}

Unit* ai::FindNearestOnyxiaWhelp(Player* bot)
{
    return FindCreature(bot, NPC_ONYXIA_WHELP, 100.0f);
}

int ai::GetOnyxiaPhase(Unit* onyxia)
{
    if (!onyxia)
        return 0;

    // Hover and pacify are up for the whole air phase, including the side steps between breaths.
    if (onyxia->HasAura(SPELL_DRAGON_HOVER) || onyxia->HasAura(SPELL_PACIFY_SELF))
        return 2;

    if (onyxia->GetHealthPercent() > 65.0f)
        return 1;

    if (onyxia->GetHealthPercent() > 40.0f)
        return 2;

    return 3;
}

bool ai::IsOnyxiaMainTank(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return ai->GetMainTank() == bot;
}

Unit* ai::FindOnyxiaWhelpInRoom(Player* bot)
{
    return NearestWhelp(bot, false);
}

bool ai::IsInsideOnyxiaWhelpPit(float x, float y)
{
    return InWhelpPit(x, y);
}

bool ai::ShouldAttackOnyxiaWhelp(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    // Phase 2 melee clear whelps that have walked onto the main floor.
    if (!ClearsWhelps(ai))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || GetOnyxiaPhase(onyxia) != 2)
        return false;

    return FindOnyxiaWhelpInRoom(bot) != nullptr;
}

bool ai::ShouldHoldOnyxiaDps(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (ai->IsTank(bot, true))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || !onyxia->IsAlive())
        return false;

    int phase = GetOnyxiaPhase(onyxia);
    if (phase != 1 && phase != 3)
        return false;

    Unit* victim = onyxia->GetVictim();
    Player* tank = victim && victim->GetTypeId() == TYPEID_PLAYER ? static_cast<Player*>(victim) : nullptr;
    if (!tank || !PlayerbotAI::IsTank(tank, true))
        return true;

    float hx, hy;
    TwelveOClockAxis(hx, hy);
    float faceX = onyxia->GetPositionX() + hx * TANK_FRONT_YARDS;
    float faceY = onyxia->GetPositionY() + hy * TANK_FRONT_YARDS;
    if (Distance2d(tank->GetPositionX(), tank->GetPositionY(), faceX, faceY) > 6.0f)
        return true;

    return !HeadFacesTwelve(onyxia);
}

bool ai::GetOnyxiaBreathDodgePoint(Player* bot, float& x, float& y, float& z)
{
    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || !onyxia->IsAlive())
        return false;

    uint32 spellId = CurrentOnyxiaSpell(onyxia);
    if (spellId == SPELL_BREATH_ENTRANCE)
    {
        if (Distance2d(bot->GetPositionX(), bot->GetPositionY(), LAIR_CENTER_X, LAIR_CENTER_Y) <= 25.0f && bot->GetPositionZ() < LAIR_RAMP_Z)
            return false;

        x = LAIR_CENTER_X;
        y = LAIR_CENTER_Y;
        z = LAIR_CENTER_Z;
        return true;
    }

    float x1, y1, x2, y2;
    if (!UpdateBreathLatch(onyxia, x1, y1, x2, y2))
        return false;

    float px = bot->GetPositionX();
    float py = bot->GetPositionY();
    const float danger = 18.0f;
    const float clear = 22.0f;
    float dist = DistanceToSegment(px, py, x1, y1, x2, y2);
    if (dist > danger)
        return false;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
        return false;

    float nx = -dy / len;
    float ny = dx / len;
    float step = clear - dist;
    if (step < 6.0f)
        step = 6.0f;
    float leftX = px + nx * step;
    float leftY = py + ny * step;
    float rightX = px - nx * step;
    float rightY = py - ny * step;
    float leftDist = Distance2d(leftX, leftY, LAIR_CENTER_X, LAIR_CENTER_Y);
    float rightDist = Distance2d(rightX, rightY, LAIR_CENTER_X, LAIR_CENTER_Y);
    float side = leftDist <= rightDist ? 1.0f : -1.0f;

    // Step off the line toward the side that stays inside the crater.
    x = px + nx * side * step;
    y = py + ny * side * step;
    ClampToLair(x, y);

    // Pulling a tunnel point back toward the middle can slide it onto the breath again.
    if (DistanceToSegment(x, y, x1, y1, x2, y2) <= danger)
    {
        x += nx * side * step;
        y += ny * side * step;
        ClampToLair(x, y);
    }

    if (GetOnyxiaPhase(onyxia) == 2)
    {
        PushOutOfWhelpPits(x, y);
        if (DistanceToSegment(x, y, x1, y1, x2, y2) <= danger)
        {
            x += nx * side * step;
            y += ny * side * step;
            PushOutOfWhelpPits(x, y);
            ClampToLair(x, y);
        }
    }

    z = LairFloorZ(bot, x, y);
    return true;
}

bool ai::GetOnyxiaLairReturnPoint(Player* bot, float& x, float& y, float& z)
{
    float dx = bot->GetPositionX() - LAIR_CENTER_X;
    float dy = bot->GetPositionY() - LAIR_CENTER_Y;
    float dist = sqrt(dx * dx + dy * dy);
    bool onRamp = bot->GetPositionZ() > LAIR_RAMP_Z;
    if (dist <= LAIR_EDGE_RADIUS && !onRamp)
    {
        if (!InWhelpPit(bot->GetPositionX(), bot->GetPositionY()))
            return false;

        Unit* onyxia = FindOnyxia(bot);
        if (!onyxia || GetOnyxiaPhase(onyxia) != 2)
            return false;

        x = bot->GetPositionX();
        y = bot->GetPositionY();
        PushOutOfWhelpPits(x, y);
        if (InWhelpPit(x, y) || Distance2d(x, y, bot->GetPositionX(), bot->GetPositionY()) < 3.0f)
        {
            float stepX = LAIR_CENTER_X - bot->GetPositionX();
            float stepY = LAIR_CENTER_Y - bot->GetPositionY();
            float step = sqrt(stepX * stepX + stepY * stepY);
            if (step < 0.1f)
            {
                x = LAIR_CENTER_X;
                y = LAIR_CENTER_Y;
            }
            else
            {
                x = bot->GetPositionX() + stepX / step * 20.0f;
                y = bot->GetPositionY() + stepY / step * 20.0f;
            }
        }

        z = LairFloorZ(bot, x, y);
        return true;
    }

    if (dist < 1.0f)
    {
        x = LAIR_CENTER_X;
        y = LAIR_CENTER_Y;
    }
    else
    {
        float scale = 45.0f / dist;
        x = LAIR_CENTER_X + dx * scale;
        y = LAIR_CENTER_Y + dy * scale;
    }

    z = LAIR_CENTER_Z;
    return true;
}

bool ai::GetOnyxiaPositionPoint(PlayerbotAI* ai, float& x, float& y, float& z)
{
    Player* bot = ai->GetBot();
    float breathX, breathY, breathZ;
    if (GetOnyxiaBreathDodgePoint(bot, breathX, breathY, breathZ))
        return false;

    if (GetOnyxiaLairReturnPoint(bot, breathX, breathY, breathZ))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || !onyxia->IsAlive())
        return false;

    int phase = GetOnyxiaPhase(onyxia);
    uint32 slot = bot->GetObjectGuid().GetCounter();

    if (phase == 2)
    {
        // Melee who are clearing whelps, or waiting at a pit mouth, leave their slot.
        if (ShouldAttackOnyxiaWhelp(ai) || ShouldLureWhelp(ai))
            return false;

        if (ai->IsRanged(bot) || ai->IsHeal(bot))
        {
            // Stay on the ground around her, inside spell range, and outside the whelp pits.
            float attackRange = ai->GetRange("spell");
            if (attackRange < 30.0f)
                attackRange = 30.0f;

            float distToBoss = Distance2d(bot->GetPositionX(), bot->GetPositionY(), onyxia->GetPositionX(), onyxia->GetPositionY());
            if (distToBoss >= 12.0f && distToBoss <= attackRange - 5.0f && !InWhelpPit(bot->GetPositionX(), bot->GetPositionY()))
                return false;

            if (!PickRangedPhase2Point(onyxia, slot, attackRange, x, y))
            {
                float angle = (slot % 8) * (M_PI_F / 4.0f);
                x = onyxia->GetPositionX() + cos(angle) * 14.0f;
                y = onyxia->GetPositionY() + sin(angle) * 14.0f;
                PushOutOfWhelpPits(x, y);
            }
        }
        else
        {
            // Wider ring around the pit. Eight slots, staggered off the breath lines.
            float angle = (slot % 8) * (M_PI_F / 4.0f) + (M_PI_F / 8.0f);
            float radius = 40.0f + (slot % 3) * 6.0f;
            x = LAIR_CENTER_X + cos(angle) * radius;
            y = LAIR_CENTER_Y + sin(angle) * radius;
            PushOutOfWhelpPits(x, y);
        }
    }
    else
    {
        // Tanks use the 12 o'clock line. Everyone else stays off the head-tail axis.
        if (ai->IsTank(bot, true))
            return false;

        float hx, hy;
        TwelveOClockAxis(hx, hy);
        float sx = -hy;
        float sy = hx;
        float sideSign = (slot % 2) ? 1.0f : -1.0f;

        if (ai->IsRanged(bot) || ai->IsHeal(bot))
        {
            float along = -4.0f;
            float side = sideSign * (18.0f + (slot % 4) * 3.0f);
            x = onyxia->GetPositionX() + hx * along + sx * side;
            y = onyxia->GetPositionY() + hy * along + sy * side;
        }
        else
        {
            float side = sideSign * (8.0f + (slot % 3) * 2.0f);
            x = onyxia->GetPositionX() + sx * side;
            y = onyxia->GetPositionY() + sy * side;
        }
    }

    ClampToLair(x, y);
    if (phase == 2)
        PushOutOfWhelpPits(x, y);
    z = LairFloorZ(bot, x, y);
    // Phase 2 slots are sectors, not a single tile. Stop once the bot is in the sector.
    float arrived = phase == 2 ? 18.0f : 6.0f;
    return Distance2d(bot->GetPositionX(), bot->GetPositionY(), x, y) > arrived;
}

Unit* ai::FindOnyxiaFearWardTarget(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!ai->HasSpell("fear ward"))
        return nullptr;

    std::list<Unit*> candidates;
    if (Player* tank = ai->GetMainTank())
        candidates.push_back(tank);

    Unit* onyxia = FindOnyxia(bot);
    if (onyxia && onyxia->GetVictim())
        candidates.push_back(onyxia->GetVictim());

    candidates.push_back(bot);

    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member->IsAlive())
                candidates.push_back(member);
        }
    }

    for (Unit* unit : candidates)
    {
        if (!unit || !unit->IsAlive() || !unit->IsInWorld() || unit->GetMapId() != bot->GetMapId())
            continue;

        if (ai->HasAura("fear ward", unit))
            continue;

        if (!bot->IsWithinDistInMap(unit, 30.0f))
            continue;

        return unit;
    }

    return nullptr;
}

bool ai::ShouldOnyxiaMainTankTaunt(PlayerbotAI* ai)
{
    if (!IsOnyxiaMainTank(ai))
        return false;

    Unit* onyxia = FindOnyxia(ai->GetBot());
    if (!onyxia || !onyxia->IsAlive() || GetOnyxiaPhase(onyxia) == 2)
        return false;

    return onyxia->GetVictim() != ai->GetBot();
}

bool OnyxiaMoveAwayFromBreathAction::Execute(Event& event)
{
    float x, y, z;
    if (!GetOnyxiaBreathDodgePoint(bot, x, y, z))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (onyxia && InDeepBreath(bot, onyxia))
        SurviveOnyxiaBreath(ai, event);

    return MoveTo(bot->GetMapId(), x, y, z, false, IsReaction(), false, true);
}

bool OnyxiaMoveAwayFromBreathAction::isUseful()
{
    float x, y, z;
    return MovementAction::isUseful() && GetOnyxiaBreathDodgePoint(bot, x, y, z);
}

bool OnyxiaMoveBackIntoLairAction::Execute(Event& event)
{
    float x, y, z;
    if (!GetOnyxiaLairReturnPoint(bot, x, y, z))
        return false;

    return MoveTo(bot->GetMapId(), x, y, z, false, IsReaction(), false, true);
}

bool OnyxiaMoveBackIntoLairAction::isUseful()
{
    float x, y, z;
    return MovementAction::isUseful() && GetOnyxiaLairReturnPoint(bot, x, y, z);
}

bool OnyxiaMoveToPositionAction::Execute(Event& event)
{
    float x, y, z;
    if (!GetOnyxiaPositionPoint(ai, x, y, z))
        return false;

    return MoveTo(bot->GetMapId(), x, y, z, false, IsReaction(), false, true);
}

bool OnyxiaMoveToPositionAction::isUseful()
{
    float x, y, z;
    return MovementAction::isUseful() && GetOnyxiaPositionPoint(ai, x, y, z);
}

bool OnyxiaAttackWhelpAction::Execute(Event& event)
{
    Unit* whelp = FindOnyxiaWhelpInRoom(bot);
    if (!whelp)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, whelp);
}

bool OnyxiaAttackWhelpAction::isUseful()
{
    if (!ShouldAttackOnyxiaWhelp(ai))
        return false;

    Unit* whelp = FindOnyxiaWhelpInRoom(bot);
    Unit* current = ai->GetUnit(AI_VALUE(ObjectGuid, "current target"));
    return whelp && current != whelp;
}

bool OnyxiaFearWardAction::Execute(Event& event)
{
    Unit* target = FindOnyxiaFearWardTarget(ai);
    if (!target)
        return false;

    return ai->CastSpell("fear ward", target);
}

bool OnyxiaFearWardAction::isUseful()
{
    Unit* target = FindOnyxiaFearWardTarget(ai);
    return target && ai->CanCastSpell("fear ward", target, 0);
}

bool OnyxiaMainTankTauntAction::Execute(Event& event)
{
    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia)
        return false;

    SET_AI_VALUE(ObjectGuid, "current target", onyxia->GetObjectGuid());

    const char* spells[] = { "taunt", "mocking blow", "growl", "hand of reckoning", "righteous defense" };
    for (const char* spell : spells)
    {
        if (ai->CanCastSpell(spell, onyxia, 0) && ai->CastSpell(spell, onyxia))
            return true;
    }

    return false;
}

bool OnyxiaMainTankTauntAction::isUseful()
{
    return ShouldOnyxiaMainTankTaunt(ai);
}

bool ai::GetOnyxiaTankPoint(PlayerbotAI* ai, float& x, float& y, float& z)
{
    Player* bot = ai->GetBot();
    if (!ai->IsTank(bot, true))
        return false;

    float skipX, skipY, skipZ;
    if (GetOnyxiaBreathDodgePoint(bot, skipX, skipY, skipZ))
        return false;
    if (GetOnyxiaLairReturnPoint(bot, skipX, skipY, skipZ))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || !onyxia->IsAlive())
        return false;

    int phase = GetOnyxiaPhase(onyxia);
    if (phase != 1 && phase != 3)
        return false;

    bool isVictim = onyxia->GetVictim() == bot;
    bool isMainTank = IsOnyxiaMainTank(ai);
    float stand = TANK_FRONT_YARDS;
    if (!isVictim && !isMainTank)
        stand = 12.0f + (bot->GetObjectGuid().GetCounter() % 4) * 2.0f;

    float hx, hy;
    TwelveOClockAxis(hx, hy);
    x = onyxia->GetPositionX() + hx * stand;
    y = onyxia->GetPositionY() + hy * stand;
    ClampToLair(x, y);
    z = LairFloorZ(bot, x, y);

    // Taunt is on cooldown and she is already in melee. Hit her instead of walking.
    if (isMainTank && !isVictim && !CanTauntOnyxia(ai, onyxia))
    {
        if (bot->GetDistance(onyxia, false, DIST_CALC_COMBAT_REACH) <= 5.0f)
            return false;
    }

    float stop = isVictim && !HeadFacesTwelve(onyxia) ? 3.0f : 5.0f;
    return Distance2d(bot->GetPositionX(), bot->GetPositionY(), x, y) > stop;
}

bool ai::GetOnyxiaWhelpLurePoint(PlayerbotAI* ai, float& x, float& y, float& z)
{
    Player* bot = ai->GetBot();
    if (!ShouldLureWhelp(ai))
        return false;

    Unit* whelp = NearestWhelp(bot, true);
    if (!whelp)
        return false;

    const float* pit = whelpPits[0];
    float best = Distance2d(whelp->GetPositionX(), whelp->GetPositionY(), pit[0], pit[1]);
    for (const auto& candidate : whelpPits)
    {
        float dist = Distance2d(whelp->GetPositionX(), whelp->GetPositionY(), candidate[0], candidate[1]);
        if (dist < best)
        {
            best = dist;
            pit = candidate;
        }
    }

    float dx = LAIR_CENTER_X - pit[0];
    float dy = LAIR_CENTER_Y - pit[1];
    float len = sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
        return false;

    float mouth = WHELP_PIT_RADIUS + 4.0f;
    x = pit[0] + dx / len * mouth;
    y = pit[1] + dy / len * mouth;
    PushOutOfWhelpPits(x, y);
    ClampToLair(x, y);
    z = LairFloorZ(bot, x, y);
    return Distance2d(bot->GetPositionX(), bot->GetPositionY(), x, y) > 6.0f;
}

bool OnyxiaMoveToTankSpotAction::Execute(Event& event)
{
    float x, y, z;
    if (!GetOnyxiaTankPoint(ai, x, y, z))
        return false;

    return MoveTo(bot->GetMapId(), x, y, z, false, IsReaction(), false, true);
}

bool OnyxiaMoveToTankSpotAction::isUseful()
{
    float x, y, z;
    return MovementAction::isUseful() && GetOnyxiaTankPoint(ai, x, y, z);
}

bool OnyxiaHoldDpsAction::Execute(Event& event)
{
    bot->AttackStop();
    bot->SetSelectionGuid(ObjectGuid());
    SET_AI_VALUE(ObjectGuid, "current target", ObjectGuid());
    StopPetAttack(bot);
    // Leave the tick free so heals and positioning still run.
    return false;
}

bool OnyxiaHoldDpsAction::isUseful()
{
    return ShouldHoldOnyxiaDps(ai);
}

bool OnyxiaLureWhelpAction::Execute(Event& event)
{
    float x, y, z;
    if (!GetOnyxiaWhelpLurePoint(ai, x, y, z))
        return false;

    return MoveTo(bot->GetMapId(), x, y, z, false, IsReaction(), false, true);
}

bool OnyxiaLureWhelpAction::isUseful()
{
    float x, y, z;
    return MovementAction::isUseful() && GetOnyxiaWhelpLurePoint(ai, x, y, z);
}
