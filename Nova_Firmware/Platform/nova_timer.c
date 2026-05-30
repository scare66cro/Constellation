/* nova_timer.c
 *
 * Nova-native implementation of the legacy Application/Timer.c module.
 *
 * MIGRATION STATUS (Phase 4 of legacy → Nova-native):
 *   Replaces docs/legacy_AS2_reference/Application/Timer.c.  Public
 *   API in Timer.h is unchanged: same globals (SystemAlarm[],
 *   AlarmTimer[], IntervalTimer[], MainTimer[], XTimerVal,
 *   LightStatus) and same function signatures.  All callers — both
 *   the still-compiled legacy modules (DataExc, States, StorePostData,
 *   ThreadMonitor, ThreadSerialCom) and the Nova-side modules
 *   (nova_dataexc, nova_thread_overrides, nova_failures) — link
 *   without source changes.
 *
 * Differences vs legacy (TM4C → AM2434):
 *   • SerialShiftTimer_Init / SerialShiftTimerISR are no-ops.  The
 *     pulse-door + status-light tick (1 Hz) has been migrated to
 *     LP-side handling, where it runs from the data-exchange tick
 *     rather than a TIMER2A hardware interrupt.
 *   • WatchdogExternal_Init / Start / Stop are no-ops.  The CPLD
 *     heartbeat GPIO toggle isn't required on AM2434 (the real
 *     watchdog is hal_watchdog).  Keeping the API present so legacy
 *     ThreadMonitor.c and friends still link.
 *   • WatchdogInternal_Init  → hal_watchdog_init()
 *     WatchdogInternalReset → hal_watchdog_feed()
 *     Routes the legacy Tiva-watchdog API to the AM2434 RTI watchdog HAL.
 *
 * Behaviour preserved bit-for-bit:
 *   • ClearAlarms(soft):  AL_AUXLOWPLENTEMP is preserved across soft
 *     resets (Gellert intentionally requires a hard reset / site visit
 *     to clear the aux-low-plenum-temp alarm), and AL_NOBROADCAST
 *     timer is rearmed to XTimerVal.
 *   • ClearIntervalTimers: IT_SHUTDOWN is preserved.
 *   • DailyJobs: 02:00 wall-clock trigger calls CtrlDoorsPulsed_Init()
 *     exactly once per minute via the DailyReset latch.
 *   • MainTimer_Init: snapshots XTimerVal into all MT_NUM_TIMERS slots.
 *   • XTimer: latches uptime_sec into XTimerVal and returns it.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hal.h"
#include "Controls.h"      /* CtrlDoorsPulsed_Init */
#include "RTC.h"           /* GetTime */
#include "Timer.h"

/* ─── Public globals (declared extern in Timer.h) ─────────────────── */
unsigned char SystemAlarm[NUM_ALARMS];
unsigned int  AlarmTimer[NUM_ALARMS];
unsigned int  IntervalTimer[IT_NUM_TIMERS];
unsigned int  MainTimer[MT_NUM_TIMERS];

unsigned int  XTimerVal = 0;

LIGHT_STATUS  LightStatus;

/* uptime_sec is the master tick from Platform/main.c, advanced by
 * hal_timer.c. */
extern unsigned int uptime_sec;

/* ─── Module-local state ──────────────────────────────────────────── */
static char DailyReset = 0;

/* ─────────────────────────────────────────────────────────────────── */

void ClearAlarms(char soft)
{
    int i;
    for (i = 0; i < NUM_ALARMS; i++) {
        if (soft == 1 && i == AL_AUXLOWPLENTEMP) {
            continue;   /* preserved across soft resets — Gellert spec */
        }
        SystemAlarm[i] = 0;
        AlarmTimer[i]  = 0;
    }
    AlarmTimer[AL_NOBROADCAST] = XTimerVal;
}

void ClearIntervalTimers(void)
{
    int i;
    for (i = 0; i < IT_NUM_TIMERS; i++) {
        if (i != IT_SHUTDOWN) {
            IntervalTimer[i] = 0;
        }
    }
}

void DailyJobs(void)
{
    char Hour = 255, Min = 255, Sec = 255;

    GetTime(&Hour, &Min, &Sec);
    if (Hour == 2 && Min == 0 && DailyReset == 0) {
        CtrlDoorsPulsed_Init();
        DailyReset = 1;     /* latch — don't fire again until 02:01+ */
    } else {
        DailyReset = 0;
    }
}

void MainTimer_Init(void)
{
    int i;
    unsigned int Counter = XTimerVal;
    for (i = 0; i < MT_NUM_TIMERS; i++) {
        MainTimer[i] = Counter;
    }
}

/* ─── No-op shims for TM4C-specific peripheral setup ──────────────── */

void SerialShiftTimer_Init(void)
{
    /* TM4C used TIMER2A @ 4 Hz to drive pulse-door + status-light
     * outputs.  On Nova that runs from nova_thread_overrides
     * (PulseDoorTick / LightTick) at the data-exchange cadence. */
}

void WatchdogExternal_Init(void)  { /* no CPLD heartbeat on AM2434 */ }
void WatchdogExternalStart(void)  { /* no-op */ }
void WatchdogExternalStop(void)   { /* no-op */ }

void WatchdogInternal_Init(void)
{
    hal_watchdog_init();
}

void WatchdogInternalReset(void)
{
    hal_watchdog_feed();
}

unsigned int XTimer(void)
{
    XTimerVal = uptime_sec;
    return XTimerVal;
}

/*** End of file ***/
