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

    struct WhelpScan
    {
        uint32 botLow;
        uint32 stamp;
        ObjectGuid guids[40];
        uint8 count;
        bool used;
    };

    WhelpScan whelpScans[48] = {};

    void CollectWhelps(Player* bot, std::list<Unit*>& out)
    {
        uint32 now = WorldTimer::getMSTime();
        uint32 botLow = bot->GetObjectGuid().GetCounter();
        WhelpScan* slot = nullptr;
        for (WhelpScan& scan : whelpScans)
        {
            if (scan.used && scan.botLow == botLow)
            {
                slot = &scan;
                break;
            }
            if (!slot && !scan.used)
                slot = &scan;
        }
        if (!slot)
            slot = &whelpScans[botLow % 48];

        if (slot->used && slot->botLow == botLow && WorldTimer::getMSTimeDiff(slot->stamp, now) < 1000)
        {
            Map* map = bot->GetMap();
            for (uint8 i = 0; i < slot->count; ++i)
            {
                Unit* unit = map ? map->GetUnit(slot->guids[i]) : nullptr;
                if (unit && unit->IsAlive())
                    out.push_back(unit);
            }
            return;
        }

        std::list<Unit*> units;
        MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, NPC_ONYXIA_WHELP, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);

        slot->used = true;
        slot->botLow = botLow;
        slot->stamp = now;
        slot->count = 0;
        for (Unit* unit : units)
        {
            if (!unit || !unit->IsAlive())
                continue;

            out.push_back(unit);
            if (slot->count < 40)
                slot->guids[slot->count++] = unit->GetObjectGuid();
        }
    }

    Unit* NearestWhelp(Player* bot, bool insidePit, float maxRange = 100.0f)
    {
        std::list<Unit*> whelps;
        CollectWhelps(bot, whelps);

        Unit* closest = nullptr;
        float best = maxRange;
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

    // Two bots per pit, chosen by guid. Everyone else keeps a floor slot.
    bool HoldsOnyxiaPit(PlayerbotAI* ai)
    {
        if (!ClearsWhelps(ai))
            return false;

        return (ai->GetBot()->GetObjectGuid().GetCounter() % 8) < 2;
    }

    bool ShouldLureWhelp(PlayerbotAI* ai)
    {
        if (!HoldsOnyxiaPit(ai))
            return false;

        Player* bot = ai->GetBot();
        Unit* onyxia = FindOnyxia(bot);
        if (!onyxia || GetOnyxiaPhase(onyxia) != 2)
            return false;

        // A whelp that has left the pit is an attack target, not a reason to change mouths.
        return FindOnyxiaWhelpInRoom(bot) == nullptr;
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

    // East and west wedges. North and south are the whelp pits, and the four breath lines
    // run through the middle, so these four bearings stay on the floor.
    const float phase2Angles[4] =
    {
        22.5f * M_PI_F / 180.0f,
        157.5f * M_PI_F / 180.0f,
        202.5f * M_PI_F / 180.0f,
        337.5f * M_PI_F / 180.0f,
    };

    void Phase2SpreadPoint(uint32 slot, float radius, float& x, float& y)
    {
        float angle = phase2Angles[slot % 4];
        int jitter = (int)((slot / 4) % 5) - 2;
        angle += jitter * (4.0f * M_PI_F / 180.0f);
        x = LAIR_CENTER_X + cos(angle) * radius;
        y = LAIR_CENTER_Y + sin(angle) * radius;
    }

    struct BreathDodge
    {
        uint32 botLow;
        uint32 instanceId;
        float x1, y1, x2, y2;
        float x, y, z;
        bool used;
    };

    BreathDodge breathDodges[64] = {};

    BreathDodge* BreathDodgeSlot(Player* bot, bool create)
    {
        uint32 botLow = bot->GetObjectGuid().GetCounter();
        uint32 instanceId = bot->GetInstanceId();
        BreathDodge* freeSlot = nullptr;
        for (BreathDodge& dodge : breathDodges)
        {
            if (dodge.used && dodge.botLow == botLow && dodge.instanceId == instanceId)
                return &dodge;
            if (!freeSlot && !dodge.used)
                freeSlot = &dodge;
        }

        if (!create)
            return nullptr;

        if (!freeSlot)
            freeSlot = &breathDodges[botLow % 64];

        freeSlot->used = true;
        freeSlot->botLow = botLow;
        freeSlot->instanceId = instanceId;
        return freeSlot;
    }

    void ClearBreathDodge(Player* bot)
    {
        if (BreathDodge* dodge = BreathDodgeSlot(bot, false))
            dodge->used = false;
    }

    bool ReuseBreathDodge(Player* bot, float x1, float y1, float x2, float y2, float& x, float& y, float& z)
    {
        BreathDodge* dodge = BreathDodgeSlot(bot, false);
        if (!dodge)
            return false;

        if (Distance2d(dodge->x1, dodge->y1, x1, y1) > 1.0f || Distance2d(dodge->x2, dodge->y2, x2, y2) > 1.0f)
            return false;

        // Arrived, and this line is still dangerous. Take one more step instead of chasing a moving point.
        if (Distance2d(bot->GetPositionX(), bot->GetPositionY(), dodge->x, dodge->y) <= 3.0f)
            return false;

        x = dodge->x;
        y = dodge->y;
        z = dodge->z;
        return true;
    }

    void StoreBreathDodge(Player* bot, float x1, float y1, float x2, float y2, float x, float y, float z)
    {
        BreathDodge* dodge = BreathDodgeSlot(bot, true);
        dodge->x1 = x1;
        dodge->y1 = y1;
        dodge->x2 = x2;
        dodge->y2 = y2;
        dodge->x = x;
        dodge->y = y;
        dodge->z = z;
    }

    struct MoveOrder
    {
        uint32 botLow;
        uint32 stamp;
        float x, y, z;
        bool used;
    };

    MoveOrder breathOrders[64] = {};

    bool BreathMoveUnderway(Player* bot, float x, float y, float z)
    {
        uint32 botLow = bot->GetObjectGuid().GetCounter();
        for (const MoveOrder& order : breathOrders)
        {
            if (!order.used || order.botLow != botLow)
                continue;

            if (Distance2d(order.x, order.y, x, y) >= 2.5f || fabs(order.z - z) >= 4.0f)
                return false;

            // Already on the safe point. A new order is needed only if this line is still dangerous.
            if (Distance2d(bot->GetPositionX(), bot->GetPositionY(), order.x, order.y) <= 3.0f)
                return false;

            if (sServerFacade.isMoving(bot))
                return true;

            // Movement flags lag a moment behind MoveTo. Don't cancel and repath in that gap.
            return WorldTimer::getMSTimeDiff(order.stamp, WorldTimer::getMSTime()) < 800;
        }
        return false;
    }

    void RememberBreathMove(Player* bot, float x, float y, float z)
    {
        uint32 botLow = bot->GetObjectGuid().GetCounter();
        MoveOrder* slot = nullptr;
        for (MoveOrder& order : breathOrders)
        {
            if (order.used && order.botLow == botLow)
            {
                slot = &order;
                break;
            }
            if (!slot && !order.used)
                slot = &order;
        }
        if (!slot)
            slot = &breathOrders[botLow % 64];

        slot->used = true;
        slot->botLow = botLow;
        slot->stamp = WorldTimer::getMSTime();
        slot->x = x;
        slot->y = y;
        slot->z = z;
    }

    struct Phase3DpsHold
    {
        uint32 instanceId;
        uint32 started;
        bool used;
    };

    Phase3DpsHold phase3DpsHolds[8] = {};

    Phase3DpsHold* Phase3DpsHoldSlot(uint32 instanceId, bool create)
    {
        Phase3DpsHold* freeSlot = nullptr;
        for (Phase3DpsHold& hold : phase3DpsHolds)
        {
            if (hold.used && hold.instanceId == instanceId)
                return &hold;
            if (!freeSlot && !hold.used)
                freeSlot = &hold;
        }

        if (!create)
            return nullptr;

        if (!freeSlot)
            freeSlot = &phase3DpsHolds[instanceId % 8];

        freeSlot->used = true;
        freeSlot->instanceId = instanceId;
        freeSlot->started = WorldTimer::getMSTime();
        return freeSlot;
    }

    void ClearPhase3DpsHold(uint32 instanceId)
    {
        if (Phase3DpsHold* hold = Phase3DpsHoldSlot(instanceId, false))
            hold->used = false;
    }

    // Shared by the raid. The first DPS to see her land starts the 20 seconds.
    bool Phase3DpsStopped(uint32 instanceId)
    {
        Phase3DpsHold* hold = Phase3DpsHoldSlot(instanceId, true);
        return WorldTimer::getMSTimeDiff(hold->started, WorldTimer::getMSTime()) < 20000;
    }

}

Unit* ai::FindOnyxia(Player* bot)
{
    if (!bot || !bot->GetMap())
        return nullptr;

    struct OnyxiaScan
    {
        uint32 instanceId;
        uint32 stamp;
        ObjectGuid guid;
        bool searched;
    };
    static OnyxiaScan scans[8] = {};

    uint32 now = WorldTimer::getMSTime();
    uint32 instanceId = bot->GetInstanceId();
    OnyxiaScan* slot = nullptr;
    for (OnyxiaScan& scan : scans)
    {
        if (scan.searched && scan.instanceId == instanceId)
        {
            slot = &scan;
            break;
        }
        if (!slot && !scan.searched)
            slot = &scan;
    }
    if (!slot)
        slot = &scans[instanceId % 8];

    if (slot->searched && slot->instanceId == instanceId && WorldTimer::getMSTimeDiff(slot->stamp, now) < 1000)
    {
        if (slot->guid.IsEmpty())
            return nullptr;

        Unit* unit = bot->GetMap()->GetUnit(slot->guid);
        if (unit && unit->IsAlive())
            return unit;
    }

    Unit* found = FindCreature(bot, NPC_ONYXIA, 200.0f);
    slot->searched = true;
    slot->instanceId = instanceId;
    slot->stamp = now;
    slot->guid = found ? found->GetObjectGuid() : ObjectGuid();
    return found;
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
    // Only whelps that have walked out near this bot. Chasing one across the crater
    // pulls melee off their slots and repaths the raid.
    return NearestWhelp(bot, false, 40.0f);
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
    // Main tank and off tanks keep attacking.
    if (ai->IsTank(bot, true))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (!onyxia || !onyxia->IsAlive())
        return false;

    int phase = GetOnyxiaPhase(onyxia);
    if (phase == 3)
        return Phase3DpsStopped(onyxia->GetInstanceId());

    ClearPhase3DpsHold(onyxia->GetInstanceId());
    if (phase != 1)
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
    {
        ClearBreathDodge(bot);
        return false;
    }

    float px = bot->GetPositionX();
    float py = bot->GetPositionY();
    const float danger = 18.0f;
    const float clear = 22.0f;
    float dist = DistanceToSegment(px, py, x1, y1, x2, y2);
    if (dist > danger)
    {
        ClearBreathDodge(bot);
        return false;
    }

    // One safe point per breath line. Recomputing from the bot's position every
    // reaction tick moves the destination and forces a new navmesh search.
    if (ReuseBreathDodge(bot, x1, y1, x2, y2, x, y, z))
        return true;

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
    StoreBreathDodge(bot, x1, y1, x2, y2, x, y, z);
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
        // Whelps on the floor, and the few bots posted at a pit mouth, leave the ring.
        if (ShouldAttackOnyxiaWhelp(ai) || ShouldLureWhelp(ai))
            return false;

        // Ranged follow her with reach spell. A fixed slot would pull them back out of range.
        if (ai->IsRanged(bot) && !ai->IsHeal(bot))
            return false;

        // Healers and melee hold fixed sectors. Her flight path is not their destination.
        float radius;
        if (ai->IsHeal(bot))
            radius = 18.0f + (slot % 3) * 3.0f;
        else if (ai->IsRanged(bot))
            radius = 30.0f + (slot % 3) * 4.0f;
        else
            radius = 46.0f + (slot % 3) * 4.0f;

        Phase2SpreadPoint(slot, radius, x, y);
        PushOutOfWhelpPits(x, y);
        ClampToLair(x, y);
        if (Distance2d(bot->GetPositionX(), bot->GetPositionY(), x, y) <= 4.0f)
            return false;

        z = LairFloorZ(bot, x, y);
        return true;
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
    if (Distance2d(bot->GetPositionX(), bot->GetPositionY(), x, y) <= 6.0f)
        return false;

    z = LairFloorZ(bot, x, y);
    return true;
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

bool OnyxiaMoveAwayFromBreathAction::ShouldReactionInterruptMovement() const
{
    float x, y, z;
    if (!GetOnyxiaBreathDodgePoint(bot, x, y, z))
        return false;

    // The reaction engine stops movement every time this fires. Leave a dodge that is already underway.
    return !BreathMoveUnderway(bot, x, y, z);
}

bool OnyxiaMoveAwayFromBreathAction::Execute(Event& event)
{
    float x, y, z;
    if (!GetOnyxiaBreathDodgePoint(bot, x, y, z))
        return false;

    Unit* onyxia = FindOnyxia(bot);
    if (onyxia && InDeepBreath(bot, onyxia))
        SurviveOnyxiaBreath(ai, event);

    if (BreathMoveUnderway(bot, x, y, z))
    {
        SetDuration(500);
        return true;
    }

    if (!MoveTo(bot->GetMapId(), x, y, z, false, IsReaction(), false, true))
        return false;

    RememberBreathMove(bot, x, y, z);
    return true;
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

    uint32 slot = bot->GetObjectGuid().GetCounter();
    const float* pit = whelpPits[slot % 2];

    float dx = LAIR_CENTER_X - pit[0];
    float dy = LAIR_CENTER_Y - pit[1];
    float len = sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
        return false;

    float mouth = WHELP_PIT_RADIUS + 4.0f;
    float side = ((slot / 8) % 2 ? 1.0f : -1.0f) * 5.0f;
    float px = -dy / len;
    float py = dx / len;
    x = pit[0] + dx / len * mouth + px * side;
    y = pit[1] + dy / len * mouth + py * side;
    PushOutOfWhelpPits(x, y);
    ClampToLair(x, y);
    if (Distance2d(bot->GetPositionX(), bot->GetPositionY(), x, y) <= 6.0f)
        return false;

    z = LairFloorZ(bot, x, y);
    return true;
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
