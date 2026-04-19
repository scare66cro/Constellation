/* GCC linker script for AM2434 Nova — QEMU R5F-1 (Communications Bridge)
 *
 * Multi-core AMP layout:
 *   0x00180000 – 0x001BFFFF  R5F-1 (256 KB)
 *   0x001E0000 – 0x001FFFFF  IPC shared (128 KB)
 */

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00180000, LENGTH = 256K
    IPC (rw)  : ORIGIN = 0x001E0000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_isr_stack_top = _estack;

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors :
    {
        . = ALIGN(4);
        KEEP(*(.vectors))
        . = ALIGN(4);
    } > RAM

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
    } > RAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > RAM

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > RAM

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RAM

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RAM

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

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
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + 0x4000;   /* 16 KB heap */
        . = ALIGN(8);
    } > RAM

    .ipc (NOLOAD) :
    {
        . = ALIGN(4);
        _ipc_start = .;
        . = . + LENGTH(IPC);
        _ipc_end = .;
    } > IPC

    /DISCARD/ :
    {
        *(.ARM.attributes*)
        *(.comment)
        *(.note*)
    }
}
