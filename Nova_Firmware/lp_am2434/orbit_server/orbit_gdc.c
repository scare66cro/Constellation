/*
 * orbit_gdc.c — GDC (Gellert Door Controller) role implementation.
 *
 * State machine ported from orbit-simulator/src/orbitSimulator.ts:
 *   - computeDoorTargets()   → gdc_compute_door_targets()
 *   - updateGDCActuators()   → gdc_tick_run()
 *   - updateCalibration()    → gdc_tick_calibration()
 *   - startCalibration()     → gdc_handle_cal_request()
 *
 * All percentages stored as fixed-point ×10 (0..1000). Travel times in ms.
 * No floating point — multiply ms*1000 fits easily in uint32 for typical
 * 1 s..1000 s travel times.
 *
 * See orbit_gdc.h for the HR map and safe-mode policy.
 */

#include "orbit_gdc.h"
#include "orbit_storage.h"
#include "orbit_state.h"
#include "../lp_orbit_io.h"

#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/DebugP.h>

#include <string.h>

/* --- helpers ----------------------------------------------------------- */

/* Effective travel time for the open or close direction in ms. Returns 0
 * if neither calibrated nor a default is set — caller treats 0 as
 * "do not move". */
static uint16_t effective_travel_ms(const GdcActuator *a,
                                    const GdcState *g, bool opening)
{
    uint16_t cal = opening ? a->open_travel_ms : a->close_travel_ms;
    if (a->calibrated && cal > 0) return cal;
    return g->default_travel_ms;
}

/* Per-tick position delta (×10 of percent) for a given travel time.
 * delta_x10 = 1000 * tick_ms / travel_ms. Returns 0 when travel_ms==0
 * (caller will skip movement). */
static uint16_t delta_per_tick_x10(uint16_t travel_ms)
{
    if (travel_ms == 0) return 0;
    uint32_t d = (1000U * ORBIT_GDC_TICK_MS) / (uint32_t)travel_ms;
    if (d == 0) d = 1;          /* always make at least 1‰ progress      */
    if (d > 1000) d = 1000;     /* clamp absurd configs                  */
    return (uint16_t)d;
}

/* --- target distribution (mirrors computeDoorTargets) ------------------- */

/* For each of the 5 doors, fill targets[] with 0..1000 based on the
 * current door_pct_x10 and per-actuator stage assignment. Doors with
 * stage==0 are excluded. Doors in the same stage move together; the
 * stage's "time slot" equals max travel time of doors in it. */
static void gdc_compute_door_targets(const GdcState *g, uint16_t targets[5])
{
    for (int i = 0; i < 5; i++) targets[i] = 0;
    if (g->door_pct_x10 == 0) return;

    /* Sum of all assigned doors' travel times. */
    uint32_t total_capacity_ms = 0;
    for (int i = 0; i < 5; i++) {
        if (g->act[i].stage == 0) continue;
        uint16_t t = effective_travel_ms(&g->act[i], g, /*opening=*/true);
        total_capacity_ms += t;
    }
    if (total_capacity_ms == 0) return;

    /* Budget in ms: pct × capacity / 1000 (pct is ×10 of percent so
     * already in 0..1000 scale). */
    uint32_t budget_ms =
        (uint32_t)((uint64_t)g->door_pct_x10 * total_capacity_ms / 1000U);

    /* Walk stages 1..5 in order. Doors of the same stage consume the
     * stage's max-travel-time slot in parallel. */
    for (uint8_t stage = 1; stage <= 5 && budget_ms > 0; stage++) {
        uint16_t max_travel = 0;
        for (int i = 0; i < 5; i++) {
            if (g->act[i].stage != stage) continue;
            uint16_t t = effective_travel_ms(&g->act[i], g, true);
            if (t > max_travel) max_travel = t;
        }
        if (max_travel == 0) continue;

        uint32_t used_ms = budget_ms < max_travel ? budget_ms : max_travel;
        for (int i = 0; i < 5; i++) {
            if (g->act[i].stage != stage) continue;
            uint16_t t = effective_travel_ms(&g->act[i], g, true);
            if (t == 0) continue;
            uint32_t pct_x10 = (used_ms * 1000U) / t;
            if (pct_x10 > 1000U) pct_x10 = 1000U;
            targets[i] = (uint16_t)pct_x10;
        }
        budget_ms -= used_ms;
    }
}

/* --- normal-operation tick --------------------------------------------- */

static void gdc_tick_run(OrbitStateData *s)
{
    GdcState *g = &s->gdc;

    uint16_t targets[5];
    gdc_compute_door_targets(g, targets);

    uint8_t active_stages_bits = 0;

    for (int i = 0; i < 5; i++) {
        GdcActuator *a = &g->act[i];
        a->target_x10 = targets[i];

        bool opening = a->pos_x10 < a->target_x10;
        uint16_t travel_ms = effective_travel_ms(a, g, opening);
        uint16_t step = delta_per_tick_x10(travel_ms);

        const int do_open  = i * 2;
        const int do_close = i * 2 + 1;
        const int di_open  = i * 2;
        const int di_close = i * 2 + 1;

        /* Update virtual limit switches FIRST (mirrors sim). */
        a->open_switch  = (a->pos_x10 >= 990);
        a->close_switch = (a->pos_x10 <= 10);

        const uint16_t deadband = 5;   /* ±0.5% — sim uses ±0.5 */

        if (step == 0) {
            /* No travel time programmed — freeze in place. */
            a->moving = false;
            s->do_[do_open]  = false;
            s->do_[do_close] = false;
        } else if (a->target_x10 > a->pos_x10 + deadband && !a->open_switch) {
            a->moving = true;
            uint16_t new_pos = (a->pos_x10 + step > a->target_x10)
                                 ? a->target_x10
                                 : (uint16_t)(a->pos_x10 + step);
            a->pos_x10 = new_pos;
            s->do_[do_open]  = true;
            s->do_[do_close] = false;
        } else if (a->target_x10 + deadband < a->pos_x10 && !a->close_switch) {
            a->moving = true;
            uint16_t new_pos = (a->pos_x10 < step + a->target_x10)
                                 ? a->target_x10
                                 : (uint16_t)(a->pos_x10 - step);
            a->pos_x10 = new_pos;
            s->do_[do_open]  = false;
            s->do_[do_close] = true;
        } else {
            /* At target / at limit / inside deadband. */
            if (a->open_switch)  a->pos_x10 = 1000;
            if (a->close_switch) a->pos_x10 = 0;
            a->moving = false;
            s->do_[do_open]  = false;
            s->do_[do_close] = false;
        }

        /* Final safety: never assert both pulses (mirrors sim assertion). */
        if (s->do_[do_open] && s->do_[do_close]) {
            s->do_[do_open]  = false;
            s->do_[do_close] = false;
            a->moving = false;
            DebugP_log("[GDC] SAFETY: door %d open+close — forced OFF\r\n", i + 1);
        }

        /* Reflect limit switches to DI. */
        s->di[di_open]  = a->open_switch;
        s->di[di_close] = a->close_switch;

        /* Track which stages are active (target>0). */
        if (a->target_x10 > 0 && a->stage > 0 && a->stage <= 5) {
            active_stages_bits |= (uint8_t)(1u << (a->stage - 1));
        }
    }

    /* popcount of the 5 stage bits. */
    uint8_t cnt = 0;
    for (uint8_t m = active_stages_bits; m; m >>= 1) cnt += (uint8_t)(m & 1u);
    g->active_stage_count = cnt;
}

/* --- calibration tick (mirrors updateCalibration) ----------------------- */

static void gdc_tick_calibration(OrbitStateData *s, uint32_t now_ticks)
{
    GdcState *g = &s->gdc;
    uint16_t step = delta_per_tick_x10(g->default_travel_ms);

    for (int i = 0; i < 5; i++) {
        GdcActuator *a = &g->act[i];
        const int do_open  = i * 2;
        const int do_close = i * 2 + 1;

        if (g->cal_phase == GDC_CAL_OPENING) {
            if (!a->open_switch) {
                a->moving = true;
                a->pos_x10 = (a->pos_x10 + step >= 1000) ? 1000
                                                         : (uint16_t)(a->pos_x10 + step);
                s->do_[do_open]  = true;
                s->do_[do_close] = false;
                if (a->pos_x10 >= 990) {
                    a->open_switch  = true;
                    a->close_switch = false;
                    a->moving = false;
                    s->do_[do_open] = false;
                    if (a->cal_start_ticks > 0) {
                        uint32_t elapsed_ticks = now_ticks - a->cal_start_ticks;
                        uint32_t elapsed_ms =
                            (uint32_t)elapsed_ticks * (uint32_t)portTICK_PERIOD_MS;
                        if (elapsed_ms > 0xFFFFu) elapsed_ms = 0xFFFFu;
                        a->open_travel_ms = (uint16_t)elapsed_ms;
                        DebugP_log("[GDC] door %d open travel = %u ms\r\n",
                                   i + 1, (unsigned)elapsed_ms);
                    }
                }
            } else {
                a->moving = false;
                s->do_[do_open]  = false;
                s->do_[do_close] = false;
            }
        } else if (g->cal_phase == GDC_CAL_CLOSING) {
            if (!a->close_switch) {
                a->moving = true;
                a->pos_x10 = (a->pos_x10 < step) ? 0 : (uint16_t)(a->pos_x10 - step);
                s->do_[do_open]  = false;
                s->do_[do_close] = true;
                if (a->pos_x10 <= 10) {
                    a->close_switch = true;
                    a->open_switch  = false;
                    a->moving = false;
                    s->do_[do_close] = false;
                    if (a->cal_start_ticks > 0) {
                        uint32_t elapsed_ticks = now_ticks - a->cal_start_ticks;
                        uint32_t elapsed_ms =
                            (uint32_t)elapsed_ticks * (uint32_t)portTICK_PERIOD_MS;
                        if (elapsed_ms > 0xFFFFu) elapsed_ms = 0xFFFFu;
                        a->close_travel_ms = (uint16_t)elapsed_ms;
                        DebugP_log("[GDC] door %d close travel = %u ms\r\n",
                                   i + 1, (unsigned)elapsed_ms);
                    }
                }
            } else {
                a->moving = false;
                s->do_[do_open]  = false;
                s->do_[do_close] = false;
            }
        }

        /* Safety + DI mirror — same as run path. */
        if (s->do_[do_open] && s->do_[do_close]) {
            s->do_[do_open]  = false;
            s->do_[do_close] = false;
            a->moving = false;
            DebugP_log("[GDC] SAFETY (cal): door %d open+close — forced OFF\r\n", i + 1);
        }
        s->di[i * 2]     = a->open_switch;
        s->di[i * 2 + 1] = a->close_switch;
    }

    /* Phase complete? */
    bool all_at_limit = true;
    if (g->cal_phase == GDC_CAL_OPENING) {
        for (int i = 0; i < 5; i++) if (!g->act[i].open_switch)  { all_at_limit = false; break; }
    } else {
        for (int i = 0; i < 5; i++) if (!g->act[i].close_switch) { all_at_limit = false; break; }
    }

    if (all_at_limit) {
        if (g->cal_phase == GDC_CAL_OPENING) {
            g->cal_phase = GDC_CAL_CLOSING;
            for (int i = 0; i < 5; i++) g->act[i].cal_start_ticks = now_ticks;
            DebugP_log("[GDC] calibration: open phase done, starting close\r\n");
        } else {
            g->calibrating = false;
            g->cal_phase   = GDC_CAL_IDLE;
            for (int i = 0; i < 5; i++) {
                g->act[i].cal_start_ticks = 0;
                g->act[i].calibrated      = true;
            }
            DebugP_log("[GDC] calibration complete\r\n");
        }
    }
}

/* --- start-calibration trigger (mirrors startCalibration) --------------- */

static void gdc_handle_cal_request(OrbitStateData *s, uint32_t now_ticks)
{
    GdcState *g = &s->gdc;
    if (g->calibrating) return;            /* already running */

    DebugP_log("[GDC] starting calibration\r\n");

    bool all_closed = true;
    for (int i = 0; i < 5; i++) {
        if (!g->act[i].close_switch) { all_closed = false; break; }
    }

    g->calibrating = true;
    if (all_closed) {
        g->cal_phase = GDC_CAL_OPENING;
        for (int i = 0; i < 5; i++) g->act[i].cal_start_ticks = now_ticks;
    } else {
        g->cal_phase = GDC_CAL_CLOSING;
        for (int i = 0; i < 5; i++) {
            g->act[i].target_x10 = 0;
            g->act[i].cal_start_ticks = g->act[i].close_switch ? 0 : now_ticks;
        }
    }
}

/* --- public API: init + safemode + task --------------------------------- */

void OrbitGdc_Init(void)
{
    /* Cold-boot defaults are seeded by OrbitState_Init (memset to zero
     * + close_switch=true). Nothing else to do here — kept as a hook
     * so role-spawn code in main.c is symmetric across orbit roles. */
}

void OrbitGdc_EnforceSafeMode(void)
{
    /* Caller (orbit_safemode_task) already holds OrbitState_Lock. */
    OrbitStateData *s = OrbitState_Get();
    GdcState *g = &s->gdc;

    /* Abort any in-flight calibration. */
    g->calibrating = false;
    g->cal_phase   = GDC_CAL_IDLE;

    for (int i = 0; i < 5; i++) {
        GdcActuator *a = &g->act[i];
        a->target_x10 = a->pos_x10;     /* freeze in place */
        a->moving     = false;
        a->cal_start_ticks = 0;
        s->do_[i * 2]     = false;      /* both pulse outputs off */
        s->do_[i * 2 + 1] = false;
    }
}

void orbit_gdc_task(void *args)
{
    (void)args;
    DebugP_log("[GDC] tick task starting (period %u ms)\r\n",
               (unsigned)ORBIT_GDC_TICK_MS);

    const TickType_t period = pdMS_TO_TICKS(ORBIT_GDC_TICK_MS);

    for (;;) {
        vTaskDelay(period);

        OrbitState_Lock();
        OrbitStateData *s = OrbitState_Get();
        if (!s->safe_mode) {
            uint32_t now = (uint32_t)xTaskGetTickCount();
            if (s->gdc.calibrating) gdc_tick_calibration(s, now);
            else                    gdc_tick_run(s);
        }
        /* Push s->do_[] to real GPIO outputs every tick — including
         * during safe-mode (OrbitGdc_EnforceSafeMode already cleared
         * s->do_[]) so the hardware always reflects the current bool
         * state. Channels without a syscfg pin are no-ops. */
        LpOrbitIo_PushOutputs(s);
        OrbitState_Unlock();
    }
}

/* --- HR dispatch -------------------------------------------------------- */

static uint8_t gdc_read_one_hr(const OrbitStateData *s, uint16_t addr,
                               uint16_t *out)
{
    const GdcState *g = &s->gdc;

    switch (addr) {
        case HR_GDC_DOOR_PCT:           *out = g->door_pct_x10;        return MB_EX_NONE;
        case HR_GDC_DEFAULT_TRAVEL_MS:  *out = g->default_travel_ms;   return MB_EX_NONE;
        case HR_GDC_ACTIVE_STAGE_COUNT: *out = g->active_stage_count;  return MB_EX_NONE;
        case HR_GDC_CALIBRATING:        *out = g->calibrating ? 1u : 0u; return MB_EX_NONE;
        case HR_GDC_CAL_PHASE:          *out = g->cal_phase;           return MB_EX_NONE;
        case HR_GDC_CAL_REQUEST:        *out = 0;                      return MB_EX_NONE;
        default: break;
    }
    if (addr >= HR_GDC_POS_BASE && addr < HR_GDC_POS_BASE + 5) {
        *out = g->act[addr - HR_GDC_POS_BASE].pos_x10;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_TARGET_BASE && addr < HR_GDC_TARGET_BASE + 5) {
        *out = g->act[addr - HR_GDC_TARGET_BASE].target_x10;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_STATUS_BASE && addr < HR_GDC_STATUS_BASE + 5) {
        const GdcActuator *a = &g->act[addr - HR_GDC_STATUS_BASE];
        uint16_t v = 0;
        if (a->moving)       v |= GDC_STATUS_MOVING;
        if (a->open_switch)  v |= GDC_STATUS_OPEN_SWITCH;
        if (a->close_switch) v |= GDC_STATUS_CLOSE_SWITCH;
        if (a->calibrated)   v |= GDC_STATUS_CALIBRATED;
        *out = v;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_STAGE_BASE && addr < HR_GDC_STAGE_BASE + 5) {
        *out = g->act[addr - HR_GDC_STAGE_BASE].stage;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_OPEN_MS_BASE && addr < HR_GDC_OPEN_MS_BASE + 5) {
        *out = g->act[addr - HR_GDC_OPEN_MS_BASE].open_travel_ms;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_CLOSE_MS_BASE && addr < HR_GDC_CLOSE_MS_BASE + 5) {
        *out = g->act[addr - HR_GDC_CLOSE_MS_BASE].close_travel_ms;
        return MB_EX_NONE;
    }
    if (addr >= 306 && addr < HR_GDC_POS_BASE) {
        /* reserved gap — read as zero, valid address. */
        *out = 0;
        return MB_EX_NONE;
    }
    return MB_EX_ILLEGAL_DATA_ADDRESS;
}

static uint8_t gdc_write_one_hr(OrbitStateData *s, uint16_t addr, uint16_t value,
                                uint32_t now_ticks)
{
    GdcState *g = &s->gdc;

    switch (addr) {
        case HR_GDC_DOOR_PCT:
            if (value > 1000) return MB_EX_ILLEGAL_DATA_VALUE;
            g->door_pct_x10 = value;
            return MB_EX_NONE;
        case HR_GDC_DEFAULT_TRAVEL_MS:
            g->default_travel_ms = value;
            return MB_EX_NONE;
        case HR_GDC_CAL_REQUEST:
            if (value == 1) gdc_handle_cal_request(s, now_ticks);
            return MB_EX_NONE;
        case HR_GDC_ACTIVE_STAGE_COUNT:
        case HR_GDC_CALIBRATING:
        case HR_GDC_CAL_PHASE:
            return MB_EX_ILLEGAL_DATA_ADDRESS;   /* read-only */
        default: break;
    }
    if (addr >= HR_GDC_POS_BASE && addr < HR_GDC_POS_BASE + 5) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;       /* RO */
    }
    if (addr >= HR_GDC_TARGET_BASE && addr < HR_GDC_TARGET_BASE + 5) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;       /* RO (driven by tick) */
    }
    if (addr >= HR_GDC_STATUS_BASE && addr < HR_GDC_STATUS_BASE + 5) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;       /* RO */
    }
    if (addr >= HR_GDC_STAGE_BASE && addr < HR_GDC_STAGE_BASE + 5) {
        if (value > 5) return MB_EX_ILLEGAL_DATA_VALUE;
        g->act[addr - HR_GDC_STAGE_BASE].stage = (uint8_t)value;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_OPEN_MS_BASE && addr < HR_GDC_OPEN_MS_BASE + 5) {
        GdcActuator *a = &g->act[addr - HR_GDC_OPEN_MS_BASE];
        a->open_travel_ms = value;
        if (value > 0 && a->close_travel_ms > 0) a->calibrated = true;
        if (value == 0) a->calibrated = false;
        return MB_EX_NONE;
    }
    if (addr >= HR_GDC_CLOSE_MS_BASE && addr < HR_GDC_CLOSE_MS_BASE + 5) {
        GdcActuator *a = &g->act[addr - HR_GDC_CLOSE_MS_BASE];
        a->close_travel_ms = value;
        if (value > 0 && a->open_travel_ms > 0) a->calibrated = true;
        if (value == 0) a->calibrated = false;
        return MB_EX_NONE;
    }
    if (addr >= 306 && addr < HR_GDC_POS_BASE) {
        /* reserved gap — accept silently. */
        return MB_EX_NONE;
    }
    return MB_EX_ILLEGAL_DATA_ADDRESS;
}

/* Returns true if `addr` is owned by the GDC map (i.e. should be served
 * by gdc_*_one_hr; otherwise the caller should delegate to storage_*). */
static inline bool gdc_owns_addr(uint16_t addr)
{
    return addr >= HR_GDC_BASE && addr < HR_GDC_END;
}

uint8_t OrbitGdc_ReadHrBlock(uint16_t start, uint16_t count, uint16_t *out)
{
    if (count == 0 || count > 125) return MB_EX_ILLEGAL_DATA_VALUE;

    /* Fast path: entire block lies outside GDC range — delegate. */
    uint16_t end = (uint16_t)(start + count);
    if (end <= HR_GDC_BASE || start >= HR_GDC_END) {
        return storage_read_hr_block(start, count, out);
    }

    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = MB_EX_NONE;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t addr = (uint16_t)(start + i);
        if (gdc_owns_addr(addr)) {
            exc = gdc_read_one_hr(s, addr, &out[i]);
        } else {
            /* One-off delegation to storage's per-address reader is not
             * exposed; fall back to a single-register block call. We
             * release the lock to honour storage_*'s own locking. */
            OrbitState_Unlock();
            uint16_t one = 0;
            exc = storage_read_hr_block(addr, 1, &one);
            out[i] = one;
            OrbitState_Lock();
            s = OrbitState_Get();
        }
        if (exc != MB_EX_NONE) break;
    }
    OrbitState_Unlock();
    return exc;
}

uint8_t OrbitGdc_WriteHrSingle(uint16_t addr, uint16_t value)
{
    if (!gdc_owns_addr(addr)) {
        return storage_write_hr_single(addr, value);
    }
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint32_t now = (uint32_t)xTaskGetTickCount();
    uint8_t exc = gdc_write_one_hr(s, addr, value, now);
    OrbitState_Unlock();
    return exc;
}

uint8_t OrbitGdc_WriteHrBlock(uint16_t start, uint16_t count, const uint16_t *vals)
{
    if (count == 0 || count > 123) return MB_EX_ILLEGAL_DATA_VALUE;

    uint16_t end = (uint16_t)(start + count);
    if (end <= HR_GDC_BASE || start >= HR_GDC_END) {
        return storage_write_hr_block(start, count, vals);
    }

    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint32_t now = (uint32_t)xTaskGetTickCount();
    uint8_t exc = MB_EX_NONE;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t addr = (uint16_t)(start + i);
        if (gdc_owns_addr(addr)) {
            exc = gdc_write_one_hr(s, addr, vals[i], now);
        } else {
            OrbitState_Unlock();
            exc = storage_write_hr_single(addr, vals[i]);
            OrbitState_Lock();
            s = OrbitState_Get();
        }
        if (exc != MB_EX_NONE) break;
    }
    OrbitState_Unlock();
    return exc;
}
