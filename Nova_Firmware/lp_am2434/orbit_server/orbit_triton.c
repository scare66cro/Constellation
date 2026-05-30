/*
 * orbit_triton.c — TRITON refrigeration role implementation.
 *
 * See orbit_triton.h for the HR map, DO assignments, cold-boot semantics,
 * safe-mode policy, and the list of features explicitly deferred to
 * follow-up patches.
 *
 * Math notes:
 *   - All wire values are int16 fixed-point: temperatures °F×10,
 *     pressures PSI×10. Internal control math uses float — refrigerant
 *     P↔T polynomials need it and the AM2432 R5F has a hardware FPU.
 *   - The refrigerant tables (P_sat / T_sat polynomials and pressure
 *     defaults) are a verbatim port from
 *     orbit-simulator/src/tritonControl.ts which itself lifted them
 *     from the legacy GRC `Refrigerant.c`. R-22, R-134A, R-404A,
 *     R-407A, R-407C, R-410A, R-507 are supported; any other Triton
 *     refrigerant index falls back to R-404A (closest HFC analog).
 */

#include "orbit_triton.h"
#include "orbit_storage.h"
#include "orbit_state.h"

#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/DebugP.h>

#include <string.h>
#include <stdint.h>

/* --- refrigerant tables (from tritonControl.ts) ------------------------ */

/* GRC refrigerant enum (R22=0, R410A=1, R407C=2, R134A=3, R404A=4,
 * R507=5, R407A=6). Triton enum maps via triton_to_grc(). */
#define GRC_R_FALLBACK 4 /* R-404A */

static int triton_to_grc(uint8_t triton)
{
    switch (triton) {
        case 0: return 0; /* R22   */
        case 1: return 3; /* R134A */
        case 2: return 4; /* R404A */
        case 3: return 6; /* R407A */
        case 4: return 2; /* R407C */
        case 5: return 1; /* R410A */
        default: return -1; /* fallback to R-404A */
    }
}

/* P(t) = a t^3 + b t^2 + c t + d  (t in °F, P in PSI). */
static const float P_COEFF[7][4] = {
    /* R_22    */ { 2.87064e-5f, 6.104444e-3f, 0.821491477f, 24.04855744f },
    /* R_410A  */ { 4.54707e-5f, 9.520674e-3f, 1.278976335f, 48.66722633f },
    /* R_407C  */ { 3.49531e-5f, 6.717706e-3f, 0.940156102f, 29.99215385f },
    /* R_134A  */ { 2.56407e-5f, 4.204094e-3f, 0.500317987f,  6.628019008f },
    /* R_404A  */ { 3.39324e-5f, 7.142816e-3f, 0.968937731f, 32.11551756f },
    /* R_507   */ { 4.35828e-5f, 5.933340e-3f, 1.053624172f, 35.44995267f },
    /* R_407A  */ { 3.57635e-5f, 6.973933e-3f, 0.997833893f, 32.30318463f },
};

/* T(P) = a4 P^4 + a3 P^3 + a2 P^2 + a1 P + b  (P in PSI, T in °F).
 * Vapor-side curves only (we don't expose subcooling/liquid curves yet). */
static const float T_COEFF[7][5] = {
    /* R_22    */ { -1.23077e-8f, 1.27346e-5f, -0.005152426f, 1.255366373f, -26.05491894f },
    /* R_410A  */ { -2.02333e-9f, 3.38782e-6f, -0.002212261f, 0.851125696f, -35.23515217f },
    /* R_407C  */ { -1.11101e-8f, 1.20528e-5f, -0.005030362f, 1.220283896f, -20.4302572f  },
    /* R_134A  */ { -6.00652e-8f, 4.11758e-5f, -0.010866345f, 1.702829601f,  -8.723462409f },
    /* R_404A  */ { -6.33033e-9f, 7.82539e-6f, -0.003782307f, 1.092220235f, -30.08415286f },
    /* R_507   */ { -5.0235e-9f,  6.59017e-6f, -0.003409960f, 1.053080767f, -31.94709005f },
    /* R_407A  */ { -8.79238e-9f, 1.00055e-5f, -0.004420016f, 1.148444786f, -22.10834435f },
};

float OrbitTriton_PsatF(uint8_t tritonRef, float tempF)
{
    int idx = triton_to_grc(tritonRef);
    if (idx < 0 || idx > 6) idx = GRC_R_FALLBACK;
    const float *c = P_COEFF[idx];
    float t = tempF, t2 = t * t;
    return c[0] * t2 * t + c[1] * t2 + c[2] * t + c[3];
}

float OrbitTriton_TsatF(uint8_t tritonRef, float pressurePSI)
{
    int idx = triton_to_grc(tritonRef);
    if (idx < 0 || idx > 6) idx = GRC_R_FALLBACK;
    const float *c = T_COEFF[idx];
    float p = pressurePSI, p2 = p * p;
    return c[0] * p2 * p2 + c[1] * p2 * p + c[2] * p2 + c[3] * p + c[4];
}

/* --- alarm bitmap helpers --------------------------------------------- */

static void alarm_set(TritonState *t, uint8_t bit, bool active)
{
    uint8_t reg = bit / 16U;
    uint16_t mask = (uint16_t)(1u << (bit % 16U));
    if (reg >= ORBIT_TRITON_ALARM_REGS) return;
    if (active) {
        t->alarm_active[reg] |= mask;
    } else {
        /* Clear active; if previously acked, also clear ack so the next
         * occurrence can re-fire and require fresh acknowledgement. */
        if (t->alarm_active[reg] & mask) {
            t->alarm_active[reg] &= (uint16_t)~mask;
            if (t->alarm_acked[reg] & mask) {
                t->alarm_acked[reg] &= (uint16_t)~mask;
            }
        }
    }
}

static bool alarm_is_active(const TritonState *t, uint8_t bit)
{
    uint8_t reg = bit / 16U;
    uint16_t mask = (uint16_t)(1u << (bit % 16U));
    if (reg >= ORBIT_TRITON_ALARM_REGS) return false;
    return (t->alarm_active[reg] & mask) != 0;
}

static void alarm_ack_all(TritonState *t)
{
    /* Per the GRC behaviour: ack-all sets acked := active. Inactive bits
     * with acked set are pruned (already done lazily in alarm_set). And
     * latched safe_off_mask clears so the compressor can restart once
     * the underlying condition has cleared. */
    for (uint8_t i = 0; i < ORBIT_TRITON_ALARM_REGS; i++) {
        t->alarm_acked[i] = t->alarm_active[i];
    }
    t->safe_off_mask = 0;
    /* Clear all per-sensor failure latches so the trip timer can
     * re-arm. Threshold-based alarms (SUC_LOW_P etc.) will re-latch
     * naturally on the next tick if the condition persists. */
    for (uint8_t i = 0; i < ORBIT_TRITON_NUM_SENSORS; i++) {
        t->failure[i].over_ms = 0;
        t->failure[i].tripped = false;
    }
}

/* --- per-sensor failure-mode timer ------------------------------------ */

/* Maps sensor index → FAIL_* alarm bit (TS TRITON_ALARM_BITS). */
static const uint8_t SENS_TO_FAIL_BIT[ORBIT_TRITON_NUM_SENSORS] = {
    TRITON_ALARM_FAIL_SUCTION,      /* SUC_P  */
    TRITON_ALARM_FAIL_DISCHARGE,    /* DIS_P  */
    TRITON_ALARM_FAIL_OIL,          /* OIL_P  */
    TRITON_ALARM_FAIL_SUPERHEAT,    /* SUC_T  — accumulated as superheat-high */
    TRITON_ALARM_FAIL_DISCHARGE,    /* DIS_T  — same code as DIS_P (high temp) */
    TRITON_ALARM_FAIL_OIL,          /* LLS_T  — reuse OIL bit until distinct */
    TRITON_ALARM_FAIL_OUTSIDE_AIR,  /* AMB_T  */
};

/* Maps sensor index → SENS_FAULT_* alarm bit (validity, not value). */
static const uint8_t SENS_TO_FAULT_BIT[ORBIT_TRITON_NUM_SENSORS] = {
    TRITON_ALARM_SENS_FAULT_SUC_P,
    TRITON_ALARM_SENS_FAULT_DIS_P,
    TRITON_ALARM_SENS_FAULT_OIL_P,
    TRITON_ALARM_SENS_FAULT_SUC_T,
    TRITON_ALARM_SENS_FAULT_DIS_T,
    TRITON_ALARM_SENS_FAULT_LLS_T,
    TRITON_ALARM_SENS_FAULT_AMB_T,
};

/* Pull live sensor values from the shared sensor_block (board 0 = temp,
 * board 1 = pressure). See orbit_triton.h "Sensor source convention".
 * Layout — sensor index → sensor_block slot:
 *   SUC_P → 4, DIS_P → 5, OIL_P → 6,
 *   SUC_T → 0, DIS_T → 1, LLS_T → 2, AMB_T → 3
 *   demand_pct → sensor_block[7] / 10  (board1 ch3 stored ×10) */
static const uint8_t SENS_TO_BLOCK[ORBIT_TRITON_NUM_SENSORS] = {
    4U, 5U, 6U,   /* pressures   */
    0U, 1U, 2U, 3U /* temperatures */
};

static void pull_sensors(OrbitStateData *s)
{
    TritonState *t = &s->triton;
    for (uint8_t i = 0; i < ORBIT_TRITON_NUM_SENSORS; i++) {
        int16_t v = s->sensor_block[SENS_TO_BLOCK[i]];
        bool valid = (v != ORBIT_SENSOR_VAL_UNDEF);
        t->sensor_value_x10[i] = v;
        t->sensor_valid[i]     = valid;
    }
    int16_t demand_x10 = s->sensor_block[7];
    if (demand_x10 == ORBIT_SENSOR_VAL_UNDEF) {
        t->demand_pct = 0;
    } else {
        int v = demand_x10 / 10;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        t->demand_pct = (uint8_t)v;
    }
}

static void run_failure_timers(TritonState *t, uint32_t dt_ms)
{
    for (uint8_t i = 0; i < ORBIT_TRITON_NUM_SENSORS; i++) {
        TritonFailureCh *fc = &t->failure[i];
        bool sensor_invalid = !t->sensor_valid[i];

        /* Sensor-validity alarm latches immediately (instantaneous).
         * No timer — bridge sees the SENS_FAULT_* bit on the very first
         * undef sample and can react. */
        alarm_set(t, SENS_TO_FAULT_BIT[i], sensor_invalid);

        if (sensor_invalid) {
            uint32_t was = fc->over_ms;
            fc->over_ms += dt_ms;
            if (fc->over_ms < was) fc->over_ms = 0xFFFFFFFFu; /* saturate */
            uint32_t threshold_ms = (uint32_t)fc->delay_sec * 1000U;
            if (!fc->tripped && fc->over_ms >= threshold_ms) {
                fc->tripped = true;
                /* Only ALARM_ONLY (0) and SAFE_OFF (1) latch the alarm;
                 * RUN_THROUGH (2) suppresses both the alarm and the
                 * safe-off mask (operator opt-out for known bad sensors). */
                if (fc->mode == 0U || fc->mode == 1U) {
                    alarm_set(t, SENS_TO_FAIL_BIT[i], true);
                }
                if (fc->mode == 1U) {
                    t->safe_off_mask |= ((uint32_t)1u << SENS_TO_FAIL_BIT[i]);
                }
            }
        } else {
            /* Sensor recovered. Reset the trip timer so a future fault
             * needs a fresh delay window before re-latching. The latched
             * FAIL_* alarm and safe_off_mask bit stay until ack-all so
             * the operator sees the historical fault. */
            fc->over_ms = 0;
        }
    }
}

/* --- threshold alarms (instantaneous; TS §6) -------------------------- */

static void run_threshold_alarms(TritonState *t)
{
    /* Suction low/high — reuse cutOutP / discHighUnloadP as alarm bands.
     * We don't have per-sensor lo/hi alarm thresholds in the HR map yet
     * (would consume 28 more setpoint regs); defer to a future revision
     * where the bridge can program them. For now use the pressure-unload
     * thresholds as a reasonable proxy. */
    if (t->sensor_valid[TRITON_SENS_SUC_P]) {
        bool low  = t->sensor_value_x10[TRITON_SENS_SUC_P] < t->cut_out_p_x10;
        alarm_set(t, TRITON_ALARM_SUC_LOW_P, low && t->compressor_on);
    }
    if (t->sensor_valid[TRITON_SENS_DIS_P] && t->disc_high_unload_p_x10 > 0) {
        bool high = t->sensor_value_x10[TRITON_SENS_DIS_P] >= t->disc_high_unload_p_x10;
        alarm_set(t, TRITON_ALARM_DIS_HIGH_P, high);
    }
    if (t->sensor_valid[TRITON_SENS_OIL_P] && t->compressor_on) {
        /* Crude oil-low: < 20 PSI while running. The TS sim has a per-
         * sensor lowAlarm field for this; until we expose it on the wire,
         * use a conservative literal. */
        bool low = t->sensor_value_x10[TRITON_SENS_OIL_P] < 200;
        alarm_set(t, TRITON_ALARM_OIL_LOW_P, low);
    }
}

/* --- compressor + unloader state machine ------------------------------ */

static void update_compressor(OrbitStateData *s, uint32_t dt_ms)
{
    TritonState *t = &s->triton;

    bool hard_off = !t->enabled || t->safe_off_mask != 0;
    bool sucP_ok  = t->sensor_valid[TRITON_SENS_SUC_P];
    int16_t sucP  = t->sensor_value_x10[TRITON_SENS_SUC_P];

    bool desired = t->compressor_on;

    if (hard_off) {
        desired = false;
    } else if (t->compressor_on) {
        /* Running — auto cut-out if suction drops at/below cutOutP. */
        if (sucP_ok && sucP <= t->cut_out_p_x10) {
            desired = false;
        }
    } else {
        /* Off — anti-short-cycle gate then cut-in. */
        bool asc_met = t->off_ms >= (uint32_t)t->min_off_time_sec * 1000U;
        if (asc_met && sucP_ok && sucP >= t->cut_in_p_x10
            && t->cut_in_p_x10 > 0)
        {
            desired = true;
        }
    }

    if (desired != t->compressor_on) {
        t->compressor_on = desired;
        if (!desired) t->off_ms = 0;
    }
    if (!t->compressor_on) {
        uint32_t was = t->off_ms;
        t->off_ms += dt_ms;
        if (t->off_ms < was) t->off_ms = 0xFFFFFFFFu;
        t->compressor_runtime_sec = 0;
    } else {
        /* Accumulate seconds; carry partial ms across ticks via off_ms
         * doubled-purpose isn't safe — keep it simple with per-tick
         * integer seconds. */
        static uint32_t s_run_ms = 0; /* per-task local; safe because
                                          this fn runs only from the tick
                                          task while holding the lock. */
        s_run_ms += dt_ms;
        while (s_run_ms >= 1000U) {
            s_run_ms -= 1000U;
            t->compressor_runtime_sec++;
        }
    }

    /* Status — coarse mapping (deferred features collapsed). */
    if (t->safe_off_mask != 0)        t->compressor_status = TRITON_STATUS_ERROR;
    else if (!t->enabled)             t->compressor_status = TRITON_STATUS_SWITCH_OFF;
    else if (s->safe_mode)            t->compressor_status = TRITON_STATUS_SYSTEM_OFF;
    else if (t->compressor_on)        t->compressor_status = TRITON_STATUS_AUTO_RUN;
    else                              t->compressor_status = TRITON_STATUS_AUTO_STANDBY;
}

static void update_unloaders(OrbitStateData *s)
{
    TritonState *t = &s->triton;

    bool sucP_ok = t->sensor_valid[TRITON_SENS_SUC_P];
    bool disP_ok = t->sensor_valid[TRITON_SENS_DIS_P];
    int16_t sucP = t->sensor_value_x10[TRITON_SENS_SUC_P];
    int16_t disP = t->sensor_value_x10[TRITON_SENS_DIS_P];
    uint8_t demand = t->demand_pct;

    uint8_t cap_stage = 0;
    for (uint8_t i = 0; i < ORBIT_TRITON_NUM_UNLOADERS; i++) {
        bool was_on = t->unloader_on[i];
        bool desired = was_on;

        /* HP override latch — discharge climbs past unload threshold,
         * latch off; clear when it falls below load threshold. */
        if (disP_ok) {
            if (!t->unloader_hp_forced[i]
                && t->unloader_hp_unload_psi_x10[i] > 0
                && disP >= t->unloader_hp_unload_psi_x10[i]) {
                t->unloader_hp_forced[i] = true;
            } else if (t->unloader_hp_forced[i]
                       && disP <= t->unloader_hp_load_psi_x10[i]) {
                t->unloader_hp_forced[i] = false;
            }
        }
        /* LP override latch — suction at/below unload threshold, latch
         * off; clear above load threshold. Protects compressor from
         * starvation regardless of demand. */
        if (sucP_ok) {
            if (!t->unloader_lp_forced[i]
                && t->unloader_lp_unload_psi_x10[i] > 0
                && sucP <= t->unloader_lp_unload_psi_x10[i]) {
                t->unloader_lp_forced[i] = true;
            } else if (t->unloader_lp_forced[i]
                       && sucP >= t->unloader_lp_load_psi_x10[i]) {
                t->unloader_lp_forced[i] = false;
            }
        }

        bool overridden = t->unloader_hp_forced[i] || t->unloader_lp_forced[i];

        if (!t->compressor_on || overridden || t->safe_off_mask != 0) {
            desired = false;
        } else if (was_on) {
            if (demand <= t->unloader_off_pct[i]) desired = false;
        } else {
            if (demand >= t->unloader_on_pct[i]) desired = true;
        }
        t->unloader_on[i] = desired;
        if (desired) cap_stage++;
    }
    t->capacity_stage = cap_stage;
}

/* --- DO drive --------------------------------------------------------- */

static void drive_outputs(OrbitStateData *s)
{
    TritonState *t = &s->triton;

    s->do_[TRITON_DO_COMPRESSOR] = t->compressor_on;
    s->do_[TRITON_DO_OIL_PUMP]   = t->compressor_on;
    s->do_[TRITON_DO_EVAP_FAN]   = t->compressor_on;

    /* Unloader contactor polarity: NO → DO=loaded; NC → DO=unloaded.
     * Capacity is "loaded" when unloader_on[i] is true (the SM treats
     * "load" as adding capacity), so for NC polarity we invert. */
    bool inv = (t->unloader_normal != 0U);
    s->do_[TRITON_DO_UNLOADER1] = inv ? !t->unloader_on[0] : t->unloader_on[0];
    s->do_[TRITON_DO_UNLOADER2] = inv ? !t->unloader_on[1] : t->unloader_on[1];

    /* Reserved DO[5..9] left untouched — operator could repurpose via
     * Modbus FC05 if a future site wires anything there. */
}

/* --- saturation/superheat derivation (read-only outputs) -------------- */

static void update_derived(TritonState *t)
{
    if (t->sensor_valid[TRITON_SENS_SUC_P]) {
        float sucP = (float)t->sensor_value_x10[TRITON_SENS_SUC_P] / 10.0f;
        float sat  = OrbitTriton_TsatF(t->refrigerant_type, sucP);
        int   x10  = (int)(sat * 10.0f + (sat >= 0 ? 0.5f : -0.5f));
        if (x10 >  32767) x10 =  32767;
        if (x10 < -32768) x10 = -32768;
        t->suction_sat_t_f_x10 = (int16_t)x10;
        if (t->sensor_valid[TRITON_SENS_SUC_T]) {
            int sh = t->sensor_value_x10[TRITON_SENS_SUC_T] - t->suction_sat_t_f_x10;
            if (sh >  32767) sh =  32767;
            if (sh < -32768) sh = -32768;
            t->superheat_f_x10 = (int16_t)sh;
        }
    }
}

/* --- public API ------------------------------------------------------- */

void OrbitTriton_Init(void)
{
    /* Cold-boot defaults are seeded by OrbitState_Init (failure modes
     * to SAFE_OFF; everything else zero). Symmetric hook for main.c. */
}

void OrbitTriton_EnforceSafeMode(void)
{
    /* Caller (orbit_safemode_task) holds OrbitState_Lock. */
    OrbitStateData *s = OrbitState_Get();
    TritonState *t = &s->triton;

    t->compressor_on        = false;
    t->compressor_status    = TRITON_STATUS_SYSTEM_OFF;
    t->capacity_stage       = 0;
    t->pumpdown_sec_remaining = 0;
    for (uint8_t i = 0; i < ORBIT_TRITON_NUM_UNLOADERS; i++) {
        t->unloader_on[i]        = false;
        /* Override latches preserved — they reflect real sensor state. */
    }
    /* Drive DOs to safe state — see orbit_triton.h "Safe-mode policy". */
    s->do_[TRITON_DO_COMPRESSOR] = false;
    s->do_[TRITON_DO_OIL_PUMP]   = false;
    s->do_[TRITON_DO_EVAP_FAN]   = false;
    /* Unloader contactors land de-energized regardless of polarity —
     * for both NO and NC, "DO=false" is the field-safe state on power
     * loss / comm loss. */
    s->do_[TRITON_DO_UNLOADER1]  = false;
    s->do_[TRITON_DO_UNLOADER2]  = false;
}

void orbit_triton_task(void *args)
{
    (void)args;
    DebugP_log("[TRITON] tick task starting (period %u ms)\r\n",
               (unsigned)ORBIT_TRITON_TICK_MS);
    const TickType_t period = pdMS_TO_TICKS(ORBIT_TRITON_TICK_MS);

    for (;;) {
        vTaskDelay(period);

        OrbitState_Lock();
        OrbitStateData *s = OrbitState_Get();
        if (!s->safe_mode) {
            pull_sensors(s);
            run_failure_timers(&s->triton, ORBIT_TRITON_TICK_MS);
            update_derived(&s->triton);
            update_compressor(s, ORBIT_TRITON_TICK_MS);
            update_unloaders(s);
            run_threshold_alarms(&s->triton);
            drive_outputs(s);
        }
        OrbitState_Unlock();
    }
}

/* --- HR dispatch ------------------------------------------------------ */

static inline bool triton_owns_addr(uint16_t addr)
{
    return (addr >= HR_TRITON_SP_BASE && addr < HR_TRITON_END);
}

/* Pack/unpack helpers for the live-sensor / live-equipment views. */
static uint8_t triton_read_one_hr(const OrbitStateData *s, uint16_t addr,
                                  uint16_t *out)
{
    const TritonState *t = &s->triton;

    /* SETPOINTS 400..426 */
    if (addr >= HR_TRITON_SP_BASE && addr < HR_TRITON_SP_END) {
        switch (addr) {
            case 400: *out = t->enabled ? 1u : 0u; return MB_EX_NONE;
            case 401: *out = t->refrigerant_type;  return MB_EX_NONE;
            case 402: *out = (uint16_t)t->cut_in_p_x10;            return MB_EX_NONE;
            case 403: *out = (uint16_t)t->cut_out_p_x10;           return MB_EX_NONE;
            case 404: *out = t->min_off_time_sec;                  return MB_EX_NONE;
            case 405: *out = t->min_runtime_sec;                   return MB_EX_NONE;
            case 406: *out = t->warm_up_sec;                       return MB_EX_NONE;
            case 407: *out = t->power_fail_min;                    return MB_EX_NONE;
            case 408: *out = (uint16_t)t->low_ambient_cutout_f_x10; return MB_EX_NONE;
            case 409: *out = t->pump_down_mode;                    return MB_EX_NONE;
            case 410: *out = (uint16_t)t->disc_high_unload_p_x10;  return MB_EX_NONE;
            case 411: *out = (uint16_t)t->suc_low_unload_p_x10;    return MB_EX_NONE;
            case 412: *out = (uint16_t)t->superheat_target_f_x10;  return MB_EX_NONE;
            case 413: *out = (uint16_t)t->superheat_low_f_x10;     return MB_EX_NONE;
            case 414: *out = t->unloader_on_pct[0];                return MB_EX_NONE;
            case 415: *out = t->unloader_on_pct[1];                return MB_EX_NONE;
            case 416: *out = t->unloader_off_pct[0];               return MB_EX_NONE;
            case 417: *out = t->unloader_off_pct[1];               return MB_EX_NONE;
            case 418: *out = (uint16_t)t->unloader_hp_unload_psi_x10[0]; return MB_EX_NONE;
            case 419: *out = (uint16_t)t->unloader_hp_unload_psi_x10[1]; return MB_EX_NONE;
            case 420: *out = (uint16_t)t->unloader_hp_load_psi_x10[0];   return MB_EX_NONE;
            case 421: *out = (uint16_t)t->unloader_hp_load_psi_x10[1];   return MB_EX_NONE;
            case 422: *out = (uint16_t)t->unloader_lp_unload_psi_x10[0]; return MB_EX_NONE;
            case 423: *out = (uint16_t)t->unloader_lp_unload_psi_x10[1]; return MB_EX_NONE;
            case 424: *out = (uint16_t)t->unloader_lp_load_psi_x10[0];   return MB_EX_NONE;
            case 425: *out = (uint16_t)t->unloader_lp_load_psi_x10[1];   return MB_EX_NONE;
            case 426: *out = t->unloader_normal;                   return MB_EX_NONE;
            default:  *out = 0;                                    return MB_EX_NONE; /* reserved */
        }
    }
    /* FAILURE MODES 440..453 */
    if (addr >= HR_TRITON_FAIL_BASE && addr < HR_TRITON_FAIL_END) {
        uint16_t off = addr - HR_TRITON_FAIL_BASE;
        uint8_t  i   = (uint8_t)(off / 2U);
        bool     is_delay = (off & 1U) != 0;
        if (i >= ORBIT_TRITON_NUM_SENSORS) return MB_EX_ILLEGAL_DATA_ADDRESS;
        *out = is_delay ? t->failure[i].delay_sec : (uint16_t)t->failure[i].mode;
        return MB_EX_NONE;
    }
    /* LIVE SENSORS 460..473 */
    if (addr >= HR_TRITON_LIVE_SENS_BASE && addr < HR_TRITON_LIVE_SENS_END) {
        uint16_t off = addr - HR_TRITON_LIVE_SENS_BASE;
        uint8_t  i   = (uint8_t)(off / 2U);
        bool     is_valid = (off & 1U) != 0;
        if (i >= ORBIT_TRITON_NUM_SENSORS) return MB_EX_ILLEGAL_DATA_ADDRESS;
        *out = is_valid ? (t->sensor_valid[i] ? 1u : 0u)
                        : (uint16_t)t->sensor_value_x10[i];
        return MB_EX_NONE;
    }
    /* LIVE EQUIPMENT 480..493 */
    if (addr >= HR_TRITON_LIVE_EQ_BASE && addr < HR_TRITON_LIVE_EQ_END) {
        switch (addr) {
            case 480: *out = t->compressor_on ? 1u : 0u;            return MB_EX_NONE;
            case 481: *out = t->compressor_status;                  return MB_EX_NONE;
            case 482: *out = t->capacity_stage;                     return MB_EX_NONE;
            case 483: {
                uint16_t v = 0;
                if (t->unloader_on[0]) v |= 0x1u;
                if (t->unloader_on[1]) v |= 0x2u;
                *out = v; return MB_EX_NONE;
            }
            case 484: {
                uint16_t v = 0;
                if (t->unloader_hp_forced[0]) v |= 0x1u;
                if (t->unloader_hp_forced[1]) v |= 0x2u;
                *out = v; return MB_EX_NONE;
            }
            case 485: {
                uint16_t v = 0;
                if (t->unloader_lp_forced[0]) v |= 0x1u;
                if (t->unloader_lp_forced[1]) v |= 0x2u;
                *out = v; return MB_EX_NONE;
            }
            case 486: *out = (uint16_t)(t->compressor_runtime_sec & 0xFFFFu);       return MB_EX_NONE;
            case 487: *out = (uint16_t)((t->compressor_runtime_sec >> 16) & 0xFFFFu); return MB_EX_NONE;
            case 488: *out = t->pumpdown_sec_remaining;             return MB_EX_NONE;
            case 489: *out = t->demand_pct;                         return MB_EX_NONE;
            case 490: *out = (uint16_t)t->suction_sat_t_f_x10;      return MB_EX_NONE;
            case 491: *out = (uint16_t)t->superheat_f_x10;          return MB_EX_NONE;
            case 492: *out = (uint16_t)(t->safe_off_mask & 0xFFFFu);       return MB_EX_NONE;
            case 493: *out = (uint16_t)((t->safe_off_mask >> 16) & 0xFFFFu); return MB_EX_NONE;
            default:  *out = 0; return MB_EX_NONE;
        }
    }
    /* ALARM bitmaps 530..541, 542 ack */
    if (addr >= HR_TRITON_ALARM_ACTIVE_BASE
        && addr < HR_TRITON_ALARM_ACTIVE_BASE + ORBIT_TRITON_ALARM_REGS) {
        *out = t->alarm_active[addr - HR_TRITON_ALARM_ACTIVE_BASE];
        return MB_EX_NONE;
    }
    if (addr >= HR_TRITON_ALARM_ACKED_BASE
        && addr < HR_TRITON_ALARM_ACKED_BASE + ORBIT_TRITON_ALARM_REGS) {
        *out = t->alarm_acked[addr - HR_TRITON_ALARM_ACKED_BASE];
        return MB_EX_NONE;
    }
    if (addr == HR_TRITON_ACK_CMD) { *out = 0; return MB_EX_NONE; }

    /* Reserved gaps inside the Triton range — read as zero (valid). */
    *out = 0;
    return MB_EX_NONE;
}

static uint8_t triton_write_one_hr(OrbitStateData *s, uint16_t addr, uint16_t value)
{
    TritonState *t = &s->triton;

    if (addr >= HR_TRITON_SP_BASE && addr < HR_TRITON_SP_END) {
        switch (addr) {
            case 400: t->enabled = (value != 0); return MB_EX_NONE;
            case 401:
                if (value > 255) return MB_EX_ILLEGAL_DATA_VALUE;
                t->refrigerant_type = (uint8_t)value; return MB_EX_NONE;
            case 402: t->cut_in_p_x10  = (int16_t)value; return MB_EX_NONE;
            case 403: t->cut_out_p_x10 = (int16_t)value; return MB_EX_NONE;
            case 404: t->min_off_time_sec = value; return MB_EX_NONE;
            case 405: t->min_runtime_sec  = value; return MB_EX_NONE;
            case 406: t->warm_up_sec      = value; return MB_EX_NONE;
            case 407: t->power_fail_min   = value; return MB_EX_NONE;
            case 408: t->low_ambient_cutout_f_x10 = (int16_t)value; return MB_EX_NONE;
            case 409:
                if (value > 3) return MB_EX_ILLEGAL_DATA_VALUE;
                t->pump_down_mode = (uint8_t)value; return MB_EX_NONE;
            case 410: t->disc_high_unload_p_x10 = (int16_t)value; return MB_EX_NONE;
            case 411: t->suc_low_unload_p_x10   = (int16_t)value; return MB_EX_NONE;
            case 412: t->superheat_target_f_x10 = (int16_t)value; return MB_EX_NONE;
            case 413: t->superheat_low_f_x10    = (int16_t)value; return MB_EX_NONE;
            case 414: case 415:
                if (value > 100) return MB_EX_ILLEGAL_DATA_VALUE;
                t->unloader_on_pct[addr - 414] = (uint8_t)value; return MB_EX_NONE;
            case 416: case 417:
                if (value > 100) return MB_EX_ILLEGAL_DATA_VALUE;
                t->unloader_off_pct[addr - 416] = (uint8_t)value; return MB_EX_NONE;
            case 418: case 419:
                t->unloader_hp_unload_psi_x10[addr - 418] = (int16_t)value; return MB_EX_NONE;
            case 420: case 421:
                t->unloader_hp_load_psi_x10[addr - 420] = (int16_t)value; return MB_EX_NONE;
            case 422: case 423:
                t->unloader_lp_unload_psi_x10[addr - 422] = (int16_t)value; return MB_EX_NONE;
            case 424: case 425:
                t->unloader_lp_load_psi_x10[addr - 424] = (int16_t)value; return MB_EX_NONE;
            case 426:
                if (value > 1) return MB_EX_ILLEGAL_DATA_VALUE;
                t->unloader_normal = (uint8_t)value; return MB_EX_NONE;
            default: return MB_EX_NONE; /* reserved gap */
        }
    }
    if (addr >= HR_TRITON_FAIL_BASE && addr < HR_TRITON_FAIL_END) {
        uint16_t off = addr - HR_TRITON_FAIL_BASE;
        uint8_t  i   = (uint8_t)(off / 2U);
        bool     is_delay = (off & 1U) != 0;
        if (i >= ORBIT_TRITON_NUM_SENSORS) return MB_EX_ILLEGAL_DATA_ADDRESS;
        if (is_delay) {
            t->failure[i].delay_sec = value;
        } else {
            if (value > 2) return MB_EX_ILLEGAL_DATA_VALUE;
            t->failure[i].mode = (uint8_t)value;
        }
        return MB_EX_NONE;
    }
    /* Live regions are RO. */
    if (addr >= HR_TRITON_LIVE_SENS_BASE && addr < HR_TRITON_LIVE_SENS_END) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;
    }
    if (addr >= HR_TRITON_LIVE_EQ_BASE && addr < HR_TRITON_LIVE_EQ_END) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;
    }
    if (addr >= HR_TRITON_ALARM_ACTIVE_BASE
        && addr < HR_TRITON_ALARM_ACTIVE_BASE + ORBIT_TRITON_ALARM_REGS) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;
    }
    if (addr >= HR_TRITON_ALARM_ACKED_BASE
        && addr < HR_TRITON_ALARM_ACKED_BASE + ORBIT_TRITON_ALARM_REGS) {
        return MB_EX_ILLEGAL_DATA_ADDRESS;
    }
    if (addr == HR_TRITON_ACK_CMD) {
        if (value == 1) alarm_ack_all(t);
        return MB_EX_NONE;
    }
    /* Reserved gap inside Triton range — accept silently. */
    return MB_EX_NONE;
}

uint8_t OrbitTriton_ReadHrBlock(uint16_t start, uint16_t count, uint16_t *out)
{
    if (count == 0 || count > 125) return MB_EX_ILLEGAL_DATA_VALUE;

    uint16_t end = (uint16_t)(start + count);
    if (end <= HR_TRITON_SP_BASE || start >= HR_TRITON_END) {
        return storage_read_hr_block(start, count, out);
    }

    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = MB_EX_NONE;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t addr = (uint16_t)(start + i);
        if (triton_owns_addr(addr)) {
            exc = triton_read_one_hr(s, addr, &out[i]);
        } else {
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

uint8_t OrbitTriton_WriteHrSingle(uint16_t addr, uint16_t value)
{
    if (!triton_owns_addr(addr)) {
        return storage_write_hr_single(addr, value);
    }
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = triton_write_one_hr(s, addr, value);
    OrbitState_Unlock();
    return exc;
}

uint8_t OrbitTriton_WriteHrBlock(uint16_t start, uint16_t count, const uint16_t *vals)
{
    if (count == 0 || count > 123) return MB_EX_ILLEGAL_DATA_VALUE;

    uint16_t end = (uint16_t)(start + count);
    if (end <= HR_TRITON_SP_BASE || start >= HR_TRITON_END) {
        return storage_write_hr_block(start, count, vals);
    }

    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = MB_EX_NONE;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t addr = (uint16_t)(start + i);
        if (triton_owns_addr(addr)) {
            exc = triton_write_one_hr(s, addr, vals[i]);
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
