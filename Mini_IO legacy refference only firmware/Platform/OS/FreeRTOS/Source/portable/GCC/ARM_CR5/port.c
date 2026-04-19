/*
 * FreeRTOS port.c — GCC/ARM_CR5 portable layer
 *
 * Cortex-R5F port for FreeRTOS v7.  Provides:
 *   - Task stack initialisation
 *   - Critical section nesting (CPSR I/F bits)
 *   - SVC-based yield
 *   - Tick timer setup (uses RTI or generic timer — configurable)
 *
 * This is a minimal port for QEMU emulation.  On real silicon the
 * tick timer would use the RTI module; here we use the ARM generic
 * timer or a QEMU-provided tick.
 */

#include "FreeRTOS.h"
#include "task.h"

/* Critical nesting count — global, saved/restored per task. */
unsigned long ulCriticalNesting = 9999UL;

/* Constants for initial CPSR value: System mode, IRQ+FIQ enabled, ARM state.
 * Tasks run in System mode so that the IRQ/SVC handlers can save/restore
 * via the System-mode SP using SRSDB/RFEIA instructions. */
#define portINITIAL_SPSR    ( 0x1F )   /* System mode */

/* VFP context: we save D0-D15 + FPSCR = 33 words */
#ifdef configUSE_TASK_FPU_SUPPORT
#define portFPU_CONTEXT_WORDS   33
#else
#define portFPU_CONTEXT_WORDS   0
#endif

/*-----------------------------------------------------------*/

/*
 * pxPortInitialiseStack — set up a new task's stack so that when
 * the context is restored, execution begins at pxCode.
 *
 * Stack layout (top → bottom, full-descending):
 *   [ FPU context if enabled ]
 *   SPSR
 *   PC (= pxCode)
 *   LR
 *   R12, R11, R10, R9, R8, R7, R6, R5, R4, R3, R2, R1, R0
 *   ulCriticalNesting
 */
portSTACK_TYPE *pxPortInitialiseStack( portSTACK_TYPE *pxTopOfStack,
                                       pdTASK_CODE pxCode,
                                       void *pvParameters )
{
    /* Simulate the ARM context as it would be stored by an IRQ handler. */

    /* Skip one word so that after restoring the 17-word context frame
     * (68 bytes), the resulting SP is 8-byte aligned (AAPCS requirement).
     * tasks.c aligns pxTopOfStack to 8; 8-aligned − 4 = 4-mod-8, and
     * (4-mod-8) − 16×4 + 17×4 = (4-mod-8) + 4 = 8-aligned.  */
    pxTopOfStack--;

    /* First on stack is the SPSR (Saved Program Status Register). */
    *pxTopOfStack = portINITIAL_SPSR;
    pxTopOfStack--;

    /* PC — entry point */
    *pxTopOfStack = ( portSTACK_TYPE ) pxCode;
    pxTopOfStack--;

    /* LR — tasks should never return, but use a safe value */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x00000000;
    pxTopOfStack--;

    /* R12 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x12121212;
    pxTopOfStack--;

    /* R11 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x11111111;
    pxTopOfStack--;

    /* R10 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x10101010;
    pxTopOfStack--;

    /* R9 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x09090909;
    pxTopOfStack--;

    /* R8 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x08080808;
    pxTopOfStack--;

    /* R7 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x07070707;
    pxTopOfStack--;

    /* R6 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x06060606;
    pxTopOfStack--;

    /* R5 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x05050505;
    pxTopOfStack--;

    /* R4 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x04040404;
    pxTopOfStack--;

    /* R3 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x03030303;
    pxTopOfStack--;

    /* R2 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x02020202;
    pxTopOfStack--;

    /* R1 */
    *pxTopOfStack = ( portSTACK_TYPE ) 0x01010101;
    pxTopOfStack--;

    /* R0 — task parameter */
    *pxTopOfStack = ( portSTACK_TYPE ) pvParameters;
    pxTopOfStack--;

    /* Critical nesting count — starts at 0. */
    *pxTopOfStack = ( portSTACK_TYPE ) 0;

    return pxTopOfStack;
}

/*-----------------------------------------------------------*/

void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    ulCriticalNesting++;
}

void vPortExitCritical( void )
{
    if( ulCriticalNesting > 0 )
    {
        ulCriticalNesting--;
        if( ulCriticalNesting == 0 )
        {
            portENABLE_INTERRUPTS();
        }
    }
}

/*-----------------------------------------------------------*/

/*
 * vPortYield — trigger a context switch via SVC.
 * In the QEMU minimal port we simply pend a yield.
 */
void vPortYield( void )
{
    __asm volatile( "svc 0" ::: "memory" );
}

/*-----------------------------------------------------------*/

/* These are defined in portASM.S */
extern void vPortRestoreTaskContext( void );

/*
 * xPortStartScheduler — start the first task.
 *
 * Sets up the tick interrupt source, resets the critical nesting
 * count, and restores the context of the first task.
 */
portBASE_TYPE xPortStartScheduler( void )
{
    /* Initialise the critical nesting count ready for the first task. */
    ulCriticalNesting = 0;

    /* Start the timer that generates the tick ISR. */
    /* For QEMU: the tick comes from the machine model's timer or
       we rely on a cooperative yield for now. */

    /* Start the first task — this never returns. */
    vPortRestoreTaskContext();

    /* Should never get here. */
    return 0;
}

/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
    /* Not implemented — tasks run forever. */
}

/*-----------------------------------------------------------*/

/* Called by the IRQ handler in portASM.S to process the tick.
 * MUST NOT call vPortYield() — context switch is handled by the
 * IRQ handler calling vTaskSwitchContext() after this returns. */
void vPortTickHandler( void )
{
    /* Clear the tick timer interrupt source (platform-specific) */
    extern void hal_timer_clear_tick(void);
    hal_timer_clear_tick();

    /* Increment the tick count — may wake a blocked task. */
    xTaskIncrementTick();
}
