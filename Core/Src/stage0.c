#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "gw_buttons.h"
#include "gw_sdcard.h"
#include "gw_flash.h"
#include "gw_lcd.h"
#include "gw_gui.h"
#include "main.h"
#include "gittag.h"
#include "ff.h"
#include "diskio.h"
#include "stage0.h"
#include "stm32h7b0xx.h"

#define FLASH_TIMEOUT_VALUE              50000U /* 50 s */

#define RAM_START D1_AXISRAM_BASE /* 0x24000000 */
#define MAX_FILE_SIZE (1024 * 1024) /* 1MB of SRAM */

void enable_screen()
{
    static bool enabled = false;

    if (!enabled)
    {
        lcd_backlight_off();

        /* Power off LCD and external Flash */
        lcd_deinit(&hspi2);

        // Keep this
        // at least 8 frames at the end of power down (lcd_deinit())
        // 4 x 50 ms => 200ms
        for (int i = 0; i < 4; i++) {
            wdog_refresh();
            HAL_Delay(50);
        }

        /* Power on LCD and external Flash */
        lcd_init(&hspi2, &hltdc);

        // Keep this
        for (int i = 0; i < 4; i++) {
            wdog_refresh();
            HAL_Delay(50);
        }

        gw_gui_fill(0x0000);
        lcd_backlight_set(180);
        enabled = true;
    }
}

static void __attribute__((naked)) start_app(void (*const pc)(void), uint32_t sp)
{
    __asm("           \n\
          msr msp, r1 /* load r1 into MSP */\n\
          bx r0       /* branch to the address at r0 */\n\
    ");
}

static void show_info(bool show_press_key) {
    uint8_t line = 0;
    char text[50];
    enable_screen();
    switch_ospi_gpio(true);
    gw_gui_draw_text(10, line++ * 10, GIT_TAG, GUI_WHITE);
    sprintf(text, "BOOT_ADD 0: 0x%04X 1: 0x%04X", *((uint16_t*)0x52002044), *((uint16_t*)0x52002046));
    gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
    sprintf(text, "BANK_SWAP: %d", (*((uint32_t*)0x52002018) & 0x80000000) != 0);
    gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
    line++;
    /*sprintf(text, "Bank 2 PC: 0x%08lX SP: 0x%08lX", pc, sp);
    if ((pc > FLASH_BANK2_BASE) && (pc < FLASH_BANK2_BASE + 256*1024)) {
        gw_gui_draw_text(10, line++ * 10, text, GUI_GREEN);
    } else {
        gw_gui_draw_text(10, line++ * 10, text, GUI_RED);
    }
    line++;*/
    if (show_press_key) {
        gw_gui_draw_text(10, line++ * 10, "Press A/B button to continue", GUI_WHITE);
    } else {
        gw_gui_draw_text(10, line++ * 10, "Failed to boot", GUI_RED);
    }
}

void boot_bank2(void)
{
    uint32_t sp = *((uint32_t *)FLASH_BANK2_BASE);
    uint32_t pc = *((uint32_t *)FLASH_BANK2_BASE + 1);

    // Check that Bank 2 content is valid
    __set_MSP(sp);
    __set_PSP(sp);
    HAL_MPU_Disable();
    start_app((void (*const)(void))pc, (uint32_t)sp);
}

void set_vtor(uint32_t address) {
    SCB->VTOR = address;
    __DSB();
    __ISB();
}

void boot_ram(void)
{
    uint32_t sp = *((uint32_t *)RAM_START);
    uint32_t pc = *((uint32_t *)RAM_START + 1);

    __set_MSP(sp);
    __set_PSP(sp);
    set_vtor(RAM_START);
    start_app((void (*const)(void))pc, (uint32_t)sp);
}



uint8_t buffer[1024*64];

void stage0_main(void)
{
    // TODO Check what we need to run from BOOT_ADD1
    // 0x0001 -> bank0 0x08000000 (mario)
    // 0x0002 -> bank1 0x08000000 (zelda)
    // 0x0003 -> bank0 0x08022000 (bootloader)
    // 0x0004 -> bank1 0x08022000 (retro-go)
    // Fallback ?

    uint32_t pc = *((uint32_t *)FLASH_BANK2_BASE + 1);

    printf("stage0_main()\n");

    // TODO Check hash of mario/zelda extflash + hash of first 1MiB --> mario or zelda ?

    // TODO Press buttons to change configuration?
    // Left: Configure Mario
    // Right: Configure Zelda
    // Up: Configure Retro-Go
    uint32_t boot_buttons = buttons_get();
    //if (boot_buttons & B_PAUSE)
    //{
        show_info(true);
        uint8_t line = 5;
        char text[50];
        while (1) {
            boot_buttons = buttons_get();
            if (boot_buttons & B_POWER) {
                GW_EnterDeepSleep();
            } else if (boot_buttons & (B_A | B_B)) {
                break;
            } else if (boot_buttons & (B_Left)) {
                printf("Mario\n");
                enable_screen();
                switch_ospi_gpio(true);
                line++;
                gw_gui_draw_text(10, line++ * 10, GIT_TAG, GUI_WHITE);
                // TODO Copy 1Mib of Mario extflash to 0x09000000
                // FIXME Only if needed
                gw_gui_draw_text(10, line++ * 10, "Copying Mario extflash", GUI_WHITE);
                OSPI_DisableMemoryMappedMode();
                if (OSPI_Init(&hospi1)) {
                    sprintf(text, "External Flash: %s (%ldMB)", OSPI_GetFlashName(), OSPI_GetFlashSize() / (1024 * 1024));
                    gw_gui_draw_text(10, line++ * 10, text, GUI_GREEN);
                } else {
                    gw_gui_draw_text(10, line++ * 10, "No external flash detected !!!", GUI_RED);
                    gw_gui_draw_text(10, line++ * 10, "Bad soldering ?", GUI_RED);
                }
                // OSPI_EnableMemoryMappedMode();
                sprintf(text, "0x08000000: 0x%08lX 0x%08lX", *((uint32_t*)0x08000000), *((uint32_t*)0x08000004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x08100000: 0x%08lX 0x%08lX", *((uint32_t*)0x08100000), *((uint32_t*)0x08100004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90000000: 0x%08lX 0x%08lX", *((uint32_t*)0x90000000), *((uint32_t*)0x90000004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90100000: 0x%08lX 0x%08lX", *((uint32_t*)0x90100000), *((uint32_t*)0x90100004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90400000: 0x%08lX 0x%08lX", *((uint32_t*)0x90400000), *((uint32_t*)0x90400004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90500000: 0x%08lX 0x%08lX", *((uint32_t*)0x90500000), *((uint32_t*)0x90500004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                if (*((uint32_t*)0x90000000) == 0x01f86efe && *((uint32_t*)0x90000004) == 0x3a2d7730) {
                    gw_gui_draw_text(10, line++ * 10, "Extflash already programmed", GUI_GREEN);
                    printf("Extflash OK\n");
                    OSPI_DisableMemoryMappedMode();
                } else {
                    printf("Program extflash\n");
                    gw_gui_draw_text(10, line++ * 10, "Erase 1MiB", GUI_RED);
                    OSPI_DisableMemoryMappedMode();
                    OSPI_EraseSync(0, 1024*1024);
                    for (int i=0; i<16; i++) {
                        OSPI_EnableMemoryMappedMode();
                        //gw_gui_draw_text(10, line++ * 10, "Copying 64KiB", GUI_WHITE);
                        memcpy(buffer, (void*) 0x90400000 + i*64*1024, 1024*64);
                        OSPI_DisableMemoryMappedMode();
                        // TODO Erase block by block ?
                        OSPI_Program(i*64*1024, buffer, 1024*64);
                    }
                    //OSPI_EnableMemoryMappedMode();
                }
                // TODO Don't swap intflash banks
                gw_gui_draw_text(10, line++ * 10, "Non-swapped intflash banks", GUI_WHITE);
                if (READ_BIT(FLASH->OPTCR, /* FIXME FLASH_OPTCR_SWAP_BANK*/ 0x80000000) != 0U) {
                    gw_gui_draw_text(10, line++ * 10, "Intflash banks already non-swapped", GUI_GREEN);
                    printf("swap OK\n");
                } else {
                    printf("un-swap\n");
                    HAL_FLASH_OB_Unlock();
                    /* Clear SWAP_BANK Bit */
                    CLEAR_BIT(FLASH->OPTSR_PRG, /* FIXME FLASH_OPTSR_SWAP_BANK_OPT*/ 0x80000000);
                    HAL_StatusTypeDef status = FLASH_OB_WaitForLastOperation((uint32_t)FLASH_TIMEOUT_VALUE);
                    if (status != HAL_OK) {
                        // TODO Handle error, abort launch?
                    }
                    HAL_FLASH_OB_Launch();
                }
                sprintf(text, "BANK_SWAP: %d", READ_BIT(FLASH->OPTCR, /* FIXME FLASH_OPTCR_SWAP_BANK*/ 0x80000000) != 0U);
                printf("swap = %d\n", READ_BIT(FLASH->OPTCR, /* FIXME FLASH_OPTCR_SWAP_BANK*/ 0x80000000) != 0U);
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                // TODO Run 0x08000000
                printf("starting 0x08000000\n");
                gw_gui_draw_text(10, line++ * 10, "Starting bank0 0x08000000", GUI_WHITE);
                /*uint32_t sp = *((uint32_t *)FLASH_BANK1_BASE);
                uint32_t pc = *((uint32_t *)FLASH_BANK1_BASE + 1);
                // Check that Bank 1 content is valid
                __set_MSP(sp);
                __set_PSP(sp);
                HAL_MPU_Disable();
                // FIXME Need to reset for bank swap ??
                start_app((void (*const)(void))pc, (uint32_t)sp);*/
                *BOOTLOADER_MAGIC_ADDRESS = BOOTLOADER_MAGIC_FORCE;
                *BOOTLOADER_JUMP_ADDRESS = (uint32_t *)0x08000000;
                NVIC_SystemReset();
            } else if (boot_buttons & (B_Right)) {
                // TODO Copy 1Mib of Zelda extflash to 0x09000000
                // FIXME Only if needed
                line++;
                gw_gui_draw_text(10, line++ * 10, "Copying Zelda extflash", GUI_WHITE);
                OSPI_DisableMemoryMappedMode();
                if (OSPI_Init(&hospi1)) {
                    sprintf(text, "External Flash: %s (%ldMB)", OSPI_GetFlashName(), OSPI_GetFlashSize() / (1024 * 1024));
                    gw_gui_draw_text(10, line++ * 10, text, GUI_GREEN);
                } else {
                    gw_gui_draw_text(10, line++ * 10, "No external flash detected !!!", GUI_RED);
                    gw_gui_draw_text(10, line++ * 10, "Bad soldering ?", GUI_RED);
                }
                // OSPI_EnableMemoryMappedMode();
                sprintf(text, "0x08000000: 0x%08lX 0x%08lX", *((uint32_t*)0x08000000), *((uint32_t*)0x08000004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x08100000: 0x%08lX 0x%08lX", *((uint32_t*)0x08100000), *((uint32_t*)0x08100004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90000000: 0x%08lX 0x%08lX", *((uint32_t*)0x90000000), *((uint32_t*)0x90000004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90100000: 0x%08lX 0x%08lX", *((uint32_t*)0x90100000), *((uint32_t*)0x90100004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90400000: 0x%08lX 0x%08lX", *((uint32_t*)0x90400000), *((uint32_t*)0x90400004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                sprintf(text, "0x90500000: 0x%08lX 0x%08lX", *((uint32_t*)0x90500000), *((uint32_t*)0x90500004));
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                if (*((uint32_t*)0x90000000) == 0x7344b9fe && *((uint32_t*)0x90000004) == 0xb1c6ebbd) {
                    gw_gui_draw_text(10, line++ * 10, "Extflash already programmed", GUI_GREEN);
                    printf("Extflash OK\n");
                    OSPI_DisableMemoryMappedMode();
                } else {
                    printf("Program extflash\n");
                    gw_gui_draw_text(10, line++ * 10, "Erase 1MiB", GUI_RED);
                    OSPI_DisableMemoryMappedMode();
                    OSPI_EraseSync(0, 1024*1024);
                    for (int i=0; i<16; i++) {
                        OSPI_EnableMemoryMappedMode();
                        //gw_gui_draw_text(10, line++ * 10, "Copying 64KiB", GUI_WHITE);
                        memcpy(buffer, (void*) 0x90500000 + i*64*1024, 1024*64);
                        OSPI_DisableMemoryMappedMode();
                        // TODO Erase block by block ?
                        OSPI_Program(i*64*1024, buffer, 1024*64);
                    }
                    //OSPI_EnableMemoryMappedMode();
                }
                // TODO Swap intflash banks
                gw_gui_draw_text(10, line++ * 10, "Swapped intflash banks", GUI_WHITE);
                if (READ_BIT(FLASH->OPTCR, /* FIXME FLASH_OPTCR_SWAP_BANK*/ 0x80000000) != 0U) {
                    gw_gui_draw_text(10, line++ * 10, "Intflash banks already swapped", GUI_GREEN);
                    printf("swap OK\n");
                } else {
                    printf("swap\n");
                    HAL_FLASH_OB_Unlock();
                    /* Set SWAP_BANK Bit */
                    SET_BIT(FLASH->OPTSR_PRG, /* FIXME FLASH_OPTSR_SWAP_BANK_OPT*/ 0x80000000);
                    HAL_StatusTypeDef status = FLASH_OB_WaitForLastOperation((uint32_t)FLASH_TIMEOUT_VALUE);
                    if (status != HAL_OK) {
                        // TODO Handle error, abort launch?
                    }
                    HAL_FLASH_OB_Launch();
                }
                sprintf(text, "BANK_SWAP: %d", READ_BIT(FLASH->OPTCR, /* FIXME FLASH_OPTCR_SWAP_BANK*/ 0x80000000) != 0U);
                printf("swap = %d\n", READ_BIT(FLASH->OPTCR, /* FIXME FLASH_OPTCR_SWAP_BANK*/ 0x80000000) != 0U);
                gw_gui_draw_text(10, line++ * 10, text, GUI_WHITE);
                // TODO Run 0x08000000
                printf("starting 0x08000000\n");
                gw_gui_draw_text(10, line++ * 10, "Starting bank0 0x08000000", GUI_WHITE);
                /*uint32_t sp = *((uint32_t *)FLASH_BANK1_BASE);
                uint32_t pc = *((uint32_t *)FLASH_BANK1_BASE + 1);
                // Check that Bank 1 content is valid
                __set_MSP(sp);
                __set_PSP(sp);
                HAL_MPU_Disable();
                // FIXME Need to reset for bank swap ??
                start_app((void (*const)(void))pc, (uint32_t)sp);*/
                *BOOTLOADER_MAGIC_ADDRESS = BOOTLOADER_MAGIC_FORCE;
                *BOOTLOADER_JUMP_ADDRESS = (uint32_t *)0x08000000;
                NVIC_SystemReset();
            } else if (boot_buttons & (B_Up)) {
                // TODO Swap intflash banks
                line++;
                gw_gui_draw_text(10, line++ * 10, "Swapped intflash banks", GUI_WHITE);
                /*volatile uint32_t* FLASH_KEY = (uint32_t*) 0x52002008;
                *FLASH_KEY = 0x08192a3b;
                *FLASH_KEY = 0x4c5d6e7f;
                // TODO Sleep ?
                volatile uint32_t* BANK_SWAP = (uint32_t*) 0x52002020;
                *BANK_SWAP |= 0x80000000;*/
                // TODO Sleep ?
                // TODO Run 0x08022000
                gw_gui_draw_text(10, line++ * 10, "Starting bank0 0x08022000", GUI_WHITE);
                /*uint32_t sp = *((uint32_t *)FLASH_BANK1_BASE+0x22000);
                uint32_t pc = *((uint32_t *)FLASH_BANK1_BASE+0x22000 + 1);
                // Check that Bank 1 content is valid
                __set_MSP(sp);
                __set_PSP(sp);
                HAL_MPU_Disable();
                start_app((void (*const)(void))pc, (uint32_t)sp);*/
                /*
                *BOOTLOADER_MAGIC_ADDRESS = BOOTLOADER_MAGIC_FORCE;
                *BOOTLOADER_JUMP_ADDRESS = (uint32_t *)0x08022000;
                NVIC_SystemReset();
                */
            }
        }
    //}

    // If bank 2 is not valid, show info screen
    if ((pc < FLASH_BANK2_BASE) || (pc >= FLASH_BANK2_BASE + 256*1024)) {
        show_info(false);
        while (1) {
            boot_buttons = buttons_get();
            if (boot_buttons & B_POWER) {
                GW_EnterDeepSleep();
            }
        }
    }
    while (1) {
        boot_bank2();
    }
}
