/* GCC linker script for TI AM2434 Cortex-R5F — Constellation Nova
 *
 * Memory map:
 *   ATCM (0x00000000): 64 KB — reset vector + interrupt handlers + hot code
 *   BTCM (0x41010000): 64 KB — fast data (stacks, ISR buffers)
 *   MSRAM (0x70000000): 2 MB — main code + data + FreeRTOS heap
 *   FLASH (0x60000000): 2 MB — on-chip flash (firmware storage)
 *   OSPI  (0x50000000): 8 MB — external QSPI NOR (settings vault)
 *
 * For QEMU development, firmware is loaded into ATCM at 0x0.
 * The R5F resets to 0x00000000 with the reset handler in ATCM.
 * Bulk code/data lives in MSRAM; ATCM holds only vectors + trampoline.
 */

MEMORY
{
    ATCM  (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
    BTCM  (rwx) : ORIGIN = 0x41010000, LENGTH = 64K
    MSRAM (rwx) : ORIGIN = 0x70000000, LENGTH = 2048K
    FLASH (rx)  : ORIGIN = 0x60000000, LENGTH = 2048K
    OSPI  (r)   : ORIGIN = 0x50000000, LENGTH = 8192K
}

/* Stack at top of BTCM */
_estack = ORIGIN(BTCM) + LENGTH(BTCM);

/* Entry point */
ENTRY(Reset_Handler)

SECTIONS
{
    /* ---- ATCM: vector table + reset handler ---- */
    .vectors :
    {
        . = ALIGN(4);
        KEEP(*(.vectors))
        . = ALIGN(4);
    } > ATCM

    .atcm_text :
    {
        . = ALIGN(4);
        *startup_r5f.o(.text .text*)
        . = ALIGN(4);
    } > ATCM

    /* ---- MSRAM: main code ---- */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > MSRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > MSRAM

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > MSRAM

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > MSRAM

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > MSRAM

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > MSRAM

    _sidata = LOADADDR(.data);

    /* ---- MSRAM: initialized data ---- */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > MSRAM

    /* ---- MSRAM: uninitialized data ---- */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > MSRAM

    /* ---- MSRAM: heap + stack ---- */
    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + 0x10000;   /* 64 KB heap for FreeRTOS (was 32 KB on TM4C) */
        . = ALIGN(8);
    } > MSRAM

    /* ---- BTCM: fast data (ISR stacks, DMA buffers) ---- */
    .btcm_data (NOLOAD) :
    {
        . = ALIGN(4);
        *(.btcm_data)
        *(.btcm_data*)
        . = ALIGN(4);
    } > BTCM

    /* ---- ISR stack in BTCM ---- */
    .isr_stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x1000;   /* 4 KB ISR stack */
        _isr_stack_top = .;
    } > BTCM
}
