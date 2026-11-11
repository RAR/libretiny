/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Common section layout for EFM32GG11B820 images (bootloader and both OTA
 * banks). Not used directly: the builder generates efm32gg11b820_{boot,ota1,ota2}.ld
 * from the .template.ld files, which define the FLASH region from the board
 * flash map and INCLUDE this file.
 *
 * The vendor startup (startup_efm32gg11b.S) initialises memory from the CMSIS
 * copy and zero tables below; both must exist even when empty. main/lt_main is
 * entered through __START, not newlib's _start, so the zero table is the only
 * .bss initialisation.
 */

MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

ENTRY(Reset_Handler)

/* Main stack (MSP), at the top of RAM: used by code before the scheduler
 * starts and, after that, by every interrupt and fault handler (FreeRTOS
 * reloads MSP from the vector table when it starts). */
_stack_size = 0x2000;

SECTIONS
{
    .text : {
        /* The vendor startup declares `.section .vectors` without flags, so the
         * input section is not ALLOC; it is only placed inside an ALLOC output
         * section. Folding it into .text puts __Vectors at ORIGIN(FLASH). */
        KEEP(*(.vectors))
        KEEP(*(.isr_vector))
        /* Application image descriptor (base/ota/lt_ota_image.h) at offset
         * LT_OTA_IMAGE_DESC_OFFSET of the image. A number assigned to `.`
         * inside an output section is an offset from the section start, and
         * .text starts at ORIGIN(FLASH). The link fails here if the vector
         * table ever grows past the descriptor. The bootloader has no
         * descriptor; it only skips the same gap. */
        . = 0x400;
        __lt_image_desc_start = .;
        KEEP(*(.lt_image_desc))
        __lt_image_desc_end = .;
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

    /* CMSIS copy table: (source, destination, word count) triplets. */
    .copy.table : {
        . = ALIGN(4);
        __copy_table_start__ = .;
        LONG (__etext)
        LONG (__data_start__)
        LONG ((__data_end__ - __data_start__) / 4)
        LONG (LOADADDR(.ram))
        LONG (__ram_start__)
        LONG ((__ram_end__ - __ram_start__) / 4)
        __copy_table_end__ = .;
    } > FLASH

    /* CMSIS zero table: (start, BYTE count) pairs. The startup's zero loop
     * takes bytes, unlike the copy loop, which takes words. */
    .zero.table : {
        . = ALIGN(4);
        __zero_table_start__ = .;
        LONG (__bss_start__)
        LONG (__bss_end__ - __bss_start__)
        __zero_table_end__ = .;
    } > FLASH

    __etext = ALIGN(4);

    .data : AT (__etext) {
        __data_start__ = .;
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
        . = ALIGN(4);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
        . = ALIGN(4);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
        . = ALIGN(4);
        _edata = .;
        __data_end__ = .;
    } > RAM

    _sidata = LOADADDR(.data);

    /* Code that must execute from RAM (SL_RAMFUNC, e.g. the emlib MSC flash
     * routines with EM_MSC_RUN_FROM_RAM), copied by the copy table above. */
    .ram : AT (LOADADDR(.data) + SIZEOF(.data)) {
        . = ALIGN(4);
        __ram_start__ = .;
        *(.ram .ram.*)
        . = ALIGN(4);
        __ram_end__ = .;
    } > RAM

    /* Length of the loadable image. .ram is the last section with a FLASH load
     * address, so it ends the image. Used by lt_ota.c in the image descriptor;
     * the builder checks it against the length of the objcopy binary. */
    _lt_image_size = LOADADDR(.ram) + SIZEOF(.ram) - ORIGIN(FLASH);

    .bss : {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = .;
    } > RAM

    /* The vendor startup reserves its own .heap and .stack (1 KiB) sections
     * and labels __HeapBase/__HeapLimit and __StackLimit/__StackTop in them.
     * Collect them as COPY sections, which take no RAM, and define the labels
     * here instead; script assignments take precedence over the startup's.
     * Vector 0 (the initial MSP) is __StackTop. */
    .heap (COPY) : {
        KEEP(*(.heap*))
    } > RAM
    .stack_dummy (COPY) : {
        KEEP(*(.stack*))
    } > RAM

    __end__ = __bss_end__;
    end = __end__;
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    __StackLimit = __StackTop - _stack_size;
    /* newlib's heap is unused (malloc goes to the FreeRTOS heap in .bss) */
    __HeapBase = __end__;
    __HeapLimit = __StackLimit;
    _estack = __StackTop;
    _sstack = __StackLimit;
    PROVIDE(_StackTop = __StackTop);

    ASSERT(__StackLimit >= __bss_end__, "region RAM overflowed with the main stack")
    /* AT() load addresses are not checked against FLASH by the linker. */
    ASSERT(LOADADDR(.ram) + SIZEOF(.ram) <= ORIGIN(FLASH) + LENGTH(FLASH), "image does not fit its FLASH region")
}
