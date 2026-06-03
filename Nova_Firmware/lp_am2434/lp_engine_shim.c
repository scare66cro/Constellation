/* ============================================================================
 * lp_engine_shim.c — adapter between LP-AM2434 runtime and the
 * Nova-native AS2 equipment engine (nova_states.c, nova_modes.c,
 * nova_failures.c, nova_warnings.c, nova_timer.c, nova_serialshift.c,
 * nova_pwm.c).
 *
 * Phase A1 (May 2026) — read-only mode display.
 *   - Engine runs on a 1 s tick from main.c via lp_engine_tick().
 *   - Settings shadow is populated from LpSettings + latest sensor
 *     sample on every tick. (Cheap; tens of writes, no allocation.)
 *   - IoBoard[MAIN].InputState is built from the operator's per-equip
 *     AUTO/OFF/MANUAL state in LpSettings.remote_off — Constellation
 *     does not have AS2's physical FAN/REFRIG/HEAT panel switches; the
 *     "soft switch" semantics are derived from Equipment Control page
 *     state instead.
 *   - OutputOn/OutputOff (provided by nova_serialshift.c) write to
 *     IoBoard[MAIN].OutputState bits — nothing in the LP runtime reads
 *     those for hardware yet. Real coil drive stays in main.c::
 *     equipment_output_sync_task. Phase A2 will hand it over.
 *   - Engine's CurrentMode is exposed via lp_engine_get_current_mode()
 *     and emitted in SystemStatus.current_mode (proto field 16).
 *
 * Globals defined here that the engine needs and which we are NOT
 * pulling in from another nova_*.c TU:
 *   - Settings (we don't compile nova_settings.c — it pulls Flash.h
 *     for TM4C-era persistence; LP owns settings via lp_settings.c +
 *     OSPI ping-pong)
 *   - SaveSettingsRequest (same reason)
 *   - OutsideTemp pointer + PlenumTempAvg + StartTemp floats (would
 *     come from nova_analog_input.c which we also don't link)
 * ============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>           /* powf, log10f for wet-bulb depression */

#include "Settings.h"
#include "SerialShift.h"
#include "Analog_Input.h"   /* SENSOR_PLENUM_TEMP_1 etc. + extern decls   */
#include "States.h"          /* CurrentMode, SystemState, RC_*, ST_*, UI_* */
#include "Timer.h"           /* SystemAlarm[], AL_*, FM_NONE              */
#include "Failures.h"        /* NovaFailures_RunMasterBroadcastChk()      */

/* lp_rtc.h — used by GetTime() to feed the engine wall-clock time. */
#include "lp_rtc.h"
#include <time.h>

/* lp_settings.h lives next to us under lp_am2434/. */
#include "lp_settings.h"
#include "orbit_server/orbit_role.h"

/* OrbitClient_GetSample is exposed by orbit_client.h. */
#include "orbit_client.h"

/* Controls.h — CC_OFF + ClimacellClockMode() prototype for the
 * cooling-available wet-bulb gate. */
#include "Controls.h"

/* ─── Engine-owned globals ─────────────────────────────────────────── */

SYSTEM_SETTINGS    Settings;
SAVE_SETTINGS_INFO SaveSettingsRequest;

/* nova_analog_input.c is not linked — provide its globals. */
float PlenumTempAvg = SENSOR_VAL_UNDEFINED;
float StartTemp     = SENSOR_VAL_UNDEFINED;
float *OutsideTemp  = &PlenumTempAvg;   /* gets re-pointed in tick() */

/* `uptime_sec` is the master 1 Hz tick the legacy AS2 engine reads via
 * `XTimer()` (which latches it into `XTimerVal`) and that all PID
 * scheduling, alarm timers, and equipment-output ramp logic gates
 * itself against. On the original TM4C target it was driven by SysTick;
 * on Nova LP we own it here and bump it once per `lp_engine_tick`
 * (which itself runs at 1 Hz from a FreeRTOS task in main.c).
 *
 * Without this every `unsigned int Counter = XTimerVal;` in
 * nova_controls.c reads 0 forever. The PID re-arm logic
 * (`if (Counter < PIDCtrl[].Timer) return;` after `Timer = Counter +
 * Settings.Refrig.PID.U`) then never fires, so `PwmChannel[].Output`
 * stays at the cold-boot value (0 → clamps to PWM_MIN_VALUE → 0% on
 * the System Monitor) regardless of sensor/setpoint error. Same
 * symptom hit Door, Burner, Climacell, Humidifier — anything driven
 * by `PIDController()`. */
unsigned int uptime_sec = 0U;

/* ─── Internal helpers ─────────────────────────────────────────────── */

/* Bit assignment for the engine's "soft switches" (SW_*). The legacy
 * CheckInputs() treats SW_* differently from EQ_*: for switches it
 * reads `IoBoard[MAIN].InputState & checkBit` where `checkBit` =
 * `Settings.EquipIo[sw].Input` (used directly as a bitmask, not a
 * port-id). So we assign each switch a unique bit in InputState; the
 * specific bit value doesn't matter as long as Settings.EquipIo[sw].
 * Input matches what we set in InputState. */
/* Switch bits MUST live above the physical DI range (DI1..DI10 use
 * bits 0..9 of InputState via OrbitSample.di_bitmap merge below). If
 * SW_BIT started at bit 0, a floating DI2 on STORAGE would assert
 * SW_FAN_AUTO and a floating DI3 would assert SW_FAN_MANUAL — which
 * is exactly the "engine stuck in fan-manual" symptom we hit on
 * v0.A.34. Shift switches up to bit 16+; SW_START_STOP through
 * EQ_TOTAL_IO is ~16 entries so this fits in a uint32_t. */
#define SW_BIT(sw)  (1u << (16U + ((sw) - SW_START_STOP)))

/* Map each LpSettings.remote_off equipment slot index to the engine's
 * RO_* constant so Settings.RemoteOff[] mirror works. The LpSettings
 * remote_off array is indexed by the same EQ_* enum the engine uses,
 * but Settings.RemoteOff[] is indexed by RO_* — different ordering. */
static const struct {
    uint16_t eq_idx;    /* EQUIPMENT_IO index in LpSettings.remote_off */
    uint16_t ro_idx;    /* RO_* index in Settings.RemoteOff            */
} kRemoteOffMap[] = {
    { EQ_FAN,             RO_FAN },
    { EQ_REFRIGERATION,   RO_REFRIGERATION },
    { EQ_CLIMACELL,       RO_CLIMACELL },
    { EQ_HEAT,            RO_HEAT },
    { EQ_CAVITY_HEAT,     RO_CAVITY_HEAT },
    { EQ_BURNER,          RO_BURNER },
    /* Cure has no real EQ_* slot in the legacy enum.  Constellation
     * uses virtual slot 63 in the 64-wide remote_off table (set by
     * the UI's `cureBtn` via BUTTON_TO_RO=63).  AUTO/OFF only — the
     * UI dropdown hides MANUAL for cure because cure mode itself
     * reshapes the device UI, so a forced "manual cure" doesn't
     * mean anything.  build_switch_state() also reads slot 63
     * directly to gate SW_CURE_AUTO. */
    { 63U /* CURE_VIRTUAL */, RO_CURE },
    { EQ_HUMID_HEAD1,     RO_HUMIDIFIER1 },
    { EQ_HUMID_HEAD2,     RO_HUMIDIFIER2 },
    { EQ_HUMID_HEAD3,     RO_HUMIDIFIER3 },
    { EQ_LIGHTS1,         RO_LIGHTS1 },
    { EQ_LIGHTS2,         RO_LIGHTS2 },
    { EQ_AUX1,            RO_AUX1 },
    { EQ_AUX2,            RO_AUX2 },
    { EQ_AUX3,            RO_AUX3 },
    { EQ_AUX4,            RO_AUX4 },
    { EQ_AUX5,            RO_AUX5 },
    { EQ_AUX6,            RO_AUX6 },
    { EQ_AUX7,            RO_AUX7 },
    { EQ_AUX8,            RO_AUX8 },
};

static void mirror_remote_off(const LpSettingsData *lp)
{
    for (uint32_t i = 0; i < NUM_REMOTE_OFF; i++) {
        Settings.RemoteOff[i] = 0;     /* AUTO */
    }
    for (size_t i = 0; i < sizeof(kRemoteOffMap)/sizeof(kRemoteOffMap[0]); i++) {
        uint16_t eq = kRemoteOffMap[i].eq_idx;
        uint16_t ro = kRemoteOffMap[i].ro_idx;
        if (eq < LP_IO_ENTRIES_MAX && ro < NUM_REMOTE_OFF) {
            Settings.RemoteOff[ro] = lp->remote_off.state[eq];
        }
    }
}

/* E-Stop latch driven from STORAGE orbit DI11 (di_bitmap bit 10).
 * Replaces AS2's panel SW_START_STOP switch on Constellation: any
 * site that previously gated on CheckInputs(SW_START_STOP) will now
 * see "running" iff E-Stop is NOT asserted. Exported via
 * Nova_GetEStopActive() for SystemStatus field 22. */
static volatile uint8_t s_estop_active = 0;

uint8_t Nova_GetEStopActive(void) { return s_estop_active; }

/* Is any orbit slot configured with role=TRITON? Used by CtrlRefrig's
 * mode-configuration gate to recognize "operator picked TRITON as
 * the refrigeration path" — without this, the gate would falsely
 * raise WARN_NO_OUTPUT ("Mode configuration error") on a Constellation
 * panel that uses TRITON for refrigeration and has no AS2-style
 * EQ_REFRIG_STAGE1..8 mapped or PWM AO assigned.
 *
 * Checks the operator's intent (populated && role==TRITON) rather
 * than connectivity (`OrbitSample.online`). Offline TRITON would
 * generate a separate orbit-connectivity alarm, not a mode-config
 * one — the config side just cares that the operator declared a
 * TRITON exists. */
bool Nova_AnyTritonConfigured(void)
{
    for (uint32_t slot = 0U; slot < LP_ORBIT_ROLE_MAX; slot++) {
        const LpOrbitRoleEntry *e = LpSettings_GetOrbitRole(slot);
        if (e != NULL && e->populated != 0U
            && e->role == ORBIT_ROLE_TRITON) {
            return true;
        }
    }
    return false;
}

/* Build IoBoard[MAIN].InputState from operator state. Constellation
 * has NO panel switches and NO CPLD — only one physical input exists
 * (E-Stop on STORAGE orbit DI11). The legacy AS2 engine still reads
 * via CheckInputs(SW_*), so we synthesize the SW_*_AUTO/MANUAL bits
 * here from the operator's per-equipment Equipment Control state in
 * lp->remote_off. RemoteOff encoding (matches `RemoteOffState` proto):
 *   0 = AUTO    → SW_*_AUTO
 *   1 = OFF     → neither (engine treats equipment as switched off)
 *   2 = MANUAL  → SW_*_MANUAL (operator override)
 * Mode-decision paths (SetStateCooling, SetStateRefrig, etc.) gate
 * on SW_*_AUTO; ApplyManualOverrides forces the coil ON when the
 * matching SW_*_MANUAL bit is set. Mixing both into AUTO masks
 * MANUAL behind the scheduler. */
static void build_switch_state(const LpSettingsData *lp)
{
    uint32_t bits = 0;

    /* SW_START_STOP is asserted unless E-Stop is active. The DI11 read
     * happens at the bottom of this function (after the OrbitSample
     * fetch); we set the bit unconditionally here and clear it later
     * if E-Stop turns out to be asserted, so any goto-style early
     * returns added later don't accidentally leave it set. */
    bits |= SW_BIT(SW_START_STOP);

    #define EQ_AUTO(eq) (lp->remote_off.state[eq] == 0U /* AUTO */)
    #define EQ_MAN(eq)  (lp->remote_off.state[eq] == 2U /* MANUAL */)

    /* SW_FAN_AUTO must stay asserted under SYSSTOP (RemoteOff==3, set by
     * the home-page Stop button via CMD_SYSTEM_STOP). The legacy
     * `CheckSystemStatus` order in nova_states.c is:
     *   1. SW_FAN_MANUAL    → ST_FAN_MANUAL
     *   2. !FAN_AUTO && !FAN_MANUAL → ST_FAN_OFF   ← would fire on SYSSTOP
     *   3. RemoteOff[RO_FAN]==1 → ST_FAN_REMOTEOFF
     *   4. RemoteOff[RO_FAN]==3 → ST_SYSTEM_REMOTEOFF (UI_SYSTEM_REMOTEOFF=20)
     * If SYSSTOP cleared SW_FAN_AUTO, gate 2 would catch it before gate 4
     * and the UI would land on UI_FAN_OFF=13 — leaving the home-page
     * button label stuck on "Stop" with no way to restart. Keeping the
     * AUTO bit set under SYSSTOP routes through gate 4 correctly; gates
     * 1-2 don't fire (MANUAL bit is clear), gate 3 doesn't fire (value
     * is 3, not 1), gate 4 latches ST_SYSTEM_REMOTEOFF as intended. */
    if (EQ_AUTO(EQ_FAN) || lp->remote_off.state[EQ_FAN] == 3U /* SYSSTOP */) {
        bits |= SW_BIT(SW_FAN_AUTO);
    }
    if (EQ_MAN(EQ_FAN))            bits |= SW_BIT(SW_FAN_MANUAL);
    /* DOORS: AUTO and MANUAL both assert SW_FRESHAIR_AUTO so the
     * legacy CtrlDoorsPulsed gate (`!CheckInputs(SW_FRESHAIR_AUTO)`
     * → pulse-close) doesn't fire under MANUAL. The post-mode
     * override hook below then forces PWM_DOORS to MAX so
     * CtrlDoorsPulsed pulses open instead. SW_FRESHAIR_MANUAL is
     * still set under MANUAL so the GetEquipStatus display shows
     * "manual" cosmetically. */
    if (EQ_AUTO(EQ_DOORS) || EQ_MAN(EQ_DOORS)) {
        bits |= SW_BIT(SW_FRESHAIR_AUTO);
    }
    if (EQ_MAN(EQ_DOORS))          bits |= SW_BIT(SW_FRESHAIR_MANUAL);
    if (EQ_AUTO(EQ_REFRIGERATION)) bits |= SW_BIT(SW_REFRIG_AUTO);
    if (EQ_AUTO(EQ_CLIMACELL))     bits |= SW_BIT(SW_CLIMACELL_AUTO);
    if (EQ_MAN(EQ_CLIMACELL))      bits |= SW_BIT(SW_CLIMACELL_MANUAL);
    if (EQ_AUTO(EQ_HUMID_HEAD1))   bits |= SW_BIT(SW_HUMID_AUTO);
    if (EQ_MAN(EQ_HUMID_HEAD1))    bits |= SW_BIT(SW_HUMID_MANUAL);
    if (EQ_AUTO(EQ_BURNER))        bits |= SW_BIT(SW_BURNER_AUTO);
    /* Burner has no SW_BURNER_MANUAL bit in the legacy CPLD shift-
     * register enum (see SerialShift.h around line 107: SW_BURNER_AUTO
     * has no _MANUAL twin). The MANUAL behaviour is delivered by the
     * post-SetMode override block below, which forces PWM_BURNER to
     * Settings.Burner.Manual % when remote_off==2. */
    if (EQ_AUTO(EQ_AUX1))          bits |= SW_BIT(SW_AUX1_AUTO);
    if (EQ_MAN(EQ_AUX1))           bits |= SW_BIT(SW_AUX1_MANUAL);
    if (EQ_AUTO(EQ_AUX2))          bits |= SW_BIT(SW_AUX2_AUTO);
    if (EQ_MAN(EQ_AUX2))           bits |= SW_BIT(SW_AUX2_MANUAL);

    /* Cure switch (AUTO/OFF only — no MANUAL).  Operator picks via
     * the Equipment Control "Cure" tile, which writes virtual slot
     * 63 in lp->remote_off (mirrored to Settings.RemoteOff[RO_CURE]
     * by mirror_remote_off above).  Default 0=AUTO sets the bit;
     * 1=OFF clears it.  2=MANUAL is treated as AUTO defensively
     * (UI hides the option but be safe against a stale OSPI blob). */
    if (lp->remote_off.state[63] != 1U /* OFF */) {
        bits |= SW_BIT(SW_CURE_AUTO);
    }

    /* Fresh-air doors are operator-controlled via the Equipment Control
     * page (EQ_AUTO/EQ_MAN predicates above already handle DOORS).
     * The legacy CPLD economizer auto-assert was a transitional bypass
     * for builds that had no DOORS selector in the UI; with the
     * dedicated Auto/Off/Manual row in place that bypass is removed.
     * Operator picks AUTO → SW_FRESHAIR_AUTO; OFF → neither bit set;
     * MANUAL → SW_FRESHAIR_MANUAL (which forces the damper full-open
     * regardless of state-machine demand). */

    #undef EQ_AUTO
    #undef EQ_MAN

    /* Merge real DI bits from STORAGE orbit. di_bitmap bit layout
     * (per orbit_client.h): bits 0..9 = DI1..DI10, bit 10 = E-stop,
     * bits 11..14 = DC24V health. Mask to 10 bits when feeding the
     * legacy IoBoard.Input[] table (which only sizes for DI1..DI10);
     * E-stop (bit 10) is consumed separately to drive s_estop_active
     * and gate SW_START_STOP. Switch bits live in the high half of
     * InputState (SW_BIT shifts way past bit 10) so OR-ing the DI
     * bits in doesn't collide with switch bits.
     *
     * Polarity (per spec, May 2026): E-Stop is the ONLY physical
     * switch on Constellation. Wired as a normally-closed contact:
     *   - Closed (input ENERGIZED, bit=1) → system PERMITTED to run.
     *   - Open   (input DEAD,        bit=0) → SHUTDOWN. Outputs are
     *     also de-energized at the hardware safety relay; the
     *     firmware path here is belt-and-suspenders so the engine
     *     state, UI indicator, AO 4 mA endpoint, and pulse-door
     *     close all line up with the physical reality.
     *
     * Fail-safe: orbit unreachable → E-Stop treated as asserted. */
    OrbitSample storage_sample;
    if (OrbitClient_GetSample(0U /* STORAGE */, &storage_sample)
        && storage_sample.io_valid) {
        bits |= ((uint32_t)storage_sample.di_bitmap) & 0x3FFU;
        s_estop_active = (storage_sample.di_bitmap & (1U << 10)) ? 0U : 1U;
    } else {
        /* Orbit unreachable: fail-safe to E-Stop asserted. */
        s_estop_active = 1U;
    }

    if (s_estop_active) {
        bits &= ~SW_BIT(SW_START_STOP);
    }

    IoBoard[MAIN].InputState = bits;
}

/* Mirror sensor values from orbit telemetry into the legacy
 * Settings.AnalogBoard[].Sensor[] table the engine reads. */
static void mirror_sensors(const LpSettingsData *lp)
{
    OrbitSample sample;
    if (!OrbitClient_GetSample(0U /* STORAGE */, &sample)) {
        return;     /* engine sees previous values until next tick */
    }

    bool tempF = (Settings.TempType == 0U);

    Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Present = 1;
    for (uint32_t ch = 0; ch < 4; ch++) {
        uint16_t raw = sample.sensorHr[ch];
        if (raw == 0x7FFFU) {
            Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[ch].Disabled = 1;
            Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[ch].Value = SENSOR_VAL_UNDEFINED;
        } else {
            float c = (float)(int16_t)raw / 10.0f;
            float v = tempF ? (c * 1.8f + 32.0f) : c;
            Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[ch].Disabled = 0;
            Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[ch].Value = v;
        }
    }

    Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present = 1;
    for (uint32_t i = 0; i < 4; i++) {
        uint16_t raw = sample.sensorHr[4 + i];
        bool isCo2 = (i == 3U);
        if (raw == 0x7FFFU) {
            Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[i].Disabled = 1;
            Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[i].Value = SENSOR_VAL_UNDEFINED;
        } else {
            float v = isCo2 ? (float)(int16_t)raw
                            : (float)(int16_t)raw / 10.0f;
            Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[i].Disabled = 0;
            Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[i].Value = v;
        }
    }

    OutsideTemp =
        &Settings.AnalogBoard[DEFAULT_TEMP_BOARD]
                  .Sensor[SENSOR_OUTSIDE_TEMP].Value;

    float pt1 = Settings.AnalogBoard[DEFAULT_TEMP_BOARD]
                  .Sensor[SENSOR_PLENUM_TEMP_1].Value;
    float pt2 = Settings.AnalogBoard[DEFAULT_TEMP_BOARD]
                  .Sensor[SENSOR_PLENUM_TEMP_2].Value;
    bool ok1 = (pt1 != SENSOR_VAL_UNDEFINED);
    bool ok2 = (pt2 != SENSOR_VAL_UNDEFINED);
    if (ok1 && ok2)      PlenumTempAvg = (pt1 + pt2) * 0.5f;
    else if (ok1)        PlenumTempAvg = pt1;
    else if (ok2)        PlenumTempAvg = pt2;
    else                 PlenumTempAvg = SENSOR_VAL_UNDEFINED;

    (void)lp;
}

static void mirror_basic_setpoints(const LpSettingsData *lp)
{
    Settings.TempType   = lp->basic.temp_type;
    Settings.SystemMode = lp->basic.system_mode;
    Settings.Plenum.TempSet          = lp->plenum.temp_setpoint;
    Settings.Plenum.HumidSet         = lp->plenum.humid_setpoint;
    /* Onion-mode humidity-source select: 0 = use plenum humid sensor,
     * 1 = use return-air sensor. Read by `CtrlFan` onion-humid path
     * (nova_controls.c:1248). Without this mirror the legacy reads
     * BSS zero and operator's "return-air" pick is silently ignored. */
    Settings.Plenum.HumidSetpointRef = (uint8_t)lp->plenum.humid_setpoint_ref;

    /* Mirror the operator-tunable plenum temperature deviation alarms.
     * Without this, `Settings.Plenum.{Low,High}AlarmTemp/Timer` stay
     * at zero (BSS), and `nova_failures.c::PlenumTempFail{High,Low}Chk`
     * fails immediately — the time gate is `Counter - AlarmTimer >=
     * Settings.Plenum.HighAlarmTimer * T_MINS`, which is satisfied on
     * the first tick when the timer is 0, and the temp gate is
     * `PlenumTempAvg > RefTemp + HighAlarmTemp` which trips as soon
     * as the plenum reads anything above setpoint when the deviation
     * is 0. The user-visible symptom is "high plenum temperature
     * failure" the moment fan output asserts. */
    Settings.Plenum.LowAlarmTemp   = lp->temp_alarm.low_temp;
    Settings.Plenum.LowAlarmTimer  = (char)lp->temp_alarm.low_timer;
    Settings.Plenum.HighAlarmTemp  = lp->temp_alarm.high_temp;
    Settings.Plenum.HighAlarmTimer = (char)lp->temp_alarm.high_timer;

    /* Plenum-side failure thresholds. Both come from FailureSettings2
     * (UI page level2/failures2). `HumidLowFailure` gates
     * `nova_failures.c::PlenumLowHumidChk`; `SensorDiff` gates the
     * plenum-sensor-mismatch failure path. BSS zero leaves both
     * thresholds permanently triggered or permanently inhibited
     * depending on comparison direction — both wrong. */
    Settings.Plenum.HumidLowFailure  = (uint8_t)lp->failure2.low_humid_set;
    Settings.Plenum.SensorDiff       = lp->failure2.plen_sen_diff;

    /* Mirror the 48-slot half-hour run schedule. LpRuntime stores
     * sparse {slot,mode} entries; default-fill RC_STANDBY for any
     * slot the operator hasn't explicitly set. The mode values are
     * legacy RC_* (1=COOLING, 2=RECIRC, 3=STANDBY, 4=REFRIG, 5=CURE)
     * — same enum on both sides, so a direct copy is correct. */
    for (uint32_t i = 0; i < 48; i++) {
        Settings.RunTimes[i] = RC_STANDBY;
    }
    for (uint32_t i = 0; i < lp->runtime.count && i < LP_RUNTIME_MAX_ENTRIES; i++) {
        uint32_t slot = lp->runtime.entries[i].slot;
        if (slot < 48U) {
            Settings.RunTimes[slot] = (char)lp->runtime.entries[i].mode;
        }
    }
    /* Cure-mode 48-slot schedule has no LpSettings field yet; AS2 keeps
     * it separate so onion cure can run a different schedule than the
     * normal run schedule. Until LpCure grows a runtimes[] field, point
     * cure-mode at the same schedule (operator can still flip the SM
     * between potato/onion to engage cure-specific logic). */
    memcpy(Settings.Cure.RunTimes, Settings.RunTimes, 48);
}

/* Mirror LpCure / LpCureLimit → Settings.Cure (legacy CURE_CTRL).
 * Engine reads StartTemp/StartHumid/HumidRef in nova_modes.c::ModeCure
 * + nova_states.c::SetStateCure. */
static void mirror_cure(const LpSettingsData *lp)
{
    Settings.Cure.StartTemp      = lp->cure.start_temp;
    Settings.Cure.HumidRef       = (char)lp->cure.humid_ref;
    Settings.Cure.StartHumid     = (char)lp->cure.start_humid;
    Settings.Cure.HumidHighLimit = (char)lp->cure.humid_high_limit;
    Settings.Cure.TempLowLimit   = lp->cure_limit.temp_low_limit;
    Settings.Cure.TempHighLimit  = lp->cure_limit.temp_high_limit;
}

/* Mirror LpRefrig → Settings.Refrig.Mode + Limit. The stage list and
 * PID gains aren't read by the mode-decision path — they're consumed
 * inside CtrlRefrig which has its own per-stage state machine. For A4
 * we only need .Mode (drives RC_REFRIG branch in SetStateCooling) and
 * .Limit (gates onion-mode RC_REFRIG entry in SetStateOnion). */
static void mirror_refrig(const LpSettingsData *lp)
{
    Settings.Refrig.Mode  = (uint8_t)lp->refrig.mode;
    Settings.Refrig.Limit = lp->refrig.limit;
    Settings.Refrig.PID.P = lp->refrig.p_gain;
    Settings.Refrig.PID.I = lp->refrig.i_gain;
    Settings.Refrig.PID.D = lp->refrig.d_gain;
    Settings.Refrig.PID.U = lp->refrig.u_limit;

    /* Defrost scheduling. `nova_states.c:1191-1197` checks
     * `DefrostPeriod != 0` before scheduling — BSS zero means defrost
     * never runs. Source: LpRefrig.{defrost_interval, defrost_duration}
     * from RefrigSettings proto fields 8 + 9. Cast to uint8_t to match
     * the legacy struct (proto carries uint32 for forwards-compat
     * headroom but the legacy AS2 storage is byte-wide). */
    Settings.Refrig.DefrostPeriod   = (uint8_t)lp->refrig.defrost_interval;
    Settings.Refrig.DefrostDuration = (uint8_t)lp->refrig.defrost_duration;

    /* Refrig fail-mode (read at nova_states.c:1354/1358, nova_controls
     * .c:1714/1715) selects sensor-failure response: 0=stop refrig,
     * 1=continue. BSS zero is the safer default but should still come
     * from operator config. */
    Settings.Refrig.FailMode = (uint8_t)lp->refrig.fail_mode;

    /* CO2-purge pump-down enable (read at nova_modes.c:510). When non-
     * zero, refrig is disengaged during a CO2 purge. BSS zero
     * permanently inhibits the pump-down path. */
    Settings.Refrig.Purge = (uint8_t)lp->refrig.purge;

    /* Multi-stage refrig staging. `nova_controls.c:1799-1856` reads
     * `Stage[i].{On,Off}` to compute staging thresholds and
     * `Stage[i].Diagnostic` for per-stage diag-off override. Without
     * this loop ALL stage thresholds are 0 → first stage trips at any
     * refrig demand and higher stages never engage. Highest-impact
     * legacy regression after the ramp bug. The proto bound is
     * LP_REFRIG_MAX_STAGES (8); legacy bound is NUM_REFRIG_STAGES (8) —
     * same. Loop count is `min(stages_count, NUM_REFRIG_STAGES)` so
     * unpopulated tail slots stay at their previous values (defaults
     * to 0 on first tick). */
    const uint32_t n_stage =
        (lp->refrig.stages_count < NUM_REFRIG_STAGES)
        ? lp->refrig.stages_count
        : NUM_REFRIG_STAGES;
    for (uint32_t i = 0; i < n_stage; i++) {
        Settings.Refrig.Stage[i].On         = (uint8_t)lp->refrig.stages[i].on;
        Settings.Refrig.Stage[i].Off        = (uint8_t)lp->refrig.stages[i].off;
        Settings.Refrig.Stage[i].Diagnostic = (uint8_t)lp->refrig.stages[i].diagnostic;
    }
    /* NOTE: Settings.Refrig.Defrost[i].Diagnostic is also read by the
     * engine but the proto schema does not yet model per-defrost-stage
     * diag overrides. Leave at BSS zero (no-op as default) until the
     * proto adds a defrost_stages[] field. */
}

/* Mirror LpOutsideAir → Settings.OutsideAir. Drives the cooling-vs-
 * refrig decision in nova_states.c::SetStateCooling and the calc-humid
 * gate in nova_states.c::SetStateOnion. Field names are different
 * (legacy CtrlMode/Diff/AboveBelow/TempRef/CalcHumidMax) but values
 * pass through directly. */
static void mirror_outside_air(const LpSettingsData *lp)
{
    Settings.OutsideAir.CtrlMode      = (uint8_t)lp->outside_air.mode;
    Settings.OutsideAir.Diff          = lp->outside_air.differential;
    Settings.OutsideAir.AboveBelow    = (char)lp->outside_air.above_below;
    Settings.OutsideAir.TempRef       = (char)lp->outside_air.temp_ref;
    Settings.OutsideAir.CalcHumidMax  = (char)lp->outside_air.calc_humid_max;
}

/* Mirror LpBurner → Settings.Burner. Engine consumes Mode/On/Low/
 * TempSet/Threshold in nova_modes.c::ModeCure + CtrlBurner.
 *
 * LpBurner has a single `manual` field; legacy AS2 splits the
 * BURNER_MANUAL output into two slots: `Manual` (the % the burner
 * should run at while in manual mode — read by `CtrlBurner(Settings
 * .Burner.Manual)` at nova_modes.c:210) and `Threshold` (the
 * economy-mode air-cure-vs-burner-cure crossover %, compared against
 * the live PWM output at nova_modes.c:180/184/188/etc.). The proto
 * does not yet model these as separate fields, so we copy `manual`
 * into BOTH slots — operator-set value drives manual mode correctly
 * AND serves as a sane economy-mode crossover until a separate
 * `threshold` proto field is added. */
static void mirror_burner(const LpSettingsData *lp)
{
    Settings.Burner.Mode      = (char)lp->burner.mode;
    Settings.Burner.On        = (char)lp->burner.on;
    Settings.Burner.Low       = (char)lp->burner.low;
    Settings.Burner.Manual    = (char)lp->burner.manual;
    /* AS2-faithful: `Plenum.TempSet` and `Burner.TempSet` are fully
     * independent settings in every mode (factory defaults 46° vs 75°,
     * separate POST handlers `StorePostData.c:2035-2056`, separate
     * downstream readers — the cure burner PID at `Controls.c:601`
     * reads `Burner.TempSet` exclusively, while `Plenum.TempSet`
     * drives cavity heat / climacell / fan ref / CO2 purge / ramp /
     * outside-air even in onion mode). The UI already routes the
     * operator's onion-mode "Plenum Setpoint" entry into proto field 4
     * (constellation-ui/src/routes/level1/plentemp/+page.svelte
     * `plensetup[3]` binding when `onionMode`) — `mirror_basic_setpoints`
     * mirrors proto field 1 to `Plenum.TempSet`, this one mirrors
     * proto field 4 to `Burner.TempSet`. The previous unconditional
     * alias `Settings.Burner.TempSet = lp->plenum.temp_setpoint` was
     * wrong: it clobbered the operator's onion-mode setpoint entry on
     * every tick, leaving the cure burner PID running against
     * proto field 1 (which the UI never wrote in onion mode). */
    Settings.Burner.TempSet   = lp->plenum.burner_temp_setpoint;
    Settings.Burner.Threshold = (float)lp->burner.manual;  /* see comment */
    Settings.Burner.PID.P     = lp->burner.p_gain;
    Settings.Burner.PID.I     = lp->burner.i_gain;
    Settings.Burner.PID.D     = lp->burner.d_gain;
    Settings.Burner.PID.U     = lp->burner.u_limit;
}

/* Mirror LpRampRate → legacy Settings.Ramp so nova_states.c::SetRamp()
 * can read the operator-configured values. Without this mirror SetRamp
 * runs every tick against BSS-zero defaults (RatePerDay=0, UpdatePeriod=0,
 * TargetTemp=0) — which silently no-ops the ramp because the activation
 * gate `target!=setpoint && period!=0 && rate!=0` (nova_states.c:886-888,
 * AS2 States.c:870-878) can never be satisfied.
 *
 * Field-name mapping is non-obvious — the proto field is `rate_per_day`
 * but it carries AS2's `UpdateTemp` semantics (degrees per *update step*,
 * not per day; the user enters e.g. 0.3°/step). Period gates "how often
 * to step": numeric = hours of fan runtime, 255 = "automatic" (drive a
 * step whenever the reference sensor catches up to within `tempDiff`).
 * UpdatePeriod is `char` in legacy struct; ARM AAPCS treats `char` as
 * unsigned by default so the auto-mode value 255 round-trips cleanly. */
static void mirror_ramp(const LpSettingsData *lp)
{
    Settings.Ramp.UpdateTemp   = lp->ramp_rate.rate_per_day;
    Settings.Ramp.UpdatePeriod = (char)lp->ramp_rate.update_period;
    Settings.Ramp.TempDiff     = lp->ramp_rate.temp_diff;
    Settings.Ramp.TempRef      = (uint8_t)lp->ramp_rate.temp_ref;
    Settings.Ramp.TargetTemp   = lp->ramp_rate.target_temp;
}

/* Mirror LpDoor → Settings.Door (CoolAirCycle gate in SetStateCooling). */
static void mirror_door(const LpSettingsData *lp)
{
    Settings.Door.CoolAirCycle = (char)lp->door.cool_air_cycle;
    Settings.Door.ActuatorTime = (short)lp->door.actuator_time;
    Settings.Door.ManualPct    = (uint8_t)(lp->door.manual_pct > 100U
                                            ? 100U
                                            : lp->door.manual_pct);
    Settings.Door.ManualTimeoutMins =
        (uint16_t)(lp->door.manual_timeout_mins > 1440U
                    ? 1440U
                    : lp->door.manual_timeout_mins);
    Settings.Door.PID.P        = lp->door.p_gain;
    Settings.Door.PID.I        = lp->door.i_gain;
    Settings.Door.PID.D        = lp->door.d_gain;
    Settings.Door.PID.U        = lp->door.u_limit;
}

/* Mirror LpClimacell → Settings.Climacell. The wet-bulb cooling-available
 * calc below reads Efficiency / Altitude / AltitudeUnits; CtrlClimacell
 * (when wired in a later phase) will read the PID gains. */
static void mirror_climacell(const LpSettingsData *lp)
{
    Settings.Climacell.Efficiency    = (char)lp->climacell.efficiency;
    Settings.Climacell.Altitude      = (short)lp->climacell.altitude;
    Settings.Climacell.AltitudeUnits = (char)lp->climacell.alt_units;
    Settings.Climacell.PID.P         = lp->climacell.p_gain;
    Settings.Climacell.PID.I         = lp->climacell.i_gain;
    Settings.Climacell.PID.D         = lp->climacell.d_gain;
    Settings.Climacell.PID.U         = lp->climacell.u_limit;
    for (uint32_t h = 0; h < 48U; h++) {
        Settings.Climacell.Times[h] = (char)lp->climacell_times.hourly_efficiency[h];
    }
}

/* Mirror LpMisc.heat_temp_thresh → Settings.HeatTempThresh. */
static void mirror_misc(const LpSettingsData *lp)
{
    Settings.HeatTempThresh = lp->misc.heat_temp_thresh;
}

/* Mirror LpCo2 + LpFailure2.co2_setpt → Settings.Co2.*  +  Purge.*
 *
 * Without this, the entire Level-1 CO2 page is dead — `Settings.Co2.Purge
 * .Mode` reads BSS zero so `nova_modes.c:282/510` permanently treats CO2
 * as disabled regardless of the operator's mode pick. The proto's
 * `cycle_or_set` field is mode-dependent: in Timer mode (1) it's the
 * cycle hours; in Sensor mode (2) it's the trigger ppm. We copy into
 * both legacy slots so the engine's mode-conditional reads land on
 * the right value (the stale slot is harmless since legacy Mode==Off
 * (0) ignores both). */
static void mirror_co2(const LpSettingsData *lp)
{
    Settings.Co2.Purge.Mode     = (char)lp->co2.mode;
    Settings.Co2.Purge.MinTemp  = lp->co2.min_temp;
    Settings.Co2.Purge.MaxTemp  = lp->co2.max_temp;
    Settings.Co2.Purge.Duration = (char)lp->co2.duration_minutes;
    Settings.Co2.FanOutput      = (char)lp->co2.fan_output;
    Settings.Co2.DoorOutput     = (char)lp->co2.door_output;

    /* cycle_or_set: mode-dependent dual interpretation. */
    if (lp->co2.mode == 1U) {
        Settings.Co2.CylceTime = (char)lp->co2.cycle_or_set;
    } else if (lp->co2.mode == 2U) {
        Settings.Co2.Set = (short)lp->co2.cycle_or_set;
    }

    /* High-CO2 failure threshold from FailureSettings2 (proto field 7).
     * Read at nova_failures.c:270 — BSS zero would have any non-zero
     * CO2 reading instantly trip the failure path (gated upstream by
     * Co2.Purge.Mode==0 today, but a latent landmine the moment CO2
     * mode becomes non-zero). */
    Settings.Co2.HighFailure = (int16_t)lp->failure2.co2_setpt;
}

/* Mirror LpMisc cavity_* → Settings.CavityHeat.*
 *
 * Without this the entire cavity-heat feature is dead: nova_controls.c:683
 * short-circuits CtrlCavityHeat when Mode<2, and nova_modes.c:597 reads
 * StandbyOn=0 so cavity heat never runs in standby even when configured.
 * `cavity_duty_or_sensor` is the "duty cycle %" in duty mode and the
 * "sensor select" in sensor mode — copied verbatim into DutyCycle since
 * the legacy struct's `Label` byte for the sensor-mode selector isn't
 * proto-modeled. */
static void mirror_cavity_heat(const LpSettingsData *lp)
{
    Settings.CavityHeat.Mode      = (uint8_t)lp->misc.cavity_mode;
    Settings.CavityHeat.Diff      = lp->misc.cavity_diff;
    Settings.CavityHeat.DutyCycle = (uint8_t)lp->misc.cavity_duty_or_sensor;
    Settings.CavityHeat.StandbyOn = (uint8_t)lp->misc.cavity_standby_on;
}

/* Mirror LpPidLog → Settings.Log.PID. The legacy `Pid()` body in
 * `nova_controls.c` only emits a record (`PIDLogWrite`) when the
 * matching `Settings.Log.PID.{Door,Refrig}` byte is non-zero. Without
 * this mirror those flags stay BSS-zero and our typed PidLogStream
 * channel never produces records — no matter what the operator picks
 * on the Level-2 PID-tuning page. (`Wrap` is informational on the
 * firmware side; the bridge SQLite store does its own retention.) */
static void mirror_pid_log(const LpSettingsData *lp)
{
    Settings.Log.PID.Wrap   = (char)lp->pid_log.wrap;
    Settings.Log.PID.Door   = (uint8_t)lp->pid_log.log_doors;
    Settings.Log.PID.Refrig = (uint8_t)lp->pid_log.log_refrig;
}

/* Mirror LpFanSpeed → Settings.Fan and LpFanBoost → Settings.FanBoost.
 * The legacy `CtrlFan` reads `Settings.Fan.{Min,Max,Refrig,Recirc,Prev}
 * Speed` (uint8 percent), `UpdatePeriod` (hours), `TempDiff`,
 * `TempRef1/2`, and `UpdateMode`; without this mirror they all sit at
 * BSS zero so `PercentToPWMVal(MaxSpeed=0) → PWM_MIN_VALUE` and the
 * fan output never builds. The runtime accumulators
 * (DailyRunTime/TotalRunTime/etc.) are populated by `FanRunTimer`
 * itself and are NOT touched here so the running tallies aren't
 * clobbered each tick. PrevSpeed is also left to the engine to
 * track (it stores the last "cooling mode" speed; mirroring the
 * persisted snapshot only on the very first tick is enough — the
 * engine then owns it). */
static bool s_fan_prev_seeded = false;
static void mirror_fan(const LpSettingsData *lp)
{
    Settings.Fan.MaxSpeed     = (char)lp->fan_speed.max_speed;
    Settings.Fan.MinSpeed     = (char)lp->fan_speed.min_speed;
    Settings.Fan.RefrigSpeed  = (char)lp->fan_speed.refrig_speed;
    Settings.Fan.RecircSpeed  = (char)lp->fan_speed.recirc_speed;
    Settings.Fan.UpdatePeriod = (char)lp->fan_speed.update_period;
    Settings.Fan.TempDiff     = lp->fan_speed.temp_diff;
    Settings.Fan.TempRef1     = (char)lp->fan_speed.temp_ref1;
    Settings.Fan.TempRef2     = (char)lp->fan_speed.temp_ref2;
    Settings.Fan.UpdateMode   = (char)lp->fan_speed.update_mode;
    if (!s_fan_prev_seeded) {
        Settings.Fan.PrevSpeed = (char)lp->fan_speed.prev_speed;
        s_fan_prev_seeded = true;
    }

    Settings.FanBoost.Mode     = (char)lp->fan_boost.mode;
    Settings.FanBoost.Speed    = (char)lp->fan_boost.speed;
    Settings.FanBoost.Interval = (char)lp->fan_boost.interval;
    Settings.FanBoost.Duration = (char)lp->fan_boost.duration;
    Settings.FanBoost.Temp     = lp->fan_boost.temp;
}

/* Mirror LpSettings.io_config.input_map[eq] → Settings.EquipIo[eq].Input
 * so the engine's CheckInputs() picks up the right port. Same encoding
 * as the output map; port_id 0 = unmapped → IO_UNDEFINED. Switches
 * (eq >= SW_START_STOP) are left alone — they're driven by
 * build_switch_state from operator AUTO/MANUAL state, not from
 * physical wiring. */
static void mirror_input_map(const LpSettingsData *lp)
{
    const uint32_t in_count = lp->io_config.input_count;
    for (uint32_t eq = 0; eq < SW_START_STOP && eq < EQ_TOTAL_IO; eq++) {
        unsigned int port_id = (eq < in_count) ? lp->io_config.input_map[eq]
                                                : 0U;
        if (port_id == 0U) {
            Settings.EquipIo[eq].Input = IO_UNDEFINED;
        } else {
            Settings.EquipIo[eq].Input = port_id;
        }
    }
    /* Three slots have engine-internal "assume present" bypass when
     * Input==IO_UNDEFINED — don't override that with a stale port_id. */
    Settings.EquipIo[EQ_POWER].Input          = IO_UNDEFINED;
    Settings.EquipIo[EQ_REFRIG_STANDBY].Input = IO_UNDEFINED;
    Settings.EquipIo[EQ_REMOTE_STANDBY].Input = IO_UNDEFINED;
}

/* Mirror LpSettings.io_config.output_map[eq] → Settings.EquipIo[eq].Output
 * so the engine's OutputOn/OutputOff (writing IoBoard[].OutputState bits)
 * find the right port. The legacy port_id encoding is identical:
 * `port_id = board * SS_PORT_ID_MULTIPLIER + port`, where MAIN board
 * has port_ids 1..11 and 0 = unmapped. */
static void mirror_output_map(const LpSettingsData *lp)
{
    const uint32_t out_count = lp->io_config.output_count;
    for (uint32_t eq = 0; eq < SW_START_STOP && eq < EQ_TOTAL_IO; eq++) {
        unsigned int port_id = (eq < out_count) ? lp->io_config.output_map[eq]
                                                : 0U;
        if (port_id == 0U) {
            Settings.EquipIo[eq].Output  = IO_UNDEFINED;
            Settings.EquipIo[eq].Enabled = 0;
        } else {
            Settings.EquipIo[eq].Output  = port_id;
            Settings.EquipIo[eq].Enabled = 1;
        }
    }
}

/* ─── Wet-bulb depression + cooling-available temperature ─────────────
 *
 * Re-implements legacy Mini_IO/States.c::WetBulbDepression() and
 * SetStartTemp(). Computes the global StartTemp ("Cooling Available
 * Temperature" on the home page) — the maximum outside dry-bulb
 * temperature at which the storage can still cool to the plenum
 * setpoint by drawing outside air (optionally through a climacell
 * evaporative cooler).
 *
 *   StartTemp = RefTemp + Diff
 *             + WetBulbDepression × Climacell.Efficiency / 100
 *                   (when climacell auto/manual + scheduled +
 *                    not RemoteOff + no climacell alarm)
 *
 * RefTemp source: Settings.OutsideAir.TempRef
 *   255 → Settings.Plenum.TempSet
 *   254 → return-air temp sensor
 *   0..N → specific sensor by global ID
 *
 * The legacy SM_POTATO restriction on the WB term is intentionally
 * dropped (matches the Platform implementation comment) — onion / cure
 * / generic storage all benefit from evap-cooling assist.
 *
 * SetStateCooling() in nova_states.c then compares *OutsideTemp
 * against StartTemp to gate fresh-air cooling vs refrigeration. */
static float lp_wet_bulb_depression(void)
{
    const int B = DEFAULT_HUMID_BOARD;
    const int S = SENSOR_OUTSIDE_HUMID;

    if (   OutsideTemp == NULL
        || *OutsideTemp == SENSOR_VAL_UNDEFINED
        || Settings.AnalogBoard[B].Sensor[S].Disabled == 1
        || Settings.AnalogBoard[B].Sensor[S].Value < 1.0f
        || Settings.AnalogBoard[B].Sensor[S].Value == SENSOR_VAL_UNDEFINED) {
        return 0.0f;
    }

    float TempC = *OutsideTemp;
    if (Settings.TempType == 0) {                       /* °F → °C */
        TempC = (TempC - 32.0f) / 1.8f;
    }

    float AltM = (float)Settings.Climacell.Altitude;
    if (Settings.Climacell.AltitudeUnits == 0) {        /* feet → metres */
        AltM *= 0.3048f;
    }

    float BP  = (101.3f * powf(((293.0f - 0.0065f * AltM) / 293.0f), 5.26f)) * 10.0f;
    float Svp = 6.11f * powf(10.0f, ((7.5f * TempC) / (237.7f + TempC)));
    float Avp = Svp * (Settings.AnalogBoard[B].Sensor[S].Value / 100.0f);
    float Tdp = (237.7f * log10f(Avp / 6.112f)) / (7.5f - log10f(Avp / 6.112f));
    float A   = (4098.0f * Avp) / powf((Tdp + 237.7f), 2.0f);
    float Twb = (((0.00066f * BP) * TempC) + (A * Tdp)) / ((0.00066f * BP) + A);
    float Dwb = TempC - Twb;
    if (Settings.TempType == 0) {                       /* back to °F */
        Dwb *= 1.8f;
    }
    return Dwb;
}

static void lp_calc_cooling_available(void)
{
    if (OutsideTemp == NULL || *OutsideTemp == SENSOR_VAL_UNDEFINED) {
        StartTemp = SENSOR_VAL_UNDEFINED;
        return;
    }

    float refTemp = SENSOR_VAL_UNDEFINED;
    if (Settings.OutsideAir.TempRef == 255) {
        refTemp = Settings.Plenum.TempSet;
    } else if (Settings.OutsideAir.TempRef == 254) {
        refTemp = Settings.AnalogBoard[DEFAULT_TEMP_BOARD]
                          .Sensor[SENSOR_RETURN_TEMP].Value;
    } else {
        int board  = Settings.OutsideAir.TempRef / ANALOG_SENSORS_PER_BOARD;
        int sensor = Settings.OutsideAir.TempRef % ANALOG_SENSORS_PER_BOARD;
        refTemp = Settings.AnalogBoard[board].Sensor[sensor].Value;
    }

    if (refTemp == SENSOR_VAL_UNDEFINED) {
        StartTemp = SENSOR_VAL_UNDEFINED;
        return;
    }

    float diff = Settings.OutsideAir.Diff;
    if (Settings.OutsideAir.AboveBelow == 1) {
        diff = -diff;                       /* "below" */
    }

    float startTemp = refTemp + diff;

    if (   (CheckInputs(SW_CLIMACELL_AUTO) || CheckInputs(SW_CLIMACELL_MANUAL))
        && ClimacellClockMode() != CC_OFF
        && Settings.RemoteOff[RO_CLIMACELL] != 1
        && SystemAlarm[AL_CLIMACELL] == 0) {
        float wb = lp_wet_bulb_depression();
        startTemp += wb * (Settings.Climacell.Efficiency / 100.0f);
    }

    StartTemp = startTemp;
}

/* ─── Public API ───────────────────────────────────────────────────── */

void lp_engine_init(void)
{
    for (uint32_t sw = SW_START_STOP; sw < EQ_TOTAL_IO; sw++) {
        Settings.EquipIo[sw].Input  = SW_BIT(sw);
        Settings.EquipIo[sw].Enabled = 1;
    }

    Settings.EquipIo[EQ_POWER].Input          = IO_UNDEFINED;
    Settings.EquipIo[EQ_REFRIG_STANDBY].Input = IO_UNDEFINED;
    Settings.EquipIo[EQ_REMOTE_STANDBY].Input = IO_UNDEFINED;

    /* Bit-mask table for OutputOn/OutputOff. Constellation has no
     * physical shift register; OutputState bits act purely as a
     * desired-state register that equipment_output_sync_task reads
     * and dispatches as Modbus FC05 coil writes to STORAGE. So the
     * specific bit value per port doesn't matter as long as it's
     * unique within the board. Match the legacy MAIN board layout for
     * any future debugging that compares OutputState dumps. */
    IoBoard[MAIN].Output[0] = 0;
    for (uint32_t p = 1; p <= SS_MAIN_IO_OUTPUTS; p++) {
        IoBoard[MAIN].Output[p] = 1UL << (SS_MAIN_IO_OUTPUTS - p);
    }

    /* Same for inputs — CheckInputs() reads `IoBoard[board].Input[port]`
     * as a bitmask, then ANDs it against `IoBoard[board].InputState`.
     * Without this table, every CheckInputs() returns 1 because
     * `(anything & 0) == 0` matches the bitmask comparison. That's
     * what was latching WARN_AIRFLOW + WARN_AUXLOWPLENTEMP at boot.
     *
     * Encoding: port `p` (1..10) → bit `(p-1)`. This matches the
     * STORAGE orbit DI bitmap (`OrbitSample.di_bitmap` bit 0 = DI1)
     * so build_switch_state can merge the two with no shifting. */
    IoBoard[MAIN].Input[0] = 0;
    for (uint32_t p = 1; p <= SS_MAIN_DI_INPUTS; p++) {
        IoBoard[MAIN].Input[p] = 1UL << (p - 1U);
    }

    extern char CurrentMode;
    extern char SystemState;
    CurrentMode = 0;
    SystemState = ST_UNDEFINED;
}

/* PID coefficient refresh — copies operator-tuned Kp/Ki/Kd plus the
 * PWM range bounds out of Settings.* into the engine's PIDCtrl[]
 * working struct. We can't use the legacy PID_Init() here because it
 * also zeroes Timer and PrevError, which would wipe the integrator
 * state every tick. This runs every lp_engine_tick after the mirror_*
 * passes so that Level 2 → PID page changes take effect immediately
 * with no controller reboot. Without this the PID coefficients stay
 * at zero (uninitialized BSS), PIDController returns 0, output gets
 * clamped to PWM_MIN_VALUE, and the home page reports 0% forever. */
static void refresh_pid_coefficients(void)
{
    PIDCtrl[PID_DOOR].Kp       = Settings.Door.PID.P;
    PIDCtrl[PID_DOOR].Ki       = Settings.Door.PID.I;
    PIDCtrl[PID_DOOR].Kd       = Settings.Door.PID.D;
    PIDCtrl[PID_DOOR].RangeMin = PWM_MIN_VALUE;
    PIDCtrl[PID_DOOR].RangeMax = PWM_MAX_VALUE;

    PIDCtrl[PID_REFRIGERATION].Kp       = Settings.Refrig.PID.P;
    PIDCtrl[PID_REFRIGERATION].Ki       = Settings.Refrig.PID.I;
    PIDCtrl[PID_REFRIGERATION].Kd       = Settings.Refrig.PID.D;
    PIDCtrl[PID_REFRIGERATION].RangeMin = PWM_MIN_VALUE;
    PIDCtrl[PID_REFRIGERATION].RangeMax = PWM_MAX_VALUE;

    PIDCtrl[PID_CLIMACELL].Kp       = Settings.Climacell.PID.P;
    PIDCtrl[PID_CLIMACELL].Ki       = Settings.Climacell.PID.I;
    PIDCtrl[PID_CLIMACELL].Kd       = Settings.Climacell.PID.D;
    PIDCtrl[PID_CLIMACELL].RangeMin = 0;
    PIDCtrl[PID_CLIMACELL].RangeMax = 100;

    PIDCtrl[PID_BURNER].Kp       = Settings.Burner.PID.P;
    PIDCtrl[PID_BURNER].Ki       = Settings.Burner.PID.I;
    PIDCtrl[PID_BURNER].Kd       = Settings.Burner.PID.D;
    PIDCtrl[PID_BURNER].RangeMin = PWM_MIN_VALUE;
    PIDCtrl[PID_BURNER].RangeMax = PWM_MAX_VALUE;

    /* Legacy AS2 wires the humidifier PID to the Climacell coefficients
     * (no separate humidifier PID page exists). Match that here. */
    PIDCtrl[PID_HUMIDIFIER].Kp       = Settings.Climacell.PID.P;
    PIDCtrl[PID_HUMIDIFIER].Ki       = Settings.Climacell.PID.I;
    PIDCtrl[PID_HUMIDIFIER].Kd       = Settings.Climacell.PID.D;
    PIDCtrl[PID_HUMIDIFIER].RangeMin = 0;
    PIDCtrl[PID_HUMIDIFIER].RangeMax = 100;
}

/* One-shot at first tick: clear Timer + PrevError so the PID starts
 * with a clean integrator. Coefficient refresh continues every tick
 * via refresh_pid_coefficients(). */
static void seed_pid_state_once(void)
{
    static bool seeded = false;
    if (seeded) return;
    seeded = true;

    for (int i = 0; i < NUM_PID_EQUIP; i++) {
        PIDCtrl[i].Timer     = 0;
        PIDCtrl[i].PrevError = 0xFFFFFFFF;
        PIDCtrl[i].IntError  = 0.0f;
    }
}

/* Mirror operator-set master/slave mode from the LP settings into the
 * legacy `Settings.MasterSlave` global that nova_failures.c watches.
 *
 * Special case: once `MasterBroadcastFailChk` latches the runtime
 * mode to MSMODE_SLAVE_NOBROADCAST (after 10 min of master silence),
 * we MUST NOT clobber it back to MSMODE_SLAVE on the next tick — the
 * NOBROADCAST mode is what tells the UI / display code to indicate
 * "slave using local sensor". `LpRemoteOutside_ApplyProto` clears it
 * back to MSMODE_SLAVE the moment a fresh master push arrives.
 *
 * Operator transitions (Local↔Master↔Slave via UI) always win — when
 * the operator picks a new mode, that's intentional and overrides any
 * latched runtime state. */
static void mirror_master_slave(const LpSettingsData *lp)
{
    unsigned char op_mode = (unsigned char)lp->master_slave.mode;
    if (Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST
        && op_mode == MSMODE_SLAVE) {
        return; /* preserve latched fault state */
    }
    Settings.MasterSlave = op_mode;
}

void lp_engine_tick(void)
{
    const LpSettingsData *lp = LpSettings_DataGet();
    if (lp == NULL) return;

    /* Master 1 Hz clock for the legacy engine. lp_engine_task in
     * main.c calls us at 1 s cadence; bump first so XTimerVal is
     * monotonic-positive on the very first PID arm. */
    uptime_sec++;
    extern unsigned int XTimerVal;
    XTimerVal = uptime_sec;

    mirror_basic_setpoints(lp);
    mirror_cure(lp);
    mirror_refrig(lp);
    mirror_outside_air(lp);
    mirror_burner(lp);
    mirror_ramp(lp);
    mirror_co2(lp);
    mirror_cavity_heat(lp);
    mirror_door(lp);
    mirror_climacell(lp);
    mirror_misc(lp);
    mirror_fan(lp);
    mirror_pid_log(lp);
    mirror_sensors(lp);
    mirror_remote_off(lp);
    mirror_output_map(lp);
    mirror_input_map(lp);
    build_switch_state(lp);

    mirror_master_slave(lp);
    NovaFailures_RunMasterBroadcastChk();

    refresh_pid_coefficients();
    seed_pid_state_once();

    /* Sensor-startup grace window. The orbit boards take 5-15 s after
     * power-on to come up, attach to Modbus TCP, complete the first
     * sensor-RTU sweep, and start publishing analog values to HR 200+.
     * Until then `PlenumTempAvg == SENSOR_VAL_UNDEFINED`, which makes
     * `CheckSystemStatus` latch `ST_FAILURE` (UI_FAILURE) the moment
     * the engine first ticks. The operator sees a "Plenum Sensor
     * Failure" warning + red header that auto-clears in a few seconds
     * — confusing and looks like the system is broken on every reboot.
     *
     * Solution: skip `SetSystemState`/`SetMode` while we're inside a
     * grace window of either (a) up to GRACE_SECS, OR (b) until
     * PlenumTempAvg becomes valid, whichever comes first. While the
     * gate holds, leave SystemState at its prior value (ST_UNDEFINED
     * from `lp_engine_init` on first boot, which the case-default in
     * nova_states.c::SetMode maps to UI_STANDBY — exactly the
     * "warming up, no decisions yet" UX we want).
     *
     * Once the gate releases we never re-enter it for this boot — a
     * mid-run sensor disconnect SHOULD raise the warning. */
    enum { GRACE_SECS = 30U };
    static bool s_grace_done = false;
    if (!s_grace_done) {
        if (uptime_sec >= GRACE_SECS
            || PlenumTempAvg != (float)SENSOR_VAL_UNDEFINED) {
            s_grace_done = true;
        }
    }
    if (!s_grace_done) {
        lp_calc_cooling_available();
        return;
    }

    SetSystemState();
    SetMode();

    /* Reverse-sync: SetRamp() (called inside SetSystemState) and other
     * legacy control code mutate `Settings.Plenum.TempSet` directly to
     * march the live setpoint toward the ramp target. Without syncing
     * those changes back to `lp->plenum.temp_setpoint`, the very next
     * tick's `mirror_basic_setpoints` overwrites the progress and the
     * ramp can never make headway. The lp store is also what the bridge
     * broadcasts on PlenumSettings (tag 40) every 5 s, so this also
     * ensures the UI sees the live value tick by tick. The setter is
     * RAM-only — OSPI persistence runs on its own cadence. */
    if (Settings.Plenum.TempSet != lp->plenum.temp_setpoint) {
        LpSettings_SetPlenumTempSetpoint(Settings.Plenum.TempSet);
    }

    /* DOORS manual override (no RO_DOORS slot in the legacy enum, so
     * `ApplyManualOverrides` doesn't handle it). Run AFTER SetMode so
     * we win against any auto-PID writes for this tick. Equipment
     * Control mapping for the DOORS row:
     *   AUTO   (0) — leave engine in control (no override). On the
     *                  TRANSITION back to AUTO from OFF/MANUAL we
     *                  also force PWM_DOORS to MIN and zero the PID
     *                  state so doors close cleanly first, then the
     *                  engine PID re-evaluates from a fresh
     *                  zero-error baseline next tick. Matches the
     *                  operator expectation that "AUTO closes the
     *                  doors until the system drives them open."
     *   OFF    (1) — SW_FRESHAIR_AUTO is clear (build_switch_state),
     *                  CtrlDoorsPulsed naturally pulses closed. Belt:
     *                  also push PWM_DOORS to MIN so a stray engine
     *                  PID write earlier in the tick can't keep it
     *                  partly open.
     *   MANUAL (2) — drive PWM_DOORS to `Settings.Door.ManualPct`
     *                  (0..100) instead of forcing PWM_MAX. Operator
     *                  enters the target % via a modal on the
     *                  Equipment Control row. ManualPct = 0 closes,
     *                  100 opens, intermediate values hold position. */
    {
        static uint8_t  s_prev_door_ro      = 0U /* AUTO */;
        static uint32_t s_manual_start_tick = 0U;
        uint8_t door_ro = lp->remote_off.state[EQ_DOORS];
        const uint32_t now_tick = XTimerVal;

        /* Auto-revert: if operator set Manual with a non-zero timeout,
         * track when MANUAL started; on the tick `elapsed >= timeout`
         * call LpSettings_SetRemoteOff(EQ_DOORS, AUTO) which next tick
         * will run the MANUAL→AUTO transition path below (close + PID
         * reset). Replaces the legacy door-diag 60-min clear pattern;
         * operator picks the duration in the modal (0 = persistent). */
        if (door_ro == 2U && s_prev_door_ro != 2U) {
            s_manual_start_tick = now_tick;
        }
        if (door_ro == 2U && Settings.Door.ManualTimeoutMins != 0U) {
            const uint32_t timeout_ticks =
                (uint32_t)Settings.Door.ManualTimeoutMins * (uint32_t)T_MINS;
            if (now_tick - s_manual_start_tick >= timeout_ticks) {
                (void)LpSettings_SetRemoteOff((uint32_t)EQ_DOORS,
                                              0U /* AUTO */);
                door_ro = 0U;
            }
        }

        if (door_ro == 1U /* OFF */) {
            PwmChannel[PWM_DOORS].Output = PWM_MIN_VALUE;
        } else if (door_ro == 2U /* MANUAL */) {
            uint32_t pct = Settings.Door.ManualPct;
            if (pct > 100U) pct = 100U;
            PwmChannel[PWM_DOORS].Output =
                (unsigned int)PWM_MIN_VALUE
                + (unsigned int)((pct * (uint32_t)PWM_RANGE + 50U) / 100U);
            OutputOn(EQ_DOORS);
        } else if (door_ro == 0U /* AUTO */ && s_prev_door_ro != 0U) {
            /* MANUAL/OFF → AUTO transition: close + clear PID state.
             * The next engine tick will rebuild PID demand from
             * fresh plenum/setpoint readings without inheriting any
             * stale integral wind-up from the time the operator
             * held the doors at a manual position. */
            PwmChannel[PWM_DOORS].Output = PWM_MIN_VALUE;
            PIDCtrl[PID_DOOR].CurError   = 0.0f;
            PIDCtrl[PID_DOOR].PrevError  = 0.0f;
            PIDCtrl[PID_DOOR].IntError   = 0.0f;
            PIDCtrl[PID_DOOR].Timer      = 0U;
        }
        s_prev_door_ro = door_ro;
    }

    /* BURNER manual override. Burner on Constellation is PWM (4-20 mA),
     * not on/off, so legacy `ApplyManualOverrides` (which calls
     * `OutputOn(EQ_BURNER)`) doesn't drive the analog output. We run
     * AFTER SetMode so we win against any auto-PID writes for this tick.
     * Equipment Control mapping for the BURNER row:
     *   AUTO   (0) — leave engine in control (Mode = Off/Economy/Max
     *                  drives the burner; no override here).
     *   OFF    (1) — force PWM_BURNER to MIN (4 mA) and de-assert
     *                  EQ_BURNER. Belt-and-suspenders against engine
     *                  PID writes earlier in the tick.
     *   MANUAL (2) — force PWM_BURNER to the operator-set Manual %
     *                  (Settings.Burner.Manual, 0..100) and assert
     *                  EQ_BURNER. Mirrors what Settings.Burner.Mode ==
     *                  BURNER_MANUAL would do via CtrlBurner, but
     *                  driven from the operator Equipment Control row
     *                  rather than the Settings page Mode field. */
    {
        uint8_t burner_ro = lp->remote_off.state[EQ_BURNER];
        if (burner_ro == 1U /* OFF */) {
            PwmChannel[PWM_BURNER].Output = PWM_MIN_VALUE;
            OutputOff(EQ_BURNER);
        } else if (burner_ro == 2U /* MANUAL */) {
            PwmChannel[PWM_BURNER].Output =
                PercentToPWMVal(Settings.Burner.Manual);
            OutputOn(EQ_BURNER);
        }
    }

    lp_calc_cooling_available();

    /* ─── Pulse-door tick (1 Hz) — Nova port of legacy AS2 ─────────────
     *
     * The AS2 board ran `SerialShiftTimerISR` (Timer.c:191) at 4 Hz; the
     * pulse-door block gated on `PulseCount == 4` so it effectively ran
     * at 1 Hz. It decremented `PulseDoorMove` by 1 per second and drove
     * the open/close pulse coils during travel. The ISR was removed in
     * the Nova port (no shift register on AM2434) but the globals
     * `PulseDoorMove` / `PulseDoorPosition` / `PulseDoor` still exist
     * and `CtrlDoors()`'s PID gate still checks
     * `(Counter < Timer || PulseDoorMove != 0)`.
     *
     * Without this tick, the first `CtrlDoorsPulsed()` call after a PID
     * fire sets `PulseDoorMove > 0`, and because nothing ever decrements
     * it back to zero, the PID gate locks forever. Bench symptom on
     * 0.A.224: fresh-air door output climbs to ~5% in the first few
     * seconds after boot, then never moves again for 16+ hours (the only
     * unblock path is the 02:00 daily `CtrlDoorsPulsed_Init`, which
     * re-arms the counter but doesn't fix the underlying drain).
     *
     * Direct 1:1 port of the legacy block (Timer.c:201-244). Engine
     * already ticks 1 Hz so no PulseCount subdivision needed. Both this
     * block and `CtrlDoorsPulsed()` run from `lp_engine_task` so
     * `PulseDoorFlag` is preserved for legacy faithfulness only — there
     * is no actual race to guard against in Nova. */
    if (PulseDoorFlag == 0) {
        const unsigned int doorOutput = PwmChannel[PWM_DOORS].Output;
        if (doorOutput == PWM_MAX_VALUE
            && PulseDoorMove == 0 && PulseDoorInit == 0) {
            /* Steady OPEN pulse when commanded to max. */
            OutputOn (EQ_PULSEDOOR_OPEN);
            OutputOff(EQ_PULSEDOOR_CLOSE);
        } else if (doorOutput == PWM_MIN_VALUE
                   && PulseDoorMove == 0 && PulseDoorInit == 0) {
            /* Steady CLOSE pulse when commanded to min. */
            OutputOn (EQ_PULSEDOOR_CLOSE);
            OutputOff(EQ_PULSEDOOR_OPEN);
        } else if (PulseDoorMove != 0) {
            /* Travel in progress: drive the direction coil, decrement
             * the remaining travel time, advance the position tracker. */
            PulseDoorFlag = 1;
            OutputOn(PulseDoor);
            PulseDoorMove--;
            if (PulseDoor == EQ_PULSEDOOR_OPEN) {
                PulseDoorPosition++;
                OutputOff(EQ_PULSEDOOR_CLOSE);
            } else {
                PulseDoorPosition--;
                OutputOff(EQ_PULSEDOOR_OPEN);
            }
            PulseDoorFlag = 0;
        } else {
            /* Idle between commands: both coils off. */
            OutputOff(EQ_PULSEDOOR_OPEN);
            OutputOff(EQ_PULSEDOOR_CLOSE);
        }
    }
}

unsigned char lp_engine_get_current_mode(void)
{
    extern char CurrentMode;
    return (unsigned char)CurrentMode;
}

/* ─── Stubs for engine externals not supplied by linked TUs ────────── */

/* The engine calls UI_SendRuntimes from nova_states.c when the
 * schedule advances. On the LP we don't push runtimes via that legacy
 * path — they go out via SystemStatus snapshots — so a no-op is
 * correct. */
void UI_SendRuntimes(void) { }

/* The engine asks "is any VFD faulted?" via this predicate from
 * nova_failures.c. LP doesn't drive VFDs through the legacy fault
 * pipeline yet — return false (no faults) until the orbit's actual
 * VFD status is wired in. */
bool nova_vfd_any_faulted(unsigned char *out_drive_idx,
                          unsigned short *out_fault_code)
{
    if (out_drive_idx != NULL)  *out_drive_idx  = 0;
    if (out_fault_code != NULL) *out_fault_code = 0;
    return false;
}

/* ─── nova_controls.c is now linked (Phase A3) ─────────────────────── */
/* Real PID + Ctrl* + ApplyManualOverrides bodies live in
 * Nova_Firmware/Platform/nova_controls.c. The shim no longer stubs them.
 * One small no-op remains because its backing module isn't linked on
 * the LP yet:
 *   - hal_pwm_pulse_width_set: CONTROLLER LP has no on-board PWM. PWM
 *     intent reaches the orbit boards via Modbus AO writes (see Phase
 *     A4 / GDC orbit). Engine pokes PwmChannel[].Output through this
 *     hook on every CtrlFan/CtrlBurner update; ignoring it costs us
 *     nothing because equipment_output_sync_task currently only
 *     dispatches DOs, not AOs. */

/* ─── PidLogStream typed proto channel ─────────────────────────────── */
/* Replaces the legacy SD-card PIDLog. The engine task calls
 * `PIDLogWrite` from `nova_controls.c::Pid()` whenever Settings.Log.PID
 * has logging enabled for the loop (gated above in `Pid()`; mirror in
 * `mirror_pid_log()` keeps that gate populated). We push the record
 * into a small SPSC ring; the data-exchange task drains via
 * `lp_pidlog_drain_one()` and emits a `PidLogRecord` envelope (tag 72)
 * per drained entry.
 *
 * Cross-task: engine task writes head, data-exchange task writes tail.
 * Single-producer / single-consumer, so volatile head/tail indices
 * with no lock are safe on the R5F (single-issue, atomic 32-bit store).
 *
 * Capacity: 32 entries. Worst case is U=2 s on the door PID + U=15 s
 * on refrig → ~0.6 records/sec. Drain runs every 100 ms (data-exchange
 * tick), so the ring should never reach more than 1-2 entries deep
 * outside of cold-boot bursts. Records dropped on overflow are
 * counted in `s_pidlog_dropped` for /health observability later. */
typedef struct {
    uint32_t epoch_sec;   /* time(NULL) at enqueue, 0 if RTC not authoritative */
    uint32_t loop_index;  /* legacy "Type" param (PID_DOOR / PID_REFRIGERATION) */
    float    p_term;
    float    i_term;
    float    d_term;
    int32_t  output;
    float    error;
    uint32_t sequence;    /* monotonic per-loop counter for gap detection */
} PidLogEntry;

#define PIDLOG_RING_CAP 32U
static volatile uint32_t s_pidlog_head = 0U;   /* producer */
static volatile uint32_t s_pidlog_tail = 0U;   /* consumer */
static PidLogEntry       s_pidlog_ring[PIDLOG_RING_CAP];
static uint32_t          s_pidlog_seq[2] = { 0U, 0U };  /* per-loop counters; index by loop_index <= 1 */
static uint32_t          s_pidlog_dropped = 0U;

int PIDLogWrite(char Type, float P, float I, float D, int Output, float Error)
{
    uint32_t head = s_pidlog_head;
    uint32_t next = (head + 1U) % PIDLOG_RING_CAP;
    if (next == s_pidlog_tail) {
        /* Full — drop oldest by advancing tail (consumer might race with us
         * but worst case it skips one record, which is also acceptable). */
        s_pidlog_dropped++;
        return -1;
    }

    PidLogEntry *e = &s_pidlog_ring[head];
    e->epoch_sec  = LpRtc_IsAuthoritative() ? (uint32_t)time(NULL) : 0U;
    e->loop_index = (uint32_t)(uint8_t)Type;
    e->p_term     = P;
    e->i_term     = I;
    e->d_term     = D;
    e->output     = (int32_t)Output;
    e->error      = Error;

    /* Per-loop monotonic sequence; only PID_DOOR (0) and PID_REFRIGERATION (1)
     * are gated to log today (see legacy nova_controls.c::Pid()). Guard the
     * array bound so a future addition that forgets to extend the array
     * fails closed (sequence sticks at 0) rather than scribbling memory. */
    if (e->loop_index < (sizeof(s_pidlog_seq) / sizeof(s_pidlog_seq[0]))) {
        e->sequence = ++s_pidlog_seq[e->loop_index];
    } else {
        e->sequence = 0U;
    }

    s_pidlog_head = next;   /* publish */
    return 0;
}

/* Called by main.c data-exchange loop. Returns 1 on success (entry
 * written to *out), 0 if ring is empty. */
int lp_pidlog_drain_one(uint32_t *out_epoch_sec, uint32_t *out_loop_index,
                        float *out_p, float *out_i, float *out_d,
                        int32_t *out_output, float *out_error,
                        uint32_t *out_sequence)
{
    uint32_t tail = s_pidlog_tail;
    if (tail == s_pidlog_head) return 0;

    const PidLogEntry *e = &s_pidlog_ring[tail];
    *out_epoch_sec  = e->epoch_sec;
    *out_loop_index = e->loop_index;
    *out_p          = e->p_term;
    *out_i          = e->i_term;
    *out_d          = e->d_term;
    *out_output     = e->output;
    *out_error      = e->error;
    *out_sequence   = e->sequence;

    s_pidlog_tail = (tail + 1U) % PIDLOG_RING_CAP;
    return 1;
}

void hal_pwm_pulse_width_set(unsigned int out, unsigned int legacy_ticks)
{
    (void)out; (void)legacy_ticks;
}

/* nova_rtc.c stub. Engine calls GetTime() once per RunClockMode() to
 * pick the half-hour slot. We feed it the LP wall-clock (set by the
 * bridge via DateTimeUpdate). If the clock has no baseline yet, we
 * return 0 — the engine then reads slot 0, which a sane operator will
 * have at RC_STANDBY by default. */
int GetTime(char *Hour, char *Minute, char *Second)
{
    if (LpRtc_IsAuthoritative()) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (tm != NULL) {
            if (Hour)   *Hour   = (char)tm->tm_hour;
            if (Minute) *Minute = (char)tm->tm_min;
            if (Second) *Second = (char)tm->tm_sec;
            return 0;
        }
    }
    if (Hour)   *Hour   = 0;
    if (Minute) *Minute = 0;
    if (Second) *Second = 0;
    return 0;
}
