/* Copyright (c) Andrew Rankin 2026-06-12. */

/* Application in OTA bank A. The builder substitutes the board flash map (env.GenerateLinkerScript). */

MEMORY
{
    FLASH (rx) : ORIGIN = ${FLASH_OTA1_OFFSET}, LENGTH = ${FLASH_OTA1_LENGTH}
}

INCLUDE efm32gg11b820_sections.ld

/* An application image must carry exactly one descriptor (sizeof(lt_ota_image_desc_t), base/ota/lt_ota_image.h). */
ASSERT(__lt_image_desc_end - __lt_image_desc_start == 24, "image descriptor (.lt_image_desc) missing or wrong size")
