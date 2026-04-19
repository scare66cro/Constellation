/*
 * nova_thread_overrides.c — Nova-specific thread implementations
 *
 * These functions have the SAME signatures as the Mini_IO/Application
 * versions (ThreadSerialShift, ThreadSerialCom, ReadAnalogBoards, etc.)
 * and are linked FIRST so they override the AS2 implementations.
 *
 * The Application layer calls these by name — by providing them here,
 * the linker picks our Modbus TCP versions instead of the CPLD/RS485 ones.
 *
 * Thread structure is identical to AS2:
 *   - serial_shift_thread → polls Orbit DI/DO via Modbus TCP (was CPLD bit-bang)
 *   - system_control_thread → calls ReadAnalogBoards + SetSystemState (unchanged)
 *   - ReadAnalogBoards → reads sensor data from Orbit passthrough regs (was RS485)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "FreeRTOS.h"
#include "task.h"

#include "hal_orbit.h"
#include "hal_modbus_tcp.h"
#include "SerialShift.h"
#include "ThreadSerialShift.h"
#include "ThreadSerialCom.h"
#include "ThreadUIUpdate.h"
#include "ThreadMonitor.h"
#include "ThreadFileReceive.h"
#include "Analog_Input.h"
#include "Controls.h"
#include "DataExc.h"
#include "RTC.h"
#include "Settings.h"
#include "States.h"
#include "StorePostData.h"
#include "Timer.h"
#include "PWM.h"
#include "UI_Messages.h"
#include "Usart.h"
#include "Warnings.h"
#include "SDCard.h"
#include "debug.h"
#include "nova_settings_store.h"
#include "nova_protocol.h"
#include <math.h>
#include "nova_dataexc.h"
#include "hal.h"             /* LTX_UART, hal_uart_send_char */
#include <string.h>
#include <stdio.h>

/* ─── External references ─────────────────────────────────────────── */

/* From SerialShift.c (still linked from Mini_IO — we only override threads) */
extern BOARD_INFO IoBoard[BOARD_COUNT];

/* Provide the SerialShift info struct (was in ThreadSerialShift.c) */
THREAD_SERIAL_SHIFT_INFO SerialShift;

/* From Settings.c */
/* Settings is declared via Settings.h include */

/* ─── Nova bridge (UART1 ↔ RPi5) wiring ───────────────── */
/*
 * The bridge speaks COBS+protobuf framing on UART1.  Without this
 * wiring NovaProto_SendRaw() has no tx callback and every NovaMsg_*
 * send becomes a no-op, while incoming bridge bytes pile up unread
 * in the legacy UsartRx ring buffer.  This was the missing link that
 * made every settings save POST time out with msg=90 (SettingsUpdate).
 */
static void nova_uart_tx(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        hal_uart_send_char((hal_uart_port_t)LTX_UART, data[i]);
    }
}

void NovaBridge_Init(void)
{
    /* Register tx callback first; NovaDataExc_Init then sets the rx
     * dispatcher (it preserves the tx_func we just installed). */
    NovaProto_Init(nova_uart_tx, NULL);
    NovaDataExc_Init();
    debug_printf("[Nova] Bridge protocol wired (COBS+protobuf on UART1)\r\n");
}

/* ─── SerialShift_Init override ───────────────────────────────────── */
/*
 * Replaces the CPLD bit-bang initialization with Orbit-compatible setup.
 * The AS2 version reads the CPLD version by clocking shift registers —
 * in QEMU there is no CPLD, so that reads version 0 and skips all switch
 * mappings.  We force v2 layout and pre-set the default switch states
 * so the system boots into a running state (Start=ON, FanAuto=ON, etc.).
 *
 * Controls.c / States.c / GetEquipStatus() all work unchanged because
 * they read IoBoard[].InputState bitmask positions, which we populate
 * identically to the v2 CPLD layout.
 */
void SerialShift_Init(void)
{
    int i;

    memset(IoBoard, 0, sizeof(IoBoard));

    /* ── Main board (Orbit #1) ─────────────────────────────────── */
    IoBoard[MAIN].Version = 0x02;       /* Constellation v2 */
    IoBoard[MAIN].SwitchesActive = 1;
    IoBoard[MAIN].Fault = 0;
    strcpy(IoBoard[MAIN].Name, "Main");

    IoBoard[MAIN].InputShiftLength  = SS_MAIN_IO_INPUTS_V2;
    IoBoard[MAIN].OutputShiftLength = SS_MAIN_IO_OUTPUTS;
    IoBoard[MAIN].NumInputs         = SS_MAIN_DI_INPUTS;
    IoBoard[MAIN].NumOutputs        = 10;
    IoBoard[MAIN].NumPwms           = MAIN_PWM_OUTPUTS;

    /* Digital output bitmask array (1-based, matches SS_MAIN_DO layout) */
    IoBoard[MAIN].Output[1]  = SS_MAIN_DO1;
    IoBoard[MAIN].Output[2]  = SS_MAIN_DO2;
    IoBoard[MAIN].Output[3]  = SS_MAIN_DO3;
    IoBoard[MAIN].Output[4]  = SS_MAIN_DO4;
    IoBoard[MAIN].Output[5]  = SS_MAIN_DO5;
    IoBoard[MAIN].Output[6]  = SS_MAIN_DO6;
    IoBoard[MAIN].Output[7]  = SS_MAIN_DO7;
    IoBoard[MAIN].Output[8]  = SS_MAIN_DO8;
    IoBoard[MAIN].Output[9]  = SS_MAIN_PD_DN;   /* Door Close */
    IoBoard[MAIN].Output[10] = SS_MAIN_PD_UP;   /* Door Open */

    /* Digital input bitmask array (v2 layout) */
    IoBoard[MAIN].Input[1]  = SS_MAIN_DI1_V2;
    IoBoard[MAIN].Input[2]  = SS_MAIN_DI2_V2;
    IoBoard[MAIN].Input[3]  = SS_MAIN_DI3_V2;
    IoBoard[MAIN].Input[4]  = SS_MAIN_DI4_V2;
    IoBoard[MAIN].Input[5]  = SS_MAIN_DI5_V2;
    IoBoard[MAIN].Input[6]  = SS_MAIN_DI6_V2;
    IoBoard[MAIN].Input[7]  = SS_MAIN_DI7_V2;
    IoBoard[MAIN].Input[8]  = SS_MAIN_DI8_V2;
    IoBoard[MAIN].Input[9]  = SS_MAIN_AIRFLOW_V2;
    IoBoard[MAIN].Input[10] = SS_MAIN_LOWTEMP_V2;

    /* Switch inputs ── no outputs for switches */
    for (i = SW_START_STOP; i < EQ_TOTAL_IO; i++) {
        Settings.EquipIo[i].Output = IO_UNDEFINED;
    }

    /* v2 switch-to-InputState bitmask mappings.
     * Same as the AS2 SerialShift_Init case 2/3. */
    Settings.EquipIo[SW_AUX1_MANUAL].Input     = SS_MAIN_SW2;
    Settings.EquipIo[SW_AUX1_AUTO].Input        = SS_MAIN_SW3;
    Settings.EquipIo[SW_AUX2_MANUAL].Input     = SS_MAIN_SW4;
    Settings.EquipIo[SW_AUX2_AUTO].Input        = SS_MAIN_SW5;
    Settings.EquipIo[SW_START_STOP].Input       = SS_MAIN_SW8;
    Settings.EquipIo[SW_REFRIG_AUTO].Input      = SS_MAIN_SW9;
    Settings.EquipIo[SW_FAN_MANUAL].Input       = SS_MAIN_SW10;
    Settings.EquipIo[SW_FAN_AUTO].Input         = SS_MAIN_SW11;
    Settings.EquipIo[SW_CLIMACELL_MANUAL].Input = SS_MAIN_SW12;
    Settings.EquipIo[SW_CLIMACELL_AUTO].Input   = SS_MAIN_SW13;
    Settings.EquipIo[SW_HUMID_MANUAL].Input     = SS_MAIN_SW14;
    Settings.EquipIo[SW_HUMID_AUTO].Input       = SS_MAIN_SW15;
    Settings.EquipIo[SW_FRESHAIR_AUTO].Input    = SS_MAIN_SW16;
    Settings.EquipIo[SW_FRESHAIR_MANUAL].Input  = SS_MAIN_SW17;

    /* Onion aliases */
    Settings.EquipIo[SW_CURE_AUTO].Input   = Settings.EquipIo[SW_HUMID_AUTO].Input;
    Settings.EquipIo[SW_BURNER_AUTO].Input = Settings.EquipIo[SW_CLIMACELL_AUTO].Input;

    /* ── Default equipment-to-output mappings (Orbit Main board) ── */
    /* Equipment output port IDs use SS_PORT_ID_MULTIPLIER (12) × board + output.
     * Board 0 (MAIN), outputs 1-11 → port IDs 1-11.
     *
     * Per operator (Apr 2026):
     *   DO1  EQ_REDLIGHT      (warning lamp)
     *   DO2  EQ_YELLOWLIGHT   (warning lamp)
     *   DO3  EQ_FAN           (greenlight wired in parallel — no firmware
     *                          entity; relay click that energizes the fan
     *                          also energizes the green run-indicator)
     *   DO4  EQ_CLIMACELL
     *   DO5  EQ_HUMID_HEAD1
     *   DO6  EQ_HUMID_PUMP1
     *   DO7  EQ_REFRIG_STAGE1 (only stages drive refrig — main
     *                          EQ_REFRIGERATION contactor is not wired)
     *   DO8  EQ_HEAT
     *   DO9  EQ_PULSEDOOR_OPEN
     *   DO10 EQ_PULSEDOOR_CLOSE
     *
     * Disabled / IO_UNDEFINED by default (operator can map via Level 2
     * IO Config if hardware is added later):
     *   EQ_REFRIGERATION  — superseded by REFRIG_STAGE1
     *   EQ_DOORS (PWM damper) — superseded by pulsed open/close
     *
     * These are *only defaults* — the Level 2 IO Config UI may remap
     * any equipment to any board+port, and OutputOn/OutputOff pick up
     * the new mapping immediately via Settings.EquipIo[].Output. */
    Settings.EquipIo[EQ_REDLIGHT].Output         = 1;   /* DO1 */
    Settings.EquipIo[EQ_REDLIGHT].Enabled        = 1;
    Settings.EquipIo[EQ_YELLOWLIGHT].Output      = 2;   /* DO2 */
    Settings.EquipIo[EQ_YELLOWLIGHT].Enabled     = 1;
    Settings.EquipIo[EQ_FAN].Output              = 3;   /* DO3 (+greenlight wired in parallel) */
    Settings.EquipIo[EQ_FAN].Enabled             = 1;
    Settings.EquipIo[EQ_CLIMACELL].Output        = 4;   /* DO4 */
    Settings.EquipIo[EQ_CLIMACELL].Enabled       = 1;
    Settings.EquipIo[EQ_HUMID_HEAD1].Output      = 5;   /* DO5 */
    Settings.EquipIo[EQ_HUMID_HEAD1].Enabled     = 1;
    Settings.EquipIo[EQ_HUMID_PUMP1].Output      = 6;   /* DO6 */
    Settings.EquipIo[EQ_HUMID_PUMP1].Enabled     = 1;
    Settings.EquipIo[EQ_REFRIG_STAGE1].Output    = 7;   /* DO7 */
    Settings.EquipIo[EQ_REFRIG_STAGE1].Enabled   = 1;
    Settings.EquipIo[EQ_HEAT].Output             = 8;   /* DO8 */
    Settings.EquipIo[EQ_HEAT].Enabled            = 1;
    Settings.EquipIo[EQ_PULSEDOOR_OPEN].Output   = 9;   /* DO9 */
    Settings.EquipIo[EQ_PULSEDOOR_OPEN].Enabled  = 1;
    Settings.EquipIo[EQ_PULSEDOOR_CLOSE].Output  = 10;  /* DO10 */
    Settings.EquipIo[EQ_PULSEDOOR_CLOSE].Enabled = 1;

    /* Pulse-door power flag: Settings.c default sets this to 1 already to
     * indicate the pulse-door subsystem is enabled (it is NOT a real port
     * ID — CtrlDoors() only checks for non-zero).  Explicit here for
     * clarity. */
    Settings.EquipIo[EQ_PULSEDOOR_POWER].Output  = 1;
    Settings.EquipIo[EQ_PULSEDOOR_POWER].Enabled = 1;

    /* Disabled equipment — superseded by the new defaults above.  Set
     * Output to IO_UNDEFINED so OutputOn/Off no-op until the operator
     * explicitly maps them in Level 2 IO Config. */
    Settings.EquipIo[EQ_REFRIGERATION].Output    = IO_UNDEFINED;
    Settings.EquipIo[EQ_REFRIGERATION].Enabled   = 0;
    Settings.EquipIo[EQ_DOORS].Output            = IO_UNDEFINED;
    Settings.EquipIo[EQ_DOORS].Enabled           = 0;

    /* Default proving inputs — paired with outputs on Orbit DIs.
     * Lights and pulse-door open/close have no proving input.
     * EQ_FAN keeps DI3 (airflow/run-proving sensor). */
    Settings.EquipIo[EQ_FAN].Input            = 3;   /* DI3 */
    Settings.EquipIo[EQ_CLIMACELL].Input      = 4;   /* DI4 */
    Settings.EquipIo[EQ_HUMID_HEAD1].Input    = 5;   /* DI5 */
    Settings.EquipIo[EQ_HUMID_PUMP1].Input    = 6;   /* DI6 */
    Settings.EquipIo[EQ_REFRIG_STAGE1].Input  = 7;   /* DI7 */
    Settings.EquipIo[EQ_HEAT].Input           = 8;   /* DI8 */

    /* Disabled equipment — clear stale proving-input defaults so a wrong
     * sensor on those DIs cannot trip a proving alarm. */
    Settings.EquipIo[EQ_REFRIGERATION].Input  = IO_UNDEFINED;
    Settings.EquipIo[EQ_DOORS].Input          = IO_UNDEFINED;

    /* Nova has no power proving relay or remote/refrig standby contacts.
     * IO_UNDEFINED causes CheckInputs() to return 1 (bypass) for these
     * three special-cased equipment indexes. */
    Settings.EquipIo[EQ_POWER].Input           = IO_UNDEFINED;
    Settings.EquipIo[EQ_REMOTE_STANDBY].Input  = IO_UNDEFINED;
    Settings.EquipIo[EQ_REFRIG_STANDBY].Input  = IO_UNDEFINED;

    /* Nova has no physical airflow or low-temp safety sensors.
     * IO_UNDEFINED → CheckInputs returns 0 → failure checks pass. */
    Settings.EquipIo[EQ_AIR_FLOW].Input        = IO_UNDEFINED;
    Settings.EquipIo[EQ_LOW_TEMP].Input        = IO_UNDEFINED;

    /* Factory defaults set all RunTimes to RC_STANDBY (3).
     * Override to RC_COOLING (1) so the system runs 24/7 in QEMU. */
    memset(Settings.RunTimes, RC_COOLING, sizeof(Settings.RunTimes));

    /* Temperature units: leave at the legacy AS2 default (Fahrenheit).
     * The user picks F or C from Level 2 Basic Settings; ReadAnalogBoards
     * converts orbit's tenths-°C wire format into the selected unit so
     * Sensor.Value stays in TempType-units (matching the legacy contract
     * that the rest of Controls.c / States.c relies on). */

    /* ── Door actuator timing ──────────────────────────────────── */
    /* Total time to open all actuators (seconds).  In production this
     * comes from calibration; for now use 240s so pulsed door timing
     * can be verified.  This is the same parameter as "Fresh Air Door
     * Actuator Time" in Level 2 settings on the legacy GDC. */
    Settings.Door.ActuatorTime = 240;     /* seconds (total travel) */

    /* ── Pre-set default switch states (system boots running) ──── */
    /* Set switch bits in InputState so CheckInputs(SW_xxx) returns 1.
     * The user can toggle individual switches via RemoteOff from the UI. */
    IoBoard[MAIN].InputState = 0x02  /* version 2 in bits [3:0] */
        | SS_MAIN_SW8               /* SW_START_STOP */
        | SS_MAIN_SW9               /* SW_REFRIG_AUTO */
        | SS_MAIN_SW11              /* SW_FAN_AUTO */
        | SS_MAIN_SW13              /* SW_CLIMACELL_AUTO */
        | SS_MAIN_SW15              /* SW_HUMID_AUTO */
        | SS_MAIN_SW16              /* SW_FRESHAIR_AUTO */
        ;

    /* ── Expansion boards (Orbit #2 and #3) ─────────────────────── */
    strcpy(IoBoard[EXPANSION_1].Name, "Ex 1");
    strcpy(IoBoard[EXPANSION_2].Name, "Ex 2");

    for (i = EXPANSION_1; i <= EXPANSION_2; i++) {
        /* Mark expansion boards as not installed by default.
         * orbit_discover() will update these if found. */
        IoBoard[i].Version = 0x0F;
    }

    debug_printf("[Nova] SerialShift_Init: Orbit v2 mode, switches preset\r\n");
    debug_printf("[Nova]   InputState = 0x%08X\r\n", IoBoard[MAIN].InputState);

    /* ── Make Red Light, Yellow Light, and Power configurable ──── */
    /* The AS2 firmware sets these to MODE_NONE (hidden from the
     * IO Config dropdown).  On Constellation they should be
     * assignable to any output/input port like other equipment. */
    Settings.EquipIo[EQ_REDLIGHT].Mode    = MODE_ALL;
    Settings.EquipIo[EQ_YELLOWLIGHT].Mode = MODE_ALL;

    /* Power is handled by DC 24V, not a relay DO — keep hidden. */

    /* Door Open/Close are assignable outputs on Constellation. */
    Settings.EquipIo[EQ_PULSEDOOR_OPEN].Mode  = MODE_ALL;
    Settings.EquipIo[EQ_PULSEDOOR_CLOSE].Mode = MODE_ALL;

    /* Remove legacy Pulse Door - Power from the list (Orbit has no
     * CPLD power-down circuit). */
    Settings.EquipIo[EQ_PULSEDOOR_POWER].Mode = MODE_NONE;
}

/* From States.c */
extern void SetSystemState(void);
extern void SetMode(void);

/* From hal_timer.c — PMU-based monotonic millisecond clock */
extern uint32_t hal_timer_get_ms(void);

/* From Timer.c (legacy, linked from Mini_IO) — light control variables */
extern LIGHT_STATUS LightStatus;

/* ─── Pulse Door Tick (replaces SerialShiftTimerISR pulse logic) ──── */
/*
 * The legacy AS2 firmware drove pulsed doors from a hardware timer ISR
 * (SerialShiftTimerISR in Timer.c) that fired every 250ms.  Every 4th
 * tick (1 second) it would:
 *   - If PulseDoorMove > 0: turn on the requested output for 1 second,
 *     decrement PulseDoorMove, update PulseDoorPosition.
 *   - If at PWM_MAX_VALUE: hold the open output steady.
 *   - If at PWM_MIN_VALUE: hold the close output steady.
 *   - Otherwise: turn both outputs off.
 *
 * In Nova, that ISR is never registered (no TM4C hardware timers).
 * This function replicates the same logic and is called from the
 * ThreadSerialShift polling loop every 250ms.  A static counter
 * (pulse_count) accumulates ticks to produce the 1-second cadence.
 *
 * Without this, PulseDoorMove is set by CtrlDoorsPulsed() but never
 * decremented, causing CtrlDoors() to block indefinitely.
 */
static int pulse_count = 0;
static int light_count = 0;

static void nova_pulse_door_tick(void)
{
    pulse_count++;

    if (pulse_count < 4)
        return;

    /* 4 × 250ms = 1 second — process one pulse step */
    pulse_count = 0;

    if (PulseDoorFlag != 0)
        return; /* CtrlDoorsPulsed is updating — skip this tick */

    if (   PwmChannel[PWM_DOORS].Output == PWM_MAX_VALUE
        && PulseDoorMove == 0
        && PulseDoorInit == 0)
    {
        /* Steady open when door is at max */
        OutputOn(EQ_PULSEDOOR_OPEN);
        OutputOff(EQ_PULSEDOOR_CLOSE);
    }
    else if (   PwmChannel[PWM_DOORS].Output == PWM_MIN_VALUE
             && PulseDoorMove == 0
             && PulseDoorInit == 0)
    {
        /* Steady close when door is at min */
        OutputOn(EQ_PULSEDOOR_CLOSE);
        OutputOff(EQ_PULSEDOOR_OPEN);
    }
    else
    {
        if (PulseDoorMove != 0 && PulseDoorFlag == 0)
        {
            PulseDoorFlag = 1;
            OutputOn(PulseDoor);
            PulseDoorMove--;
            if (PulseDoor == EQ_PULSEDOOR_OPEN)
            {
                PulseDoorPosition++;
                OutputOff(EQ_PULSEDOOR_CLOSE);
            }
            else
            {
                PulseDoorPosition--;
                OutputOff(EQ_PULSEDOOR_OPEN);
            }
            PulseDoorFlag = 0;
        }
        else
        {
            OutputOff(EQ_PULSEDOOR_OPEN);
            OutputOff(EQ_PULSEDOOR_CLOSE);
        }
    }
}

/* ─── Light Tick (replaces SerialShiftTimerISR light logic) ───────── */
static void nova_light_tick(void)
{
    light_count++;
    if (light_count < 4)
        return;

    light_count = 0;

    /* Red light */
    if (LightStatus.Red == LT_ON) {
        OutputOn(EQ_REDLIGHT);
        LightStatus.RedOn = 1;
    } else if (LightStatus.Red == LT_OFF) {
        OutputOff(EQ_REDLIGHT);
        LightStatus.RedOn = 0;
    } else if (LightStatus.Red == LT_BLINK) {
        if (LightStatus.RedOn == 0) {
            OutputOn(EQ_REDLIGHT);
            LightStatus.RedOn = 1;
            if (LightStatus.Yellow == LT_BLINK) {
                OutputOff(EQ_YELLOWLIGHT);
                LightStatus.YellowOn = 0;
            }
        } else {
            OutputOff(EQ_REDLIGHT);
            LightStatus.RedOn = 0;
            if (LightStatus.Yellow == LT_BLINK) {
                OutputOn(EQ_YELLOWLIGHT);
                LightStatus.YellowOn = 1;
            }
        }
    }

    /* Yellow light */
    if (LightStatus.Yellow == LT_ON) {
        OutputOn(EQ_YELLOWLIGHT);
        LightStatus.YellowOn = 1;
    } else if (LightStatus.Yellow == LT_OFF) {
        OutputOff(EQ_YELLOWLIGHT);
        LightStatus.YellowOn = 0;
    } else if (LightStatus.Yellow == LT_BLINK && LightStatus.Red != LT_BLINK) {
        if (LightStatus.YellowOn == 0) {
            OutputOn(EQ_YELLOWLIGHT);
            LightStatus.YellowOn = 1;
        } else {
            OutputOff(EQ_YELLOWLIGHT);
            LightStatus.YellowOn = 0;
        }
    }
}

/*
 * In QEMU, vTaskDelay(1) returns immediately via the pended-tick
 * mechanism but does process one context switch.  For longer delays
 * we count iterations of vTaskDelay(1) — each corresponds to roughly
 * one pass through the scheduler.  With the 1kHz QEMU tick timer,
 * N iterations ≈ N ms of emulated time.
 *
 * On real hardware (no QEMU_BUILD), use the standard vTaskDelay.
 */
static void nova_delay_ms(uint32_t ms)
{
#ifdef QEMU_BUILD
    for (uint32_t i = 0; i < ms; i++) {
        vTaskDelay(1);
    }
#else
    vTaskDelay(ms / portTICK_RATE_MS);
#endif
}

/* ─── ThreadSerialShift override ──────────────────────────────────── */
/*
 * Replaces the CPLD bit-banging loop with Modbus TCP I/O polling.
 * Same task name ("SERIALSHIFT"), same priority, same calling convention.
 * Controls.c doesn't know the difference — it sees IoBoard[].InputState
 * and OutputState updated exactly as before.
 */

void ThreadSerialShift(void)
{
    debug_printf("[Nova] ThreadSerialShift: Orbit Modbus TCP mode\r\n");

    /* Initialize the semaphore that ThreadSerialCom uses to sync */
    SerialShift.Semaphore = xSemaphoreCreateBinary();
    SerialShift.BlockTimeout = 500 / portTICK_RATE_MS;
    if (SerialShift.Semaphore != NULL) {
        xSemaphoreGive(SerialShift.Semaphore);
    }

#ifdef QEMU_STUB_ORBIT
    /*
     * QEMU stub mode: skip Modbus TCP discovery/polling.
     * Enable by adding -DQEMU_STUB_ORBIT to DEFS in Makefile.
     * When disabled, real Modbus TCP polling runs via UART2 tunnel.
     */
    debug_printf("[Nova] QEMU: stubbing 3 Orbit boards\r\n");

    for (int i = 0; i < BOARD_COUNT && i < 3; i++) {
        IoBoard[i].Version = 0x02;
        IoBoard[i].InputState = 0;
        IoBoard[i].OutputState = 0;
        IoBoard[i].SwitchesActive = 1;
        IoBoard[i].Fault = 0;

        if (i == MAIN) {
            IoBoard[i].InputShiftLength = SS_MAIN_IO_INPUTS_V2;
            IoBoard[i].OutputShiftLength = SS_MAIN_IO_OUTPUTS;
            IoBoard[i].NumInputs = SS_MAIN_DI_INPUTS;
            IoBoard[i].NumOutputs = SS_MAIN_IO_OUTPUTS;
            for (int j = 0; j <= SS_MAIN_DI_INPUTS; j++)
                IoBoard[i].Input[j] = (j == 0) ? 0 : (1UL << (32 - j));
            for (int j = 0; j <= SS_MAIN_IO_OUTPUTS; j++)
                IoBoard[i].Output[j] = (j == 0) ? 0 : (1UL << (SS_MAIN_IO_OUTPUTS - j));
        } else {
            IoBoard[i].InputShiftLength = SS_EX_IO_INPUTS;
            IoBoard[i].OutputShiftLength = SS_EX_IO_OUTPUTS;
            IoBoard[i].NumInputs = SS_EX_DI_INPUTS;
            IoBoard[i].NumOutputs = SS_EX_IO_OUTPUTS;
            for (int j = 0; j <= SS_EX_DI_INPUTS; j++)
                IoBoard[i].Input[j] = (j == 0) ? 0 : (1UL << (14 - j));
            for (int j = 0; j <= SS_EX_IO_OUTPUTS; j++)
                IoBoard[i].Output[j] = (j == 0) ? 0 : (1UL << (SS_EX_IO_OUTPUTS - j));
        }
    }

    debug_printf("[Nova] QEMU: boards ready, populating initial sensor data\r\n");

    /* Pre-populate sensor data so the first UI_SendMain has values.
     * ReadAnalogBoards will refresh this every ThreadSerialCom cycle. */
    Settings.AnalogBoard[0].Present = 1;
    Settings.AnalogBoard[0].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Disabled = 0;
    Settings.AnalogBoard[0].Sensor[0].Value = 21.0f;
    Settings.AnalogBoard[0].Sensor[0].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[1].Value = 21.5f;
    Settings.AnalogBoard[0].Sensor[1].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[2].Value = 30.0f;
    Settings.AnalogBoard[0].Sensor[2].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[3].Value = 19.0f;
    Settings.AnalogBoard[0].Sensor[3].Type = ANALOG_SENSOR_TYPE_TEMP;

    Settings.AnalogBoard[1].Present = 1;
    Settings.AnalogBoard[1].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Disabled = 0;
    Settings.AnalogBoard[1].Sensor[0].Value = 45.0f;
    Settings.AnalogBoard[1].Sensor[0].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[1].Value = 55.0f;
    Settings.AnalogBoard[1].Sensor[1].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[2].Value = 40.0f;
    Settings.AnalogBoard[1].Sensor[2].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[3].Value = 800.0f;
    Settings.AnalogBoard[1].Sensor[3].Type = ANALOG_SENSOR_TYPE_CO2;

    Settings.AnalogBoard[2].Present = 1;
    Settings.AnalogBoard[2].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Disabled = 0;
    Settings.AnalogBoard[2].Sensor[0].Value = 21.0f;
    Settings.AnalogBoard[2].Sensor[0].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[1].Value = 21.5f;
    Settings.AnalogBoard[2].Sensor[1].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[2].Value = 15.0f;
    Settings.AnalogBoard[2].Sensor[2].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[3].Value = 19.0f;
    Settings.AnalogBoard[2].Sensor[3].Type = ANALOG_SENSOR_TYPE_TEMP;

    debug_printf("[Nova] QEMU: sensor data ready\r\n");

    /* Set the global PlenumTempAvg directly so FormatPlenumTemp works
     * immediately, without waiting for CalculatePlenumTemp() from
     * ThreadSerialCom → SetSystemState */
    {
        extern float PlenumTempAvg;
        PlenumTempAvg = (21.0f + 21.5f) / 2.0f;  /* 21.25 */
    }

    /* Idle loop — just yield to let other threads run */
    {
        int loop_count = 0;
        while (1) {
            vTaskDelay(250 / portTICK_RATE_MS);

            /* Replicate the legacy SerialShiftTimerISR functionality.
             * Each loop iteration = 250ms, matching the ISR tick rate.
             * nova_pulse_door_tick counts to 4 (1 second) internally. */
            nova_pulse_door_tick();
            nova_light_tick();

            loop_count++;
            if (loop_count <= 5) {
                debug_printf("[SerialShift] loop %d, tick=%lu\r\n",
                             loop_count, (unsigned long)hal_timer_get_ms());
            }
        }
    }

#else
    /* Real hardware: full Orbit Modbus TCP discovery + polling */
    debug_printf("[Nova] calling orbit_init...\r\n");
    orbit_init();

    debug_printf("[Nova] tick_ms=%lu, starting discover\r\n",
                 (unsigned long)hal_timer_get_ms());

    int discovered = orbit_discover();
    debug_printf("[Nova] Discovered %d Orbit board(s) of %d scanned\r\n",
                 discovered, NOVA_MAX_ORBITS);

    /* Main polling loop — 250ms cadence.
     *
     * Legacy slots 0-2: poll via orbit_poll_io() which maps DI/DO
     * into IoBoard[] for Controls.c.
     *
     * Extended slots 3+: poll via orbit_poll_extended() which reads
     * DI/E-Stop directly into orbit_boards[] (no IoBoard[] mapping).
     * These are refrigeration, door, or second-zone boards.
     *
     * Note: in single-tunnel QEMU mode we only poll slot 0 to avoid
     * UART chardev contention.  On real hardware (per-board TCP) all
     * discovered boards are polled in parallel.
     */
    while (1) {
        /* Legacy slots — Controls.c depends on IoBoard[] */
        for (int i = 0; i < BOARD_COUNT; i++) {
            if (i > 0) continue;  /* single-tunnel: MAIN only (QEMU) */
            if (IoBoard[i].Version != 0x0F) {
                int rc = orbit_poll_io((SYSTEM_BOARDS)i);
                if (rc != MBTCP_OK) {
                    if (orbit_boards[i].comm_errors > 10) {
                        debug_printf("[Nova] Board %d lost — reconnecting\r\n", i);
                        mbtcp_disconnect(&orbit_boards[i].conn);
                        orbit_boards[i].comm_errors = 0;
                    }
                }
            }
        }

        /* Extended slots — refrigeration, door, second-zone Orbits */
        for (int i = BOARD_COUNT; i < NOVA_MAX_ORBITS; i++) {
            if (!orbit_boards[i].connected) continue;
            int rc = orbit_poll_extended(i);
            if (rc != MBTCP_OK) {
                if (orbit_boards[i].comm_errors > 10) {
                    debug_printf("[Nova] Extended slot %d lost — reconnecting\r\n", i);
                    mbtcp_disconnect(&orbit_boards[i].conn);
                    orbit_boards[i].comm_errors = 0;
                }
            }
        }

        /* Replicate the legacy SerialShiftTimerISR functionality.
         * Each loop iteration = 250ms, matching the ISR tick rate.
         * nova_pulse_door_tick counts to 4 (1 second) internally. */
        nova_pulse_door_tick();
        nova_light_tick();

        nova_delay_ms(250);
    }
#endif
}

/* ─── ReadAnalogBoards override ───────────────────────────────────── */
/*
 * Replaces the RS485 packet-based analog board polling with Modbus TCP
 * sensor register reads from Orbit boards.
 *
 * The Orbit boards read their RS-485 sensors locally and expose the
 * data as Modbus holding registers 200-263. We read those and map
 * them into the same Sensor[][] array that Controls.c uses.
 *
 * Register layout per Orbit board (holding regs 200+):
 *   200 = sensor types bitmask
 *   201 = sensor count
 *   202-205 = sensor 1-4 raw values (same format as RS485 packets)
 *   206-209 = sensor 5-8 raw values
 *   ...etc
 *
 * For now in QEMU, we populate stub sensor data since there's no
 * real RS-485 bus. This still exercises the full data path.
 */

void ReadAnalogBoards(char ReadType)
{
    (void)ReadType;
    static int read_count = 0;

#ifdef QEMU_STUB_ORBIT
    /*
     * QEMU stub: populate sensor data matching the orbit simulator's
     * analog board configuration.  Three boards:
     *   Board 0 (addr 0): Type 3 (Temperature) — Plenum1, Plenum2, Outside, Return
     *   Board 1 (addr 1): Type 1 (Humidity)    — Outside RH, Plenum RH, Return RH, CO2
     *   Board 2 (addr 2): Type 3 (Temperature) — 4 pile sensors
     *
     * Enable by adding -DQEMU_STUB_ORBIT to DEFS in Makefile.
     */

    /* Board 0: Temperature — Plenum/Outside/Return */
    Settings.AnalogBoard[0].Present = 1;
    Settings.AnalogBoard[0].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[0].Value = 21.0f;   /* Plenum 1 */
    Settings.AnalogBoard[0].Sensor[0].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[1].Value = 21.5f;   /* Plenum 2 */
    Settings.AnalogBoard[0].Sensor[1].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[2].Value = 30.0f;   /* Outside */
    Settings.AnalogBoard[0].Sensor[2].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[0].Sensor[3].Value = 19.0f;   /* Return */
    Settings.AnalogBoard[0].Sensor[3].Type = ANALOG_SENSOR_TYPE_TEMP;

    /* Board 1: Humidity */
    Settings.AnalogBoard[1].Present = 1;
    Settings.AnalogBoard[1].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[0].Value = 45.0f;   /* Outside RH */
    Settings.AnalogBoard[1].Sensor[0].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[1].Value = 55.0f;   /* Plenum RH */
    Settings.AnalogBoard[1].Sensor[1].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[2].Value = 40.0f;   /* Return RH */
    Settings.AnalogBoard[1].Sensor[2].Type = ANALOG_SENSOR_TYPE_HUMID;
    Settings.AnalogBoard[1].Sensor[3].Value = 800.0f;  /* CO2 ppm */
    Settings.AnalogBoard[1].Sensor[3].Type = ANALOG_SENSOR_TYPE_CO2;

    /* Board 2: Temperature — pile sensors */
    Settings.AnalogBoard[2].Present = 1;
    Settings.AnalogBoard[2].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[0].Value = 21.0f;
    Settings.AnalogBoard[2].Sensor[0].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[1].Value = 21.5f;
    Settings.AnalogBoard[2].Sensor[1].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[2].Value = 15.0f;
    Settings.AnalogBoard[2].Sensor[2].Type = ANALOG_SENSOR_TYPE_TEMP;
    Settings.AnalogBoard[2].Sensor[3].Value = 19.0f;
    Settings.AnalogBoard[2].Sensor[3].Type = ANALOG_SENSOR_TYPE_TEMP;

    /* Log every 20th read */
    if ((++read_count % 20) == 1) {
        int t0 = (int)(Settings.AnalogBoard[0].Sensor[0].Value * 10);
        int t1 = (int)(Settings.AnalogBoard[0].Sensor[1].Value * 10);
        int t2 = (int)(Settings.AnalogBoard[0].Sensor[2].Value * 10);
        int t3 = (int)(Settings.AnalogBoard[0].Sensor[3].Value * 10);
        debug_printf("[Sensor] stub T0=%d.%d T1=%d.%d T2=%d.%d T3=%d.%d\r\n",
                     t0/10, t0%10, t1/10, t1%10,
                     t2/10, t2%10, t3/10, t3%10);
    }

#else
    /* Real hardware: read from Orbit boards via Modbus TCP.
     * Legacy slots (0..BOARD_COUNT-1) use IoBoard[] bounds check.
     * Extended slots poll via orbit_read_sensors_slot() — sensor
     * data from refrigeration/door boards goes through a separate
     * path (not Settings.AnalogBoard[]). */
    for (int b = 0; b < BOARD_COUNT; b++) {
        if (IoBoard[b].Version == 0x0F) continue;
        if (!orbit_boards[b].connected) continue;

        int rc = orbit_read_sensors_slot(b);
        if (rc < 0) continue;
        if (!orbit_boards[b].sensor_regs_valid) continue;

        int board_count = orbit_boards[b].sensor_regs[0];
        if (board_count > 8) board_count = 8;

        for (int ab = 0; ab < board_count; ab++) {
            int base = 1 + ab * 7;
            uint16_t info   = orbit_boards[b].sensor_regs[base + 0];
            uint16_t stypes = orbit_boards[b].sensor_regs[base + 1];

            int board_addr = info & 0x3F;
            int present    = (info & 0x80) ? 1 : 0;
            int board_type = (info >> 8) & 0xFF;

            if (!present) continue;
            if (board_addr >= ANALOG_BOARDS_PER_SYSTEM) continue;

            Settings.AnalogBoard[board_addr].Present = 1;
            Settings.AnalogBoard[board_addr].Type = board_type;

            for (int s = 0; s < ANALOG_SENSORS_PER_BOARD; s++) {
                int stype = (stypes >> (12 - s * 4)) & 0xF;
                if (stype == 0xF) continue;

                int16_t raw = (int16_t)orbit_boards[b].sensor_regs[base + 2 + s];
                float value;
                if (raw == (int16_t)SENSOR_VAL_UNDEFINED) {
                    value = SENSOR_VAL_UNDEFINED;
                } else if (stype == ANALOG_SENSOR_TYPE_CO2) {
                    value = (float)raw;
                } else {
                    /* Orbit wire format: tenths of the sensor's native unit.
                     * Temperature: tenths of °C (orbit always C on the wire,
                     *   matching the sensor board's NTC lookup output).
                     *   Convert to °F if the user picked Fahrenheit so the
                     *   stored value matches the legacy AS2 contract that
                     *   Sensor.Value is always in TempType-units. */
                    value = (float)raw / 10.0f;
                    if (stype == ANALOG_SENSOR_TYPE_TEMP
                        && Settings.TempType == 0) {       /* °C → °F */
                        value = value * 1.8f + 32.0f;
                    }
                }
                Settings.AnalogBoard[board_addr].Sensor[s].Value = value;
                Settings.AnalogBoard[board_addr].Sensor[s].Type = stype;
            }
        }

        if (b == 0 && (++read_count % 20) == 1) {
            int t0 = (int)(Settings.AnalogBoard[0].Sensor[0].Value * 10);
            int t1 = (int)(Settings.AnalogBoard[0].Sensor[1].Value * 10);
            int t2 = (int)(Settings.AnalogBoard[0].Sensor[2].Value * 10);
            int t3 = (int)(Settings.AnalogBoard[0].Sensor[3].Value * 10);
            debug_printf("[Sensor] %d boards, T0=%d.%d T1=%d.%d T2=%d.%d T3=%d.%d\r\n",
                         board_count,
                         t0/10, t0%10, t1/10, t1%10,
                         t2/10, t2%10, t3/10, t3%10);
        }
    }
#endif

    /* CalculatePlenumTemp() is static in Analog_Input.c — replicate here
     * so PlenumTempAvg is valid for CheckSystemStatus(). */
    {
        int validSensors = 0;
        float avg = 0;
        if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Value
                != SENSOR_VAL_UNDEFINED) {
            avg = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Value;
            validSensors++;
        }
        if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value
                != SENSOR_VAL_UNDEFINED) {
            if (validSensors > 0)
                avg += Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value;
            else
                avg = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value;
            validSensors++;
        }
        if (validSensors > 0)
            PlenumTempAvg = avg / validSensors;
        else
            PlenumTempAvg = SENSOR_VAL_UNDEFINED;
    }
}

/* ─── FindAnalogBoards override ───────────────────────────────────── */

void FindAnalogBoards(void)
{
    debug_printf("[Nova] FindAnalogBoards: using Orbit discovery data\r\n");
}

/* ─── Cooling-available temperature ───────────────────────────────── */
/*
 * Climacell wet-bulb depression — returns °F (or °C, matching
 * Settings.TempType) of evaporative cooling potential available from
 * the climacell given current outside temp + humidity + altitude.
 *
 * Equation provided by Gellert (legacy Mini_IO/States.c::WetBulbDepression).
 * Re-implemented here because the legacy function is file-static and
 * therefore not linkable from Nova platform code.
 */
static float nova_wet_bulb_depression(void)
{
    extern float *OutsideTemp;
    const int B = DEFAULT_HUMID_BOARD;
    const int S = SENSOR_OUTSIDE_HUMID;

    if (   OutsideTemp == NULL
        || *OutsideTemp == SENSOR_VAL_UNDEFINED
        || Settings.AnalogBoard[B].Sensor[S].Disabled == 1
        || Settings.AnalogBoard[B].Sensor[S].Value < 1
        || Settings.AnalogBoard[B].Sensor[S].Value == SENSOR_VAL_UNDEFINED)
    {
        return 0.0f;
    }

    float TempC = *OutsideTemp;
    if (Settings.TempType == 0) {                       /* °F → °C */
        TempC = (TempC - 32.0f) / 1.8f;
    }

    float AltM = Settings.Climacell.Altitude;
    if (Settings.Climacell.AltitudeUnits == 0) {        /* feet → m */
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

/*
 * Computes the global StartTemp ("Cooling Available Temperature" in the
 * UI) and the threshold SetStateCooling() compares against to decide
 * when to open the fresh-air doors.
 *
 * Constellation formula (Apr 2026):
 *
 *   StartTemp = RefTemp + Settings.OutsideAir.Diff
 *             + (WetBulbDepression × Climacell.Efficiency / 100)
 *                                ← only when climacell switch is in
 *                                  auto/manual, schedule active,
 *                                  not remote-off, no climacell alarm.
 *
 *   RefTemp is the cooling target reference, selected by
 *   Settings.OutsideAir.TempRef:
 *     255 → Settings.Plenum.TempSet               (default)
 *     254 → Return-air sensor
 *     0..N → specific sensor by global ID
 *
 * Interpretation:  StartTemp is the **maximum outside dry-bulb temperature**
 * at which the system can still cool the storage to the plenum setpoint
 * by drawing outside air through the climacell evaporative cooler.
 *
 *   Outside air enters the climacell at OutsideTemp and exits at
 *   approximately (OutsideTemp − Eff × Dwb).  For that exit air to
 *   meet the cooling target (RefTemp + Diff) we need
 *
 *       OutsideTemp − Eff × Dwb ≤ RefTemp + Diff
 *       OutsideTemp ≤ RefTemp + Diff + Eff × Dwb
 *                    └─────── StartTemp ───────┘
 *
 *   SetStateCooling() then compares the actual *OutsideTemp against
 *   StartTemp to decide whether to enter cooling mode.
 *
 * Notes:
 *  • The POTATO-mode restriction on the WB term that was in legacy
 *    SetStartTemp() is intentionally dropped — onion / cure / generic
 *    storage all benefit from climacell evap-cooling assist.
 *  • Settings.OutsideAir.AboveBelow == 1 inverts Diff to "below".
 *  • Runs after SetSystemState() inside ThreadSerialCom so it
 *    overwrites the legacy SetStartTemp() value.
 */
static void nova_calc_cooling_available(void)
{
    extern float StartTemp;
    extern float *OutsideTemp;

    /* No outside-air sensor data ⇒ can't evaluate. */
    if (OutsideTemp == NULL || *OutsideTemp == SENSOR_VAL_UNDEFINED) {
        StartTemp = SENSOR_VAL_UNDEFINED;
        return;
    }

    /* ── Resolve the cooling target reference ── */
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
        && SystemAlarm[AL_CLIMACELL] == 0)
    {
        float wb = nova_wet_bulb_depression();
        startTemp += wb * (Settings.Climacell.Efficiency / 100.0f);
    }

    StartTemp = startTemp;
}

/* ─── ThreadSerialCom override ────────────────────────────────────── */
/*
 * Simplified for QEMU: the original takes SaveSettingsRequest and
 * SerialShift semaphores, calls ReadAnalogBoards/SetSystemState/SetMode.
 * For Phase 1, just delay-loop to let the idle task drive cooperative
 * ticks.  Phase 2 will add the control logic back.
 */
void ThreadSerialCom(void)
{
    debug_printf("[Nova] ThreadSerialCom: QEMU mode\r\n");
    static uint32_t ctrl_cnt = 0;
    extern char SystemState;
    while (1) {
        ReadAnalogBoards(0);

        if (SerialShift.Semaphore != NULL) {
            if (xSemaphoreTake(SerialShift.Semaphore,
                               SerialShift.BlockTimeout) == pdTRUE) {
                SetSystemState();
                SetMode();
                nova_calc_cooling_available();

                if ((ctrl_cnt++ % 30) == 0) {
                    extern float StartTemp;
                    extern float *OutsideTemp;
                    extern int RunClockMode(void);
                    int rc = RunClockMode();
                    int sw_refrig = CheckInputs(SW_REFRIG_AUTO);
                    int sw_fan    = CheckInputs(SW_FAN_AUTO);
                    int sw_fresh  = CheckInputs(SW_FRESHAIR_AUTO);
                    int prv_stby  = CheckInputs(EQ_REFRIG_STANDBY);
                    int al_refr   = SystemAlarm[AL_REFRIGERATION];
                    int ro_refr   = Settings.RemoteOff[RO_REFRIGERATION];
                    int door_pct  = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
                    float ot      = OutsideTemp ? *OutsideTemp : -999.0f;
                    /* Door PID diagnostics: gates inside CtrlDoors() */
                    extern unsigned int XTimerVal;
                    extern int PulseDoorMove;
                    int pwm_en   = Settings.PWM[PWM_DOORS].Enabled;
                    int pp_out   = Settings.EquipIo[EQ_PULSEDOOR_POWER].Output;
                    int pid_tmr  = (int)((long)PIDCtrl[PID_DOOR].Timer - (long)XTimerVal);
                    int pid_u    = Settings.Door.PID.U;
                    int pdm      = PulseDoorMove;
                    int purge    = (Settings.Co2.Purge.Start != 0);
                    int boost    = (IntervalTimer[IT_FANBOOSTCYCLE] != 0);
                    int pwm_out  = (int)PwmChannel[PWM_DOORS].Output;
                    debug_printf("[Ctrl] t=%lu SS=%d Mode=%d RC=%d "
                                 "sw[refr=%d fan=%d fresh=%d] "
                                 "prv=%d al_refr=%d ro_refr=%d "
                                 "Out=%d.%d Start=%d.%d Door=%d%% "
                                 "PID[en=%d pp=%d tmr=%d U=%d] PDM=%d purge=%d boost=%d pwm=%d "
                                 "Out_bits=0x%08X\r\n",
                                 (unsigned long)ctrl_cnt,
                                 (int)SystemState, (int)CurrentMode, rc,
                                 sw_refrig, sw_fan, sw_fresh,
                                 prv_stby, al_refr, ro_refr,
                                 (int)ot, (int)((ot - (int)ot) * 10),
                                 (int)StartTemp, (int)((StartTemp - (int)StartTemp) * 10),
                                 door_pct,
                                 pwm_en, pp_out, pid_tmr, pid_u, pdm, purge, boost, pwm_out,
                                 (unsigned)IoBoard[MAIN].OutputState);
                }

                xSemaphoreGive(SerialShift.Semaphore);
            }
        }

        nova_delay_ms(1000);
    }
}

/* ─── ThreadUIUpdate override ─────────────────────────────────────── */
/*
 * Production ThreadUIUpdate for Nova / Constellation.
 *
 * Same timer-driven control loop as the AS2 original, adapted:
 *   - No SDCard (Nova uses different storage)
 *   - No FileReceive gate (no serial file transfer protocol)
 *   - MessagingStatus set to MS_RESPONDING immediately (bridge is always up)
 *   - Equipment-dependent sends (EquipStatus, AuxSwitches, IoConfig) are
 *     skipped until I/O configuration is validated
 *
 * On real AM2434 hardware, re-enable the full send set and add
 * SDCard_Init() for settings persistence.
 */
void ThreadUIUpdate(void)
{
    unsigned int UpTime = 0;

    /* Initialize SaveSettingsRequest semaphore */
    SaveSettingsRequest.BlockTimeout = 5 / portTICK_RATE_MS;
    vSemaphoreCreateBinary(SaveSettingsRequest.Semaphore);

    /* UI messaging initialization */
    UI_Message.State = MSG_STATE_WAITING;
    MessagingStatus = MS_RESPONDING;
    LtxInitialized = 1;

#ifdef QEMU_BUILD
    /* Note: initial UI data sent before bridge connects will be dropped
     * (QEMU TCP chardev discards bytes with no client).  The bridge will
     * send a Version init handshake which triggers a full resend. */
#endif

    /* Initialize the timers/counters */
    MainTimer_Init();

    /* Clear any active CO2 purge cycles */
    Settings.Co2.Purge.Start = 0;
    Settings.Co2.Purge.Last = XTimerVal;

    debug_printf("[Nova] ThreadUIUpdate: active\r\n");

    while (1) {
        UpTime = XTimer();

        /* ── 1 second: system state + analog board discovery ───────── */
        if (UpTime - MainTimer[MT_SYSTEMSTATE] >= 1 * T_SECS) {
            UpdateSystemTime();
            MainTimer[MT_SYSUPTIME] = UpTime;
            MainTimer[MT_SYSTEMSTATE] = UpTime;

            if (DiscoverAnalogBoards() == AB_REPORT) {
                UI_SendSensorLabels();
                UI_SendSensorData();
                DiscoverAnalogBoardsRequest(AB_CLEAR);
            }

            ThreadMonitorUpdate(TM_UI_UPDATE);
        }

        /* ── 3 seconds: status page (safe subset) ─────────────────── */
        if (UpTime - MainTimer[MT_READSENSORS] >= 3 * T_SECS) {
            if (DiscoverAnalogBoards() == AB_CLEAR) {
                UI_SendMain(MSG_QUEUE);
                UI_SendMode();
            }

            UI_SendDateTime(MSG_QUEUE);
            UI_SendHumidModes();
            UI_SendEquipStatus(MSG_QUEUE);
            UI_SendAuxSwitches(MSG_QUEUE);
            UI_SendPlenSetPoints(MSG_QUEUE);
            MainTimer[MT_READSENSORS] = UpTime;
        }

        /* ── 7 seconds: sensor data + warnings ─────────────────────── */
        if (UpTime - MainTimer[MT_SENDSENSORS] >= 7 * T_SECS) {
            UI_SendSensorData();
            UI_SendWarnings(0);
            MainTimer[MT_SENDSENSORS] = UpTime;
        }

        /* ── 50 seconds (or first boot): settings + labels ─────────── */
        /* Nova change: do NOT trigger a full send burst on LtxInitialized.
         * The bridge sends a DataRequest when it connects, which routes
         * through nova_dataexc and calls NovaMsg_SendAllSettings() at
         * that point — that is the correct demand-driven path.  Blasting
         * 40 encoder calls on the very first thread tick starves other
         * tasks and (with previous buggy stack allocations) also
         * overflowed.  Leave the 50-minute periodic refresh intact. */
        if (UpTime - MainTimer[MT_UI_ALL] >= 50 * T_SECS) {
            UI_SendAllSettings();
            UI_SendSensorLabels();

            DailyJobs();
            DaylightSavingCheck(0, 1);
            MainTimer[MT_UI_ALL] = UpTime;
        }

        /* Clear the init flag once — nova_dataexc's DataRequest handler
         * is the authoritative initial-send trigger. */
        if (LtxInitialized == 1) {
            LtxInitialized = 0;
            debug_printf("[Nova] LtxInitialized cleared — awaiting bridge DataRequest\r\n");
        }

        /* ── Settings save (periodic + on-demand) ──────────────────── */
        if ((SaveSettingsRequest.Status == SR_REQUEST
                && UpTime - MainTimer[MT_SYSTEMSETTINGS] >= 2 * T_SECS)
            || UpTime - MainTimer[MT_SYSTEMSETTINGS] >= 60 * T_MINS) {
            if (SaveSettingsRequest.Semaphore != NULL) {
                if (xSemaphoreTake(SaveSettingsRequest.Semaphore,
                                   SaveSettingsRequest.BlockTimeout) == pdTRUE) {
                    /* Nova uses ping-pong OSPI banking instead of the
                     * TM4C SDCard/KFS path.  Type 0 = operator save,
                     * Type 1 = snapshot as panel defaults. */
                    NssResult r;
                    if (SaveSettingsRequest.Type == 1) {
                        r = NovaSettings_SavePanelDefaults(
                                &Settings, sizeof(Settings));
                    } else {
                        r = NovaSettings_Save(&Settings, sizeof(Settings));
                    }
                    if (r != NSS_OK) {
                        debug_printf("[Settings] Save failed rc=%d type=%d\r\n",
                                     r, SaveSettingsRequest.Type);
                        WarningsSet(WARN_SAVESETTINGS, FM_ALARM, FM_ALARM, NA);
                    } else {
                        /* Successful Nova OSPI save — clear the legacy
                         * boot warnings that Settings_Init / ReadSettings
                         * raise when both banks come up empty.  The
                         * legacy "settings successfully read" branch in
                         * Settings.c never runs in the Nova path, so
                         * without this the UI shows WARN_SAVESETTINGS /
                         * WARN_FACTORYDEFAULT forever even after the
                         * operator presses Save Settings. */
                        WarningsSet(WARN_SAVESETTINGS,    FM_NONE, FM_NONE, NA);
                        WarningsSet(WARN_FACTORYDEFAULT,  FM_NONE, FM_NONE, NA);
                        WarningsSet(WARN_EEPROMACCESS,    FM_NONE, FM_NONE, NA);
                    }
                    SaveSettingsRequest.Status = 0;   /* clear SR_REQUEST */
                    MainTimer[MT_SYSTEMSETTINGS] = UpTime;
                    xSemaphoreGive(SaveSettingsRequest.Semaphore);
                }
            }
        }

        /* ── Drain message queue → UART1 (bridge) ─────────────────── */
        ProcessMsgQueue();

        /* ── Poll UART1 RX (no interrupts in QEMU — ISR must be called
         *    explicitly to transfer bytes from the 16550 FIFO into
         *    UsartRx.Buffer). ── */
        Usart_ISR();

        /* ── Feed bytes to Nova COBS+protobuf framer ─────────────────
         * Drains the legacy ring buffer one byte at a time into
         * NovaProto_FeedByte(), which accumulates until a 0x00 frame
         * delimiter, validates CRC-16, and dispatches via the rx
         * callback registered by NovaDataExc_Init().  The legacy text
         * ProcessUIMessage path is bypassed (it's a no-op stub on Nova). */
        {
            extern int Usart_GetLastChar(char *ret);  /* not in Usart.h */
            static int s_first_rx_logged = 0;
            static unsigned int s_rx_bytes = 0;
            char b;
            while (Usart_CharsBuffered() && Usart_GetLastChar(&b) == 0) {
                if (!s_first_rx_logged) {
                    debug_printf("[Nova] First RX byte: 0x%02x\r\n", (uint8_t)b);
                    s_first_rx_logged = 1;
                }
                s_rx_bytes++;
                NovaProto_FeedByte((uint8_t)b);
            }
            /* Periodically log RX byte count so we can confirm bytes are
             * flowing even if no full frame has been assembled yet. */
            static unsigned int s_rx_log_ctr = 0;
            if (++s_rx_log_ctr >= 200) {  /* ~10s at 50ms tick */
                s_rx_log_ctr = 0;
                if (s_rx_bytes > 0) {
                    debug_printf("[Nova] RX bytes=%u\r\n", s_rx_bytes);
                }
            }
        }

        /* ── Periodic Nova housekeeping (heartbeat, log ring drain) ── */
        NovaDataExc_Tick();

        /* Yield — minimum 1 tick to let other tasks run. */
        vTaskDelay(1);
    }
}

/* ─── ThreadMonitor override ──────────────────────────────────────── */
/*
 * The original monitors watchdogs and thread health flags.
 * In QEMU, no external hardware watchdog exists.  Just delay.
 */
void ThreadMonitor(void)
{
    debug_printf("[Nova] ThreadMonitor: QEMU mode\r\n");
    while (1) {
        vTaskDelay(2000 / portTICK_RATE_MS);
    }
}

/* ─── ThreadFileReceive override ──────────────────────────────────── */
/*
 * Override the AS2's busy-polling UART file receive with a simple
 * blocking delay loop.  In QEMU there's no file receive protocol,
 * and the busy-poll prevents the idle task from running (which drives
 * the cooperative tick).
 */
void ThreadFileReceive(void)
{
    debug_printf("[Nova] ThreadFileReceive: idle (QEMU)\r\n");
    while (1) {
        vTaskDelay(10000 / portTICK_RATE_MS);
    }
}

/* ─── SD Card overrides ───────────────────────────────────────────── */
/*
 * Constellation has no SD card on the controller board.  All data
 * logging (history, activity, PID, load) is handled by the RPi5
 * via the GellertFileSystem daemon which receives log data through
 * the bridge server.
 *
 * These stubs replace the AS2 SD card functions to:
 *   (a) prevent WARN_SDCARD_NONE / WARN_SDCARD alarms
 *   (b) keep the firmware's log-write call sites happy (they check
 *       ActiveSDCard.Initialized before writing)
 *   (c) return success from read/write so callers don't retry
 */
/* Override SDCard_Init — fake a working card */
void SDCard_Init(void)
{
    memset(&ActiveSDCard, 0, sizeof(ActiveSDCard));
    ActiveSDCard.Initialized = 1;
    ActiveSDCard.Present = 1;
    ActiveSDCard.Size = 1024 * 1024; /* fake 512 MB */
    ActiveSDCard.BlockTimeout = 100 / portTICK_RATE_MS;
    vSemaphoreCreateBinary(ActiveSDCard.Semaphore);

    /* Zero the header — log record counts stay at 0 so the UI
     * won't try to read back nonexistent records. */
    memset(&SDCardHeader, 0, sizeof(SDCardHeader));

    debug_printf("[Nova] SDCard_Init: stub (logging via RPi5)\r\n");
}

/* Override SDCardMonitor — nothing to monitor */
void SDCardMonitor(void)
{
    /* Card is always "present" */
}

/* Override SDCardSave — accept data, discard silently */
int SDCardSave(unsigned char *Record, int RecSize, int StartBlock)
{
    (void)Record; (void)RecSize; (void)StartBlock;
    return 1; /* success */
}

/* Override SDCardRead — return empty data */
int SDCardRead(char *Record, int RecSize, int StartBlock)
{
    (void)StartBlock;
    if (Record && RecSize > 0) memset(Record, 0, RecSize);
    return 1; /* success */
}

/* Override SDCardHeaderUpdate — nothing to persist */
int SDCardHeaderUpdate(void)
{
    return 1; /* success */
}

/* Override SDHeaderReset — nothing to reset */
void SDHeaderReset(SDCARD_HEADER *Header, int WriteHeader)
{
    (void)WriteHeader;
    if (Header) memset(Header, 0, sizeof(SDCARD_HEADER));
}
