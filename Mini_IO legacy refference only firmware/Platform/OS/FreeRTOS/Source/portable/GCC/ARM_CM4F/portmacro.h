/*
 * FreeRTOS V7.1.1 — GCC/ARM_CM4F portmacro.h
 *
 * GCC-compatible port macros for ARM Cortex-M4F.
 *
 * FreeRTOS is free software under the GPL v2 with the FreeRTOS exception.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------
 * Port specific definitions.
 *-----------------------------------------------------------
 */

/* Type definitions. */
#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short
#define portSTACK_TYPE  unsigned portLONG
#define portBASE_TYPE   long

#if( configUSE_16_BIT_TICKS == 1 )
    typedef unsigned portSHORT portTickType;
    #define portMAX_DELAY ( portTickType ) 0xffff
#else
    typedef unsigned portLONG portTickType;
    #define portMAX_DELAY ( portTickType ) 0xffffffff
#endif
/*-----------------------------------------------------------*/

/* Architecture specifics. */
#define portSTACK_GROWTH            ( -1 )
#define portTICK_RATE_MS            ( ( portTickType ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT          8
/*-----------------------------------------------------------*/

/* Scheduler utilities. */
extern void vPortYield( void );
extern void vPortYieldFromISR( void );

#define portYIELD()                         vPortYield()
#define portEND_SWITCHING_ISR( xSwitchRequired ) if( xSwitchRequired ) vPortYieldFromISR()
#define portYIELD_FROM_ISR( x ) portEND_SWITCHING_ISR( x )
/*-----------------------------------------------------------*/

/* Critical section management. */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern void vPortSetInterruptMask( void );
extern void vPortClearInterruptMask( void );

#define portDISABLE_INTERRUPTS()            vPortSetInterruptMask()
#define portENABLE_INTERRUPTS()             vPortClearInterruptMask()
#define portENTER_CRITICAL()                vPortEnterCritical()
#define portEXIT_CRITICAL()                 vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR()       0;vPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)    vPortClearInterruptMask();(void)x

/* Alignment assert is overridden due to FPU context oddity. */
#define portALIGNMENT_ASSERT_pxCurrentTCB ( void )

/*-----------------------------------------------------------*/

/* Task function macros. */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters ) void vFunction( void *pvParameters )

#define portNOP() __asm volatile("nop")

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
