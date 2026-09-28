#ifndef SD_CARD_CONFIG_H_
#define SD_CARD_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Override with -DUSED_FREERTOS=0 to omit FreeRTOS synchronization code.
 * Keep this value consistent across every translation unit that includes sd_card.h. */
#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#if ((USED_FREERTOS != 0) && (USED_FREERTOS != 1))
#error "USED_FREERTOS must be 0 or 1"
#endif

/* Disabled checks require valid pointers/configuration and correct init/deinit ordering from the caller. */
#ifndef SD_CARD_CFG_PARAMETER_CHECKING
#define SD_CARD_CFG_PARAMETER_CHECKING 1
#endif

#if (SD_CARD_CFG_PARAMETER_CHECKING != 0) && (SD_CARD_CFG_PARAMETER_CHECKING != 1)
#error "SD_CARD_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* SD card filesystem and buffer limits. */
#define SD_CARD_MAX_FILES            10U
#define SD_CARD_ALLOCATION_UNIT_SIZE (16U * 1024U)
#define SD_MAX_DIRECTORY_DEPTH       8
#define SD_CARD_FILE_BUF_SIZE        8192U
#define SD_CARD_PATH_MAX_LEN         512U

#endif /* SD_CARD_CONFIG_H_ */