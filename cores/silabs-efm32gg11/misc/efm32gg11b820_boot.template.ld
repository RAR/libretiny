/* Copyright (c) Andrew Rankin 2026-06-12. */

/* First-stage bootloader. The builder substitutes the board flash map (env.GenerateLinkerScript). */

MEMORY
{
    FLASH (rx) : ORIGIN = ${FLASH_BOOT_OFFSET}, LENGTH = ${FLASH_BOOT_LENGTH}
}

INCLUDE efm32gg11b820_sections.ld
