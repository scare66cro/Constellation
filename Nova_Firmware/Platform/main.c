/*
 * main.c — Constellation Nova entry point (AM2434 Cortex-R5F)
 *
 * Ported from Mini_IO/Platform/main.c (TM4C129 Cortex-M4F).
 * Same thread structure, same initialization sequence, but using
 * the AM2434 HAL instead of TivaWare DriverLib.
 *
 * All Application/ code is shared with Mini_IO — the only changes
 * are in this Platform layer.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

/*** include files ***/

#include <stdint.h>
#include <stdbool.h>

/* Platform */
#include "hal.h"
#include "debug.h"
#include "system.h"
#include "pinout.h"
#include "nova_ipc.h"
#include "nova_settings_store.h"
#include "nova_dataexc.h"     /* NovaBridge_Init prototype */

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* Application — identical to Mini_IO */
#include "Controls.h"
#include "PIDLogs.h"
#include "PWM.h"
#include "RS485.h"
#include "RTC.h"
#include "SDCard.h"
#include "SerialShift.h"
#include "Settings.h"
#include "StorePostData.h"
#include "SystemLogs.h"
#include "ThreadFileReceive.h"
#include "ThreadMonitor.h"
#include "ThreadSerialCom.h"
#include "ThreadSerialShift.h"
#include "ThreadUIUpdate.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "Usart.h"
#include "Warnings.h"
#include "States.h"

/*** defines ***/

#define ARM_FIRMWARE_VERSION "1.0.0-nova"

/*** module variables ***/

unsigned int system_clock_speed = 800000000;
unsigned int uptime_sec = 0;
unsigned int uptime_ms = 0;
unsigned int reset_cause = 0;
unsigned int net_up = 0;
unsigned char system_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/*** static functions ***/

static void monitor_thread(void *pvParameters);
static void UI_update_thread(void *pvParameters);
static void system_control_thread(void *pvParameters);
static void serial_shift_thread(void *pvParameters);
static void file_receive_thread(void *pvParameters);

/***************************************************************************
 * main() — Initialize hardware and start FreeRTOS
 ***************************************************************************/
int main(void)
{
    /* Initialize IPC shared memory region (before any other init) */
    {
        volatile IpcRegion *ipc = IPC;
        /* Zero the entire IPC region on first boot (R5F-0 owns init) */
        volatile uint8_t *p = (volatile uint8_t *)ipc;
        for (uint32_t i = 0; i < sizeof(IpcRegion); i++) p[i] = 0;
        ipc->magic = IPC_MAGIC;
        ipc->version = IPC_VERSION;
        ipc->init_core = CORE_ID_CONTROL;
        ipc->heartbeat[CORE_ID_CONTROL].state = CORE_STATE_BOOTING;
        const char *ver = ARM_FIRMWARE_VERSION;
        for (int i = 0; i < 31 && ver[i]; i++) {
            ipc->heartbeat[CORE_ID_CONTROL].version[i] = ver[i];
        }
        __asm volatile("DMB" ::: "memory");
    }

    /* Initialize pin mux and peripherals */
    PinoutSet();

    /* Debug banner */
    debug_printf("\033[2J\033[H");
    debug_printf("Nova AM2434: v%s (R5F-0 Control)\r\n", ARM_FIRMWARE_VERSION);
    debug_printf("CPU: Cortex-R5F @ %u MHz\r\n", system_clock_speed / 1000000);
    debug_printf("MSRAM: 2 MB, Flash: 2 MB\r\n");
    debug_printf("IPC: initialized at 0x%08X\r\n", (unsigned)IPC_BASE_ADDR);

    /* Initialize shared resources */
    CreateSPILocks();
    hal_flash_init();

    /* Initialize Application modules — same calls as TM4C */
    Usart_Init();
    NovaBridge_Init();   /* Wire NovaProto + NovaDataExc to UART1 */
    RS485_Init();
    SerialShiftTimer_Init();
    RTC_Init();

    hal_watchdog_init();

    hal_pwm_init();   /* Program AM2434 EPWM regs (TBCTL, AQCTL) before legacy
                       * PWM_Init() calls PWMClockSet/GenConfigure/PeriodSet. */
    PWM_Init();
    UI_PostMsg_Init();
    QueryTags_Init();
    TempTable_Init();
    Settings_Init();                /* Load factory defaults into RAM Settings */
    SerialShift_Init();             /* Apply Nova-specific setup overrides */

    /* ── Settings vault: ping-pong OSPI persistence ────────────────
     * Read both bank headers, pick the newer valid bank, and overlay
     * its blob on top of the factory/override defaults in RAM.
     * On first boot (blank flash) this is a no-op and Settings keeps
     * the defaults populated by Settings_Init + SerialShift_Init. */
    NovaSettings_Init();
    {
        size_t loaded_len = 0;
        NssResult r = NovaSettings_Load(&Settings, sizeof(Settings),
                                        &loaded_len);
        if (r == NSS_OK) {
            debug_printf("[Settings] Loaded %u bytes from bank %s seq=%lu\r\n",
                         (unsigned)loaded_len,
                         NovaSettings_GetActiveBank() == 0 ? "A" : "B",
                         (unsigned long)NovaSettings_GetSequence());
            /* Settings_Init's legacy ReadSettings can't see the OSPI
             * banks (it probes legacy TM4C flash addresses that are
             * unmapped under QEMU/AM2434), so it always raises
             * WARN_SAVESETTINGS + WARN_FACTORYDEFAULT during boot.
             * Now that Nova has loaded a valid bank on top, those
             * warnings are misleading — clear them here so the UI
             * doesn't show "factory default" / "unable to save" until
             * the user touches anything. */
            WarningsSet(WARN_SAVESETTINGS,   FM_NONE, FM_NONE, NA);
            WarningsSet(WARN_FACTORYDEFAULT, FM_NONE, FM_NONE, NA);
            WarningsSet(WARN_EEPROMACCESS,   FM_NONE, FM_NONE, NA);
        } else if (r == NSS_ERR_NO_VALID_BANK) {
            debug_printf("[Settings] No valid bank — using factory defaults\r\n");
        } else {
            debug_printf("[Settings] Load failed (rc=%d) — using factory defaults\r\n", r);
        }
    }

    SystemTime_Init();
    SystemLogLabel_Init();
    PID_Init();
    CtrlDoorsPulsed_Init();

    FileReceive.Status = FR_IDLE;

    /* Start threads — same as TM4C but with larger stacks for Orbit Modbus */
    debug_printf("Starting Serial Shift Thread\r\n");
    if (xTaskCreate(serial_shift_thread, "SERIALSHIFT",
                    1024, NULL,  /* 4 KB — orbit has deep call stack */
                    tskIDLE_PRIORITY + THREADSERIALSHIFT_PRIORITY, NULL) != pdTRUE) {
        debug_printf("!!!Failed to start serial_shift_thread!!!\r\n");
        return 1;
    }

    debug_printf("Starting System Control Thread\r\n");
    if (xTaskCreate(system_control_thread, "SYSTEM_CTRL",
                    THREADSERIALCOM_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + THREADSERIALCOM_PRIORITY, NULL) != pdTRUE) {
        debug_printf("!!!Failed to start system_control_thread!!!\r\n");
        return 1;
    }

    debug_printf("Starting UI Update Thread\r\n");
    /* Nova override: bump stack from 1024 to 16384 words.  The override
     * ThreadUIUpdate now runs NovaProto_FeedByte → dispatch_envelope →
     * settings handlers AND NovaMsg_Send* path.  NovaProto_SendRaw alone
     * allocates a 4KB COBS scratch buffer on stack, plus encoders use
     * additional stack.  8KB still overflows on bursts (UI_SendAllSettings
     * sends ~40 messages back-to-back). */
    if (xTaskCreate(UI_update_thread, "UI_UPDATE",
                    16384, NULL,
                    tskIDLE_PRIORITY + THREADUIUPDATE_PRIORITY, NULL) != pdTRUE) {
        debug_printf("!!!Failed to start UI_update_thread!!!\r\n");
        return 1;
    }

    debug_printf("Starting Thread Monitor\r\n");
    if (xTaskCreate(monitor_thread, "TASKMONITOR",
                    THREADMONITOR_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + THREADMONITOR_PRIORITY, NULL) != pdTRUE) {
        debug_printf("!!!Failed to start monitor_thread!!!\r\n");
        return 1;
    }

    debug_printf("Starting File Receive Thread\r\n");
    if (xTaskCreate(file_receive_thread, "FILERECEIVE",
                    THREADFILERECEIVE_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + THREADFILERECEIVE_PRIORITY, NULL) != pdTRUE) {
        debug_printf("!!!Failed to start file_receive_thread!!!\r\n");
        return 1;
    }

    /* Mark control core as running before starting scheduler */
    IPC->heartbeat[CORE_ID_CONTROL].state = CORE_STATE_RUNNING;
    __asm volatile("DMB" ::: "memory");

    debug_printf("Starting Scheduler\r\n");
    vTaskStartScheduler();

    /* Should never reach here */
    while (1) {}
}

/***************************************************************************
 * Thread wrappers — call into Application/ functions (shared with TM4C)
 ***************************************************************************/

static void monitor_thread(void *pvParameters)
{
    ThreadMonitor();
    vTaskDelete(NULL);
}

static void UI_update_thread(void *pvParameters)
{
    ThreadUIUpdate();
    vTaskDelete(NULL);
}

static void serial_shift_thread(void *pvParameters)
{
    ThreadSerialShift();
    vTaskDelete(NULL);
}

static void system_control_thread(void *pvParameters)
{
    ThreadSerialCom();
    vTaskDelete(NULL);
}

static void file_receive_thread(void *pvParameters)
{
    ThreadFileReceive();
    vTaskDelete(NULL);
}

/***************************************************************************
 * FreeRTOS callback hooks
 ***************************************************************************/

void vApplicationStackOverflowHook(xTaskHandle pxTask, char *pcTaskName)
{
    debug_printf("StackOverflow: %s\r\n", pcTaskName);
    while (1) {}
}

void vApplicationIdleHook(void)
{
    static unsigned int last_sec = 0;
    static uint32_t idle_count = 0;

    idle_count++;

    /* Pump PMU-based tick_ms */
    {
        extern uint32_t hal_timer_get_ms(void);
        hal_timer_get_ms();
    }

    /* IPC heartbeat — update every idle pass.
     * The watchdog core (R5F-2) monitors this counter.
     * Cost: one volatile write — trivial. */
    IPC->heartbeat[CORE_ID_CONTROL].counter++;
    IPC->heartbeat[CORE_ID_CONTROL].uptime_ms = uptime_ms;

    if (last_sec != uptime_sec) {
        last_sec = uptime_sec;
        debug_printf("[Idle] up=%u idle=%lu\r\n", uptime_sec, (unsigned long)idle_count);
    }
}
