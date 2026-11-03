// Includes   <System Includes> , "Project Includes"

#include "keypad_task.h"
#include "rfid_task.h"
#include "network_task.h"
#include "system_task.h"
#include "ui_flow_task.h"
#include "ui_task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "solenoid_lock.h"
#include "audio_task.h"
#include "battery_task.h"
#include "fingerprint_task.h"
#include "storage_task.h"
#include "voice_task.h"

#define SYSTEM_TAG "SYSTEM"

/* Free internal RAM after each boot step, to see what takes it (no PSRAM: TLS needs ~30-40 KB left at the end). */
#define BOOT_MEM(step)                                               \
    ESP_LOGI(SYSTEM_TAG,                                             \
             "RAM after %-14s free %6u, largest %6u",                \
             (step),                                                 \
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), \
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))
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
    /* Step 1, before NVS (an erase can take hundreds of ms): IO38 has no external pull-down on rev A. */
    if (APP_SUCCESS != solenoid_lock_init())
    {
        ESP_LOGE(SYSTEM_TAG, "Solenoid init failed, the door cannot be unlocked");
    }
    BOOT_MEM("start");
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
    BOOT_MEM("event_log+net");
#endif
    boot_system_control();
    /* Right away: it relocks the door and ends timeouts, whatever happens to the peripherals below. */
    xTaskCreatePinnedToCore(system_control_task, "system_control", 4096, NULL, 1, NULL, 1);
    BOOT_MEM("system_control");

    /* Before the reader and the keypad, which wait here forever when their chip is missing, so the screen still comes
     * up. The flow shows system_control, so after boot_system_control(). */
    boot_ui();
    BOOT_MEM("ui");
    boot_ui_flow();
    BOOT_MEM("ui_flow");
    /* Loads the users before anything can read a PIN, a card or a finger, and before boot_fingerprint() clears the
     * sensor templates nobody owns. */
    boot_storage();
    xTaskCreatePinnedToCore(storage_task, "storage", STORAGE_TASK_STACK, NULL, 1, NULL, 0);
    BOOT_MEM("storage");
    /* Early: the NS4168 enable pin floats until boot_audio() drives it low, and the amplifier then hums. */
    boot_audio(); /* Before the keypad, which sets the beep state from its first loop. */
    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 1, NULL, 0);
    boot_rfid(); /* Creates the I2C bus that boot_pcf7584() then joins. */
    xTaskCreatePinnedToCore(rfid_task, "rfid_task", 4096, NULL, 1, NULL, 1);
    BOOT_MEM("rfid");
    boot_fingerprint(); /* Returns at once without a sensor. */
    BOOT_MEM("fingerprint");
    BOOT_MEM("audio");
    boot_voice(); /* After the SD card (boot_storage) and the I2S bus (boot_audio). */
    BOOT_MEM("voice");
    boot_pcf7584();
    xTaskCreatePinnedToCore(keypad_task, "keypad", 4096, NULL, 1, NULL, 1);
    boot_battery(); /* Joins the I2C bus that boot_rfid() or boot_pcf7584() created. */
    BOOT_MEM("battery");

    /* No PSRAM: Wi-Fi, TLS, LVGL and every task stack share internal RAM. A TLS handshake needs about 40 KB more. */
    ESP_LOGI(SYSTEM_TAG,
             "Free internal RAM after boot: %u bytes (largest block %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
