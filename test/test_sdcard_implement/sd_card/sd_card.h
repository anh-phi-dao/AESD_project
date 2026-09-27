#ifndef SD_CARD_H_
#define SD_CARD_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sd_card_config.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Macro definitions
     ******************************************************************************************************************/

#define SD_CARD_MOUNT_POINT "/sdcard"
#define SD_MOUNT_POINT      SD_CARD_MOUNT_POINT

    /*******************************************************************************************************************
     * Typedef definitions
     ******************************************************************************************************************/

    /*******************************************************************************************************************
     * Exported global variables
     ******************************************************************************************************************/

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /* Initialize the SD card via SDMMC (attempts 4-bit mode with 1-bit fallback) and print card diagnostics. */
    esp_err_t sd_card_init(void);

    /* Unmount the FAT filesystem and deinitialize the SDMMC host. */
    esp_err_t sd_card_deinit(void);

    /* Mount the SD card to the virtual filesystem using 1-bit SDMMC mode. */
    esp_err_t sd_card_mount(void);

    /* Unmount the SD card from the virtual filesystem and release card resources. */
    void sd_card_unmount(void);

    /* Check whether the SD card is currently mounted. */
    bool sd_card_is_mounted(void);

    /* Retrieve a pointer to the internal SDMMC card descriptor structure. */
    esp_err_t sd_card_get_info(sdmmc_card_t **card);

    /* Print SDMMC bus parameters, capacity, and theoretical speed information to the terminal. */
    void sd_card_print_performance_info(void);

    /* Create, overwrite, or append text content to a .txt file on the SD card. */
    esp_err_t sd_card_write_txt_file(const char *filename, const char *content, bool append);

    /* Open and read a .txt file from the SD card, logging each line and optionally copying into out_buffer. */
    esp_err_t sd_card_read_txt_file(const char *filename, char *out_buffer, size_t buffer_size);

    /* Recursively list all files and subdirectories starting from the given directory path. */
    void sd_card_list_files(const char *directory, int level);

    /* Recursively delete all files and subdirectories inside the given directory path. */
    esp_err_t sd_card_delete_all(const char *directory);

    /* Format the mounted SD card FAT filesystem to erase all data quickly. */
    esp_err_t sd_card_format(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H_ */