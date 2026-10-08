 //Includes   <System Includes> , "Project Includes"

#include "keypad_task.h"
#include "network_task.h"
#include "esp_log.h"
#include "nvs_flash.h"


/***********************************************************************************************************************
 * Initializes the main NVS partition used by the Wi-Fi driver and user data.
 *
 * Erasing is the documented recovery when the partition is full or was written by a newer NVS version. It only
 * touches "nvs"; device credentials live in the separate "devcfg" partition and survive it.
 **********************************************************************************************************************/
static esp_err_t system_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_LOGW(SYSTEM_TAG, "NVS partition is unusable (%s), erasing it", esp_err_to_name(ret));
        ret = nvs_flash_erase();
        if (ret == ESP_OK)
        {
            ret = nvs_flash_init();
        }
    }
    return ret;
}

/***********************************************************************************************************************
 * Application entry point for the production firmware.
 **********************************************************************************************************************/
void app_main(void)
{
    esp_err_t nvs_ret = system_nvs_init();
    if (nvs_ret != ESP_OK)
    {
        ESP_LOGE(SYSTEM_TAG, "NVS init failed: %s", esp_err_to_name(nvs_ret));
    }

#if USED_FREERTOS
    /* The event log tolerates a broken NVS (it then runs from RAM), so start it regardless of nvs_ret. */
    boot_event_log();

    /* Start the network first and independently, so a local peripheral failure can still be reported remotely. */
    if ((nvs_ret == ESP_OK)
        && (xTaskCreate(network_task, "net_start", NETWORK_TASK_STACK, NULL, NETWORK_TASK_PRIORITY, NULL) != pdPASS))
    {
        ESP_LOGE(SYSTEM_TAG, "Failed to create the network task");
    }
#endif

    boot_pcf7584();
    xTaskCreate(keypad_task, "keypad", 4096, NULL, 1, NULL);
}
