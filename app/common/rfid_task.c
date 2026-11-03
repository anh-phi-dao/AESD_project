#include "rfid_task.h"

#include <string.h>

#include "esp_rom_sys.h"

#define SCL_PIN   7
#define SDA_PIN   15
#define RESET_PIN -1
#define IRQ_PIN   -1

static const char *TAG = "ntag_read";
pn532_io_t         pn532_io;
uint8_t            uid[] = {0, 0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID

uint8_t uid_length; // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

SemaphoreHandle_t uid_mutex;

#define RFID_INIT_TRIES                                                                          \
    5U /* 1 s apart. Then the lock runs without cards rather than without keys and fingerprints. \
        */

static bool s_ready = false;

bool rfid_task_is_ready(void)
{
    return s_ready;
}

/* Before the I2C driver takes the pins: logs the idle levels of SDA / SCL (both must read 1 through the pull-ups), and
 * frees a slave left holding SDA low by a reset in the middle of a byte (9 clocks, then a STOP). */
static void rfid_i2c_bus_check(void)
{
    const gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << SDA_PIN) | (1ULL << SCL_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    (void)gpio_config(&in_cfg);
    esp_rom_delay_us(50);
    const int sda = gpio_get_level(SDA_PIN);
    const int scl = gpio_get_level(SCL_PIN);
    ESP_LOGI(TAG, "I2C idle levels (external pull-ups only): SDA=%d SCL=%d", sda, scl);
    (void)gpio_set_pull_mode(SDA_PIN, GPIO_PULLUP_ONLY);
    (void)gpio_set_pull_mode(SCL_PIN, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(200);
    ESP_LOGI(TAG,
             "I2C idle levels with the internal pull-ups: SDA=%d SCL=%d",
             gpio_get_level(SDA_PIN),
             gpio_get_level(SCL_PIN));
    if (0 == scl)
    {
        ESP_LOGE(TAG,
                 "SCL (IO%d) is held low: a device on the bus is stuck or the line is shorted / not pulled up",
                 SCL_PIN);
        return;
    }
    if (0 != sda)
    {
        return;
    }

    ESP_LOGW(TAG, "SDA (IO%d) is held low, clocking the bus free", SDA_PIN);
    (void)gpio_set_direction(SCL_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    for (int i = 0; (i < 9) && (0 == gpio_get_level(SDA_PIN)); i++)
    {
        (void)gpio_set_level(SCL_PIN, 0);
        esp_rom_delay_us(10);
        (void)gpio_set_level(SCL_PIN, 1);
        esp_rom_delay_us(10);
    }
    (void)gpio_set_direction(SDA_PIN, GPIO_MODE_INPUT_OUTPUT_OD); /* STOP: SDA low -> high while SCL is high. */
    (void)gpio_set_level(SDA_PIN, 0);
    esp_rom_delay_us(10);
    (void)gpio_set_level(SDA_PIN, 1);
    esp_rom_delay_us(10);
    (void)gpio_set_direction(SDA_PIN, GPIO_MODE_INPUT);
    (void)gpio_set_direction(SCL_PIN, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "After the bus clear: SDA=%d SCL=%d", gpio_get_level(SDA_PIN), gpio_get_level(SCL_PIN));
}

void boot_rfid(void)
{
    esp_err_t err = ESP_FAIL;
    rfid_i2c_bus_check();
    ESP_LOGI(TAG, "init PN532 in I2C mode");
    ESP_ERROR_CHECK(pn532_new_driver_i2c(SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN, I2C_NUM_0, &pn532_io));
    for (uint8_t tries = 0U; (tries < RFID_INIT_TRIES) && (ESP_OK != err); tries++)
    {
        err = pn532_init(&pn532_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to initialize PN532");
            pn532_release(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    ESP_LOGI(TAG, "get firmware version");
    uint32_t version_data = 0;
    for (uint8_t tries = 0U; (tries < RFID_INIT_TRIES) && (ESP_OK == err); tries++)
    {
        err = pn532_get_firmware_version(&pn532_io, &version_data);
        if (ESP_OK == err)
        {
            break;
        }
        ESP_LOGI(TAG, "Didn't find PN53x board");
        pn532_reset(&pn532_io);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if (ESP_OK != err)
    {
        ESP_LOGE(TAG, "No PN532: cards disabled until restart");
        pn532_release(&pn532_io); /* Also frees the I2C bus: the keypad then creates its own. */
        return;
    }

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
    s_ready = true;
}

void rfid_task(void *pvParameter)
{
    esp_err_t err;
    if (!s_ready || (uid_mutex == NULL))
    {
        ESP_LOGW(TAG, "No card reader, RFID task ends");
        vTaskDelete(NULL);
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
