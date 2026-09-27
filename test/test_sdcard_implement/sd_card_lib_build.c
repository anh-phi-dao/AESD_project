/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "sd_card.h"
#include "esp_log.h"

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

void app_main(void)
{
    /* 1. Initialize SD card (4-bit with 1-bit fallback) */
    if (sd_card_init() != ESP_OK)
    {
        return;
    }

    /* 2. Print performance info and check card descriptor */
    sd_card_print_performance_info();

    sdmmc_card_t *p_card = NULL;
    if (sd_card_get_info(&p_card) == ESP_OK && p_card != NULL)
    {
        /* Optional: clean all old files before testing */
        /* sd_card_delete_all(SD_CARD_MOUNT_POINT); */
    }

    /* 3. Create/overwrite hello.txt (append = false) */
    const char *init_text = "Hello from ESP32-S3 using SDMMC!\n"
                            "High-speed SD card driver initialized.\n";
    sd_card_write_txt_file("hello.txt", init_text, false);

    /* 4. Append additional text to hello.txt (append = true) */
    const char *append_text = "Appended line 1: Smart Door Lock log entry.\n"
                              "Appended line 2: Access granted.\n";
    sd_card_write_txt_file("hello.txt", append_text, true);

    /* 5. Open and read hello.txt back to terminal */
    sd_card_read_txt_file("hello.txt", NULL, 0);

    /* 6. List all files on the SD card */
    sd_card_list_files(SD_CARD_MOUNT_POINT, 0);

    /* 7. Deinitialize and unmount SD card */
    sd_card_deinit();
}