/*
 * FreeRTOSConfig.h — FreeRTOS configuration for AM2434 Cortex-R5F
 *
 * Adapted from Mini_IO FreeRTOSConfig.h (Cortex-M4F @ 120 MHz)
 * for Cortex-R5F @ 800 MHz with more SRAM available.
 *
 * Key differences from TM4C config:
 *   - CPU clock: 800 MHz (was 120 MHz)
 *   - Heap size: 64 KB (was 30 KB) — 2 MB MSRAM available
 *   - FreeRTOS port: ARM_CR5 (was ARM_CM4F)
 *   - No SysTick — uses DMTIMER instead (Cortex-R has no SysTick)
 *
 * Copyright (c) 2026 Agristar
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---- Core settings ---- */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ((unsigned long)800000000)
#define configTICK_RATE_HZ                      ((portTickType)1000)
#define configMINIMAL_STACK_SIZE                ((unsigned short)512)
#define configTOTAL_HEAP_SIZE                   ((size_t)(262144))  /* 256 KB — Nova path needs larger UI_UPDATE stack for NovaProto+NovaMsg encoders */
#define configMAX_TASK_NAME_LEN                 (12)
#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_CO_ROUTINES                   0
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

/* ---- Priority settings ---- */
#define configMAX_PRIORITIES                    ((unsigned portBASE_TYPE)16)
#define configMAX_CO_ROUTINE_PRIORITIES         (2)
#define configQUEUE_REGISTRY_SIZE               10

/* ---- Cortex-R5F interrupt settings ---- */
/*
 * Cortex-R doesn't have NVIC like Cortex-M.
 * It uses the VIM (Vectored Interrupt Manager) on AM243x.
 * These settings are for the FreeRTOS ARM_CR5 port.
 */
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS (0x02FFF000)  /* VIM base */
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET (0)
#define configUNIQUE_INTERRUPT_PRIORITIES        (16)
#define configMAX_API_CALL_INTERRUPT_PRIORITY    (10)

/* ---- Optional API inclusion ---- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* ---- Assert ---- */
extern void debug_printf(const char *fmt, ...);
#define configASSERT(x) if (!(x)) { debug_printf("ASSERT: %s:%d\r\n", __FILE__, __LINE__); while(1); }

#endif /* FREERTOS_CONFIG_H */
