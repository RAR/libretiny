/* Phase 1 linker script for EFM32GG11B820F2048GM64.
 * Single flash image at 0x00000000 (no bootloader).
 * Vector table at start of flash. Heap above .bss; stack at top of RAM.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 2048K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

ENTRY(Reset_Handler)

_stack_size = 4096;   /* main stack (interrupt context); FreeRTOS task stacks
                       * are allocated separately from the FreeRTOS heap. */

SECTIONS
{
    .isr_vector : {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array : {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } > FLASH
    .init_array : {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } > FLASH
    .fini_array : {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH

    .bss : {
        _sbss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
        __bss_end__ = .;
    } > RAM

    .heap (COPY) : {
        __end__ = .;
        end = __end__;
        KEEP(*(.heap*))
        __HeapLimit = ORIGIN(RAM) + LENGTH(RAM) - _stack_size;
    } > RAM

    .stack_dummy (COPY) : {
        KEEP(*(.stack*))
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
    _sstack = _estack - _stack_size;
    PROVIDE(_StackTop = _estack);

    ASSERT(__HeapLimit >= _ebss, "region RAM overflowed with heap")
}
