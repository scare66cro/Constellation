#ifndef SYS_FLASH
#define SYS_FLASH

//#include "net_params.h"
#include "SDCard.h"
#include "Settings.h"

// This part has 2 flashs, tiva (onboard/chip) and at15

#define TIVA_FLASH_LEN(bytes)					  ((((bytes)/16384)+ (((bytes)%16384)?1:0))*16384)
#define MICRON_FLASH_LEN(bytes)					((((bytes)/4096)+  (((bytes)%4096)?1:0))*4096)

#define PLACE_TIVA(a)							      (a|(1<<31))
#define PLACE_MICRON(a)                 (a)
#define IS_TIVA(a)                      ((a&(1U<<31))?1:0)
#define CLEAR_TIVA(a)							      (a&~(1<<31))

#define SETTINGS_FILE_SIZE              (48*1024)
#define UPGRADE_FILE_SIZE               (512*1024)

// TIVA
#define TIVA_FLASH_START						    0x000000
#define TIVA_FLASH_LENGTH						    0x100000    // 1MB

#define FLASH_BOOTLOADER						    PLACE_TIVA(TIVA_FLASH_START)
#define FLASH_BOOTLOADER_LENGTH					TIVA_FLASH_LEN(100*1024)

#define FLASH_APP_IMAGE                 PLACE_TIVA(CLEAR_TIVA(FLASH_BOOTLOADER)+FLASH_BOOTLOADER_LENGTH) // This is the main application
#define FLASH_APP_IMAGE_LENGTH          TIVA_FLASH_LEN(192*1024)

#define FLASH_WEB       		 				    PLACE_TIVA(CLEAR_TIVA(FLASH_APP_IMAGE)+FLASH_APP_IMAGE_LENGTH)
#define FLASH_WEB_LENGTH						    TIVA_FLASH_LEN(TIVA_FLASH_LENGTH-(CLEAR_TIVA(FLASH_APP_IMAGE)+FLASH_APP_IMAGE_LENGTH))

#define TIVA_FLASH_END							    (CLEAR_TIVA(FLASH_WEB)+FLASH_WEB_LENGTH)

// AT25
#define MICRON_FLASH_START						  0x0000000
#define MICRON_FLASH_LENGTH						  0x1000000   // 16MB

#define FLASH_UPGRADE_IMAGE             PLACE_MICRON(MICRON_FLASH_START)
#define FLASH_UPGRADE_IMAGE_LENGTH      MICRON_FLASH_LEN(UPGRADE_FILE_SIZE)

#define FLASH_SDCARD_HEADER             PLACE_MICRON(FLASH_UPGRADE_IMAGE + FLASH_UPGRADE_IMAGE_LENGTH)
#define FLASH_SDCARD_HEADER_LENGTH      MICRON_FLASH_LEN(sizeof(SDCARD_HEADER))

// ── A/B dual-bank settings storage ──
// Two identical regions so a power-loss during erase/write never corrupts
// the only copy.  A monotonic sequence number inside the struct tells us
// which bank was written last (highest valid seq wins at boot).
#define FLASH_SETTINGS_BANK_A           PLACE_MICRON(FLASH_SDCARD_HEADER + FLASH_SDCARD_HEADER_LENGTH)
#define FLASH_SETTINGS_BANK_A_LENGTH    MICRON_FLASH_LEN(sizeof(SYSTEM_SETTINGS))

#define FLASH_SETTINGS_BANK_B           PLACE_MICRON(FLASH_SETTINGS_BANK_A + FLASH_SETTINGS_BANK_A_LENGTH)
#define FLASH_SETTINGS_BANK_B_LENGTH    MICRON_FLASH_LEN(sizeof(SYSTEM_SETTINGS))

// Legacy alias — existing code that reads FLASH_ACTIVE_SETTINGS still compiles,
// but SaveSettings()/ReadSettings() now use the A/B logic internally.
#define FLASH_ACTIVE_SETTINGS           FLASH_SETTINGS_BANK_A
#define FLASH_ACTIVE_SETTINGS_LENGTH    FLASH_SETTINGS_BANK_A_LENGTH

// arbitrary placement of ASCII formatted settings file
#define FLASH_SETTINGS_SAVE_FILE        PLACE_MICRON(0x0500000)

//#define MICRON_FLASH_END					      (FLASH_ACTIVE_SETTINGS+FLASH_ACTIVE_SETTINGS_LENGTH)

/*=============================================================================
                       External Function Protoypes
=============================================================================*/

int FlashInit(void);
void FlashEraseArea(unsigned int addr, unsigned int length);
int FlashWrite(unsigned int to, unsigned char *from, unsigned int length);
unsigned int FlashRead(unsigned char *to, unsigned int from, unsigned int length);

#endif
/***   End Of File   ***/
