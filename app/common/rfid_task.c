#include "rfid_task.h"

#define SCL_PIN   7
#define SDA_PIN   15
#define RESET_PIN -1
#define IRQ_PIN   -1

static const char *TAG = "ntag_read";
pn532_io_t         pn532_io;
uint8_t            uid[] = {0, 0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID

uint8_t uid_length; // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

SemaphoreHandle_t uid_mutex;

void boot_rfid(void)
{
    esp_err_t err;
    ESP_LOGI(TAG, "init PN532 in I2C mode");
    ESP_ERROR_CHECK(pn532_new_driver_i2c(SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN, I2C_NUM_0, &pn532_io));
    do
    {
        err = pn532_init(&pn532_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to initialize PN532");
            pn532_release(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (err != ESP_OK);

    ESP_LOGI(TAG, "get firmware version");
    uint32_t version_data = 0;
    do
    {
        err = pn532_get_firmware_version(&pn532_io, &version_data);
        if (ESP_OK != err)
        {
            ESP_LOGI(TAG, "Didn't find PN53x board");
            pn532_reset(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (ESP_OK != err);

    // Log firmware infos
    ESP_LOGI(TAG, "Found chip PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);

    ESP_LOGI(TAG, "Waiting for an ISO14443A Card ...");

    uid_mutex = xSemaphoreCreateMutex();
    if (uid_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex for UID list");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

void rfid_task(void *pvParameter)
{
    esp_err_t err;
    if (uid_mutex == NULL)
    {
        ESP_LOGE(TAG, "UID mutex is not initialized");
        return;
    }

    while (1)
    {
        if (xSemaphoreTake(uid_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            ESP_LOGW(TAG, "Failed to take mutex for UID list");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        memset(uid, 0, sizeof(uid));
        uid_length = 0;
        xSemaphoreGive(uid_mutex);

        uint8_t read_uid[sizeof(uid)] = {0};
        uint8_t read_uid_length       = 0;
        // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
        // 'uid' will be populated with the UID, and uid_length will indicate
        // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)

        err = pn532_read_passive_target_id(&pn532_io, PN532_BRTY_ISO14443A_106KBPS, read_uid, &read_uid_length, 0);

        if (ESP_OK == err)
        {
            if (xSemaphoreTake(uid_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
            {
                ESP_LOGW(TAG, "Failed to take mutex to update UID");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            memcpy(uid, read_uid, sizeof(uid));
            uid_length = read_uid_length;
            xSemaphoreGive(uid_mutex);

            // Display some basic information about the card
            ESP_LOGI(TAG, "\nFound an ISO14443A card");
            ESP_LOGI(TAG, "UID Length: %d bytes", read_uid_length);
            ESP_LOGI(TAG, "UID Value:");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, read_uid, read_uid_length, ESP_LOG_INFO);
            system_control_card(read_uid, read_uid_length); /* Unlock, unknown card, or add it in the admin menu. */

            err = pn532_in_list_passive_target(&pn532_io);
            if (err != ESP_OK)
            {
                ESP_LOGI(TAG, "Failed to inList passive target");
                continue;
            }

            NTAG2XX_MODEL ntag_model = NTAG2XX_UNKNOWN;
            err                      = ntag2xx_get_model(&pn532_io, &ntag_model);
            if (err != ESP_OK)
            {
                continue;
            }

            int page_max;
            switch (ntag_model)
            {
                case NTAG2XX_NTAG213:
                    page_max = 45;
                    ESP_LOGI(TAG, "found NTAG213 target (or maybe NTAG203)");
                    break;

                case NTAG2XX_NTAG215:
                    page_max = 135;
                    ESP_LOGI(TAG, "found NTAG215 target");
                    break;

                case NTAG2XX_NTAG216:
                    page_max = 231;
                    ESP_LOGI(TAG, "found NTAG216 target");
                    break;

                default:
                    ESP_LOGI(TAG, "Found unknown NTAG target!");
                    continue;
            }

            for (int page = 0; page < page_max; page += 4)
            {
                uint8_t buf[16];
                err = ntag2xx_read_page(&pn532_io, page, buf, 16);
                if (err == ESP_OK)
                {
                    ESP_LOG_BUFFER_HEXDUMP(TAG, buf, 16, ESP_LOG_INFO);
                }
                else
                {
                    ESP_LOGI(TAG, "Failed to read page %d", page);
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
