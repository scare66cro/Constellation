/*
 * FreeRTOS portmacro.h — GCC/ARM_CR5 port
 *
 * Cortex-R5F port macros for FreeRTOS v7.  The R5 uses ARM (A-profile
 * compatible) mode, CPSR for interrupt control, and SVC for yield.
 *
 * Based on FreeRTOS ARM_CR5 port conventions.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------
 * Port specific definitions.
 *-----------------------------------------------------------*/

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

/* Architecture specifics — Cortex-R5 uses full-descending stacks. */
#define portSTACK_GROWTH            ( -1 )
#define portTICK_RATE_MS            ( ( portTickType ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT          8

/*-----------------------------------------------------------*/

/* Scheduler utilities. */
extern void vPortYield( void );

/* On Cortex-R5 we yield by issuing an SVC.  From ISR context we set a
   flag and the tick handler checks it. */
#define portYIELD()                         vPortYield()
#define portEND_SWITCHING_ISR( xSwitchRequired ) \
    if( xSwitchRequired ) vPortYield()
#define portYIELD_FROM_ISR( x )             portEND_SWITCHING_ISR( x )

/*-----------------------------------------------------------*/

/* Critical section management — use CPSR I-bit. */

static inline unsigned long portINLINE_SET_INTERRUPT_MASK( void )
{
    unsigned long cpsr, tmp;
    __asm volatile(
        "mrs  %0, cpsr          \n"
        "orr  %1, %0, #0xC0    \n"  /* set I and F bits */
        "msr  cpsr_c, %1       \n"
        : "=r"(cpsr), "=r"(tmp)
        :
        : "memory"
    );
    return cpsr;
}

static inline void portINLINE_CLEAR_INTERRUPT_MASK( unsigned long cpsr )
{
    __asm volatile(
        "msr  cpsr_c, %0  \n"
        :
        : "r"(cpsr)
        : "memory"
    );
}

#define portDISABLE_INTERRUPTS()    \
    do { __asm volatile( "cpsid if" ::: "memory" ); } while(0)

#define portENABLE_INTERRUPTS()     \
    do { __asm volatile( "cpsie if" ::: "memory" ); } while(0)

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );

#define portENTER_CRITICAL()                vPortEnterCritical()
#define portEXIT_CRITICAL()                 vPortExitCritical()

#define portSET_INTERRUPT_MASK_FROM_ISR()       portINLINE_SET_INTERRUPT_MASK()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)    portINLINE_CLEAR_INTERRUPT_MASK(x)

/*-----------------------------------------------------------*/

/* Task function macros. */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters ) void vFunction( void *pvParameters )

#define portNOP() __asm volatile( "nop" )

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
