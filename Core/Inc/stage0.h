#pragma once
#include <stdint.h>


#define BOOTLOADER_MAGIC_FORCE 0x45435246  // "FRCE"

#define BOOTLOADER_MAGIC_ADDRESS ((uint32_t *)0x2001FFF8)
#define BOOTLOADER_JUMP_ADDRESS ((uint32_t **)0x2001FFFC)

void stage0_main();
