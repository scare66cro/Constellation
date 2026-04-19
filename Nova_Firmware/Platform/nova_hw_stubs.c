/*
 * nova_hw_stubs.c — Stubs for hardware functions that don't exist on Nova
 *
 * The AS2 firmware has several functions tied to physical hardware
 * (CPLD bit-bang, external watchdog) that are called from Application
 * code we can't modify. These stubs provide link-time resolution.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"
#include "SerialShift.h"
#include "FreeRTOS.h"
#include "task.h"

#include <reent.h>

/* ─── Thread-safe newlib locks ────────────────────────────────────── */
/*
 * newlib calls __malloc_lock/__malloc_unlock around heap operations.
 * The default libnosys stubs are no-ops, which is unsafe with
 * preemptive FreeRTOS scheduling.  Use vTaskSuspendAll/xTaskResumeAll
 * to prevent context switches during malloc/free without disabling IRQs.
 *
 * BUG FIX: __malloc_unlock must check != taskSCHEDULER_NOT_STARTED
 * (not == taskSCHEDULER_RUNNING), because after vTaskSuspendAll() the
 * state is taskSCHEDULER_SUSPENDED, and failing to call xTaskResumeAll
 * leaves uxSchedulerSuspended permanently stuck at 1.
 */
void __malloc_lock(struct _reent *r)
{
    (void)r;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        vTaskSuspendAll();
}

void __malloc_unlock(struct _reent *r)
{
    (void)r;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xTaskResumeAll();
}

/*
 * ReadInput — was in ThreadSerialShift.c, reads CPLD shift register.
 * Called by SerialShift_Init() to detect CPLD version.
 * On Nova, return 0x02 (version 2) to trigger v2 initialization paths.
 */
unsigned int ReadInput(_pin_str CS, unsigned int inputs)
{
    (void)CS;
    (void)inputs;
    /* Return CPLD version 2 in the low 4 bits */
    return 0x02;
}

/*
 * SetOutput — was in ThreadSerialShift.c, writes to CPLD shift register.
 * Called by SerialShift code. No-op on Nova (outputs go via Modbus TCP).
 */
void SetOutput(_pin_str CS, unsigned int outputs, unsigned int state)
{
    (void)CS;
    (void)outputs;
    (void)state;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Legacy UI_Messages.c / DataExc.c stubs                                 */
/*                                                                         */
/* The Mini_IO Application code (StorePostData.c, ThreadUIUpdate.c, etc.) */
/* calls these functions which were defined in DataExc.c and UI_Messages.c.*/
/* On Nova they're replaced by the nova_* protobuf layer. These stubs     */
/* provide link-time resolution so Application code compiles unchanged.    */
/* ═══════════════════════════════════════════════════════════════════════ */

#include "nova_messages.h"
#include <stddef.h>

/* ─── UI_Send* functions (from UI_Messages.c) ─────────────────────────── */
/* In Nova, data is pushed via protobuf. These stubs redirect to the
 * NovaMsg equivalents where possible, or no-op where the data flow is
 * handled differently (push-based instead of pull-based). */

void UI_SendMain(void)             { NovaMsg_SendSystemStatus(); }
void UI_SendMode(void)             { NovaMsg_SendMode(); }
void UI_SendEquipStatus(void)      { NovaMsg_SendEquipmentStatus(); }
void UI_SendDateTime(void)         { NovaMsg_SendDateTime(); }
void UI_SendBasicSetup(void)       { NovaMsg_SendBasicSetup(); }
void UI_SendPlenSetPoints(void)    { NovaMsg_SendPlenumSettings(); }
void UI_SendFanSpeed(void)         { NovaMsg_SendFanSpeedSettings(); }
void UI_SendFanBoost(void)         { NovaMsg_SendFanBoostSettings(); }
void UI_SendRampRate(void)         { NovaMsg_SendRampRateSettings(); }
void UI_SendRefrig(void)           { NovaMsg_SendRefrigSettings(); }
void UI_SendBurner(void)           { NovaMsg_SendBurnerSettings(); }
void UI_SendCo2(void)              { NovaMsg_SendCo2Settings(); }
void UI_SendAirCure(void)          { NovaMsg_SendCureSettings(); }
void UI_SendClimacell(void)        { NovaMsg_SendClimacellSettings(); }
void UI_SendClimacellTimes(void)   { NovaMsg_SendClimacellTimes(); }
void UI_SendHumCtrl(void)          { NovaMsg_SendHumidCtrlSettings(); }
void UI_SendOutsideAir(void)       { NovaMsg_SendOutsideAirSettings(); }
void UI_SendMisc(void)             { NovaMsg_SendMiscSettings(); }
void UI_SendFailures(void)         { NovaMsg_SendFailureSettings(); }
void UI_SendFailures2(void)        { NovaMsg_SendFailureSettings2(); }
void UI_SendTempDevAlarms(void)    { NovaMsg_SendTempAlarmSettings(); }
void UI_SendDoor(void)             { NovaMsg_SendDoorSettings(); }
void UI_SendLoadMonitor(void)      { NovaMsg_SendLoadMonitorSettings(); }
void UI_SendAuxProgram(void)       { NovaMsg_SendAuxProgram(); }
void UI_SendUserLogSettings(void)  { NovaMsg_SendUserLogSettings(); }
void UI_SendGraphFavorites(void)   { NovaMsg_SendGraphFavorites(); }
void UI_SendEmail(void)            { NovaMsg_SendEmailSettings(); }
void UI_SendEmailAlertFlags(void)  { NovaMsg_SendAlertSettings(); }
void UI_SendPWMChannels(void)      { NovaMsg_SendPwmSettings(); }
void UI_SendIoConfig(void)         { NovaMsg_SendIoConfig(); }
void UI_SendIoDefinition(void)     { NovaMsg_SendIoDefinition(); }
void UI_SendAvailableIo(void)      { NovaMsg_SendAvailableIo(); }
void UI_SendAnalogBoard(void)      { /* Board data pushed on change */ }
void UI_SendSensorData(void)       { NovaMsg_SendSensorData(); }
void UI_SendSensorLabels(void)     { NovaMsg_SendSensorLabels(); }
void UI_SendRuntimes(void)         { NovaMsg_SendRuntimes(); }
void UI_SendFanDailyRun(void)      { NovaMsg_SendFanRuntime(); }
void UI_SendFanTotalRun(void)      { NovaMsg_SendFanRuntime(); }
void UI_SendHumidModes(void)       { NovaMsg_SendHumidModes(); }
void UI_SendAuxSwitches(void)      { NovaMsg_SendAuxSwitches(); }
void UI_SendAccounts(void)         { NovaMsg_SendAccountSettings(); }
void UI_SendNetworkNodes(void)     { NovaMsg_SendNetworkNodes(); }
void UI_SendService(void)          { NovaMsg_SendServiceInfo(); }
void UI_SendWarnings(void)         { NovaMsg_SendWarnings(); }
void UI_SendAllSettings(void)      { NovaMsg_SendAllSettings(); }

/* Functions with specific signatures or side effects */
void UI_SendLtxInit(void)          { /* No LTX on Nova */ }
void UI_SendHttpPort(void)         { /* No LTX on Nova */ }
void UI_SendNetMonMode(void)       { /* No LTX on Nova */ }
void UI_SendPgmLevel(void)         { /* No PGM level on Nova */ }
void UI_SendSettingsAck(void)      { NovaMsg_SendAck(0, 0); }
void UI_SendEquipTranslateAck(void){ NovaMsg_SendAck(0, 0); }

/* Some functions from UI_Messages.c with different signatures: */
void UI_SendVersions(void)         { NovaMsg_SendVersionInfo(); }

/* ─── Data Exchange / Messaging (from DataExc.c) ──────────────────────── */
/* These manage the old RTS/ACK message queue and POST parsing.
 * On Nova, POST data arrives as protobuf SettingsUpdate messages.
 * The Application code still calls these during settings store operations;
 * we provide no-op / dummy implementations. */

/* Message types from DataExc.c */
typedef struct { int unused; } UI_MSG;

static char dummy_tag[4] = "";
static char dummy_value[4] = "";

char *PostTag(int index)                  { (void)index; return dummy_tag; }
char *PostValue(int index)                { (void)index; return dummy_value; }
int   GetNumPostItems(void)               { return 0; }
char *GetPostValueByField(const char *f)  { (void)f; return dummy_value; }
int   ParsePost(const char *data)         { (void)data; return 0; }
void  FreeMsgQueue(void)                  { }
void  ProcessMsgQueue(void)               { }
void  ProcessUIMessage(void)              { }
int   SendMsgAndWaitForResponse(void *msg, int timeout)
{
    (void)msg; (void)timeout; return 0;
}
void  MessagingStatus(int s)              { (void)s; }
void  UI_Message(int type, void *data, int len)
{
    (void)type; (void)data; (void)len;
}

/* Uptime counter referenced by nova_messages.c heartbeat encoder.
 * TODO: increment from a 1Hz timer (e.g. ThreadUIUpdate) for accurate
 * heartbeat reporting.  Stubbed to 0 keeps protocol valid (proto3 zero
 * default) until that wiring lands. */
uint32_t UptimeSeconds = 0;

/* Post reply functions called by StorePostData.c */
void AnalogBoardPostReply(void) { }
void AuxProgramPostReply(void)  { }
