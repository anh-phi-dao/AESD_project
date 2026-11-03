#include "battery_task.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ina219.h"
#include "network_task.h"
#include "nvs.h"

#define BATTERY_I2C_PORT       I2C_NUM_0
#define BATTERY_PERIOD_MS      1000U
#define BATTERY_RETRY_MS       5000U  /* Between two attempts to find the INA219. */
#define BATTERY_MAX_ERRORS     5U     /* Failed reads in a row before the INA219 is opened again. */
#define BATTERY_SAVE_MS        300000U
#define BATTERY_LOG_MS         30000U
#define BATTERY_SHUNT_MOHM     100    /* The bus voltage is read after the shunt: add I * R back for the pack. */

#define BATTERY_NVS_NAMESPACE  "battery"
#define BATTERY_NVS_KEY_CHARGE "charge_uah"

static void    battery_task(void *pvParameter);
static bool    battery_open(void);
static int64_t battery_load_charge(void);
static void    battery_save_charge(int64_t charge_uah);
static void    battery_report(const power_status_t *const p_old, const power_status_t *const p_new);

static ina219_instance_ctrl_t s_ina219;
static power_manager_t        s_pm;
static power_status_t         s_status;
static bool                   s_status_valid  = false;
static portMUX_TYPE           s_status_mux    = portMUX_INITIALIZER_UNLOCKED;
static power_level_t          s_alerted_level = POWER_LEVEL_NORMAL; /* Worst level already sent as an alert. */

void boot_battery(void)
{
    if (pdPASS != xTaskCreatePinnedToCore(battery_task, "battery", BATTERY_TASK_STACK, NULL, 2, NULL, 0))
    {
        ESP_LOGE(BATTERY_TASK_TAG, "Failed to create the battery task");
    }
}

bool battery_task_get_status(power_status_t *const p_out)
{
    if (NULL == p_out)
    {
        return false;
    }

    taskENTER_CRITICAL(&s_status_mux);
    const bool valid = s_status_valid;
    *p_out           = s_status;
    taskEXIT_CRITICAL(&s_status_mux);

    return valid;
}

static void battery_task(void *pvParameter)
{
    (void)pvParameter;

    bool     opened     = false;
    bool     started    = false;
    uint32_t errors     = 0U;
    int64_t  last_us    = 0;
    int64_t  last_save  = 0;
    int64_t  last_log   = 0;
    uint8_t  logged_soc = 0U;

    while (1)
    {
        if (!opened)
        {
            opened = battery_open();
            if (!opened)
            {
                vTaskDelay(pdMS_TO_TICKS(BATTERY_RETRY_MS));
                continue;
            }
            errors  = 0U;
            last_us = esp_timer_get_time();
        }

        ina219_reading_t reading;
        app_err_t        ret = ina219_read(&s_ina219, &reading);
        if (APP_ERR_INVALID_STATE == ret)
        {
            ESP_LOGW(BATTERY_TASK_TAG, "INA219 lost its calibration, writing it again");
            ret = ina219_configure(&s_ina219);
            if (APP_SUCCESS == ret)
            {
                ret = ina219_read(&s_ina219, &reading);
            }
        }

        const int64_t        now_us = esp_timer_get_time();
        const uint32_t       dt_ms  = (uint32_t)((now_us - last_us) / 1000);
        const power_status_t old    = s_pm.status;
        last_us                     = now_us;

        if (APP_SUCCESS != ret)
        {
            errors++;
            if (errors >= BATTERY_MAX_ERRORS)
            {
                ESP_LOGE(BATTERY_TASK_TAG, "INA219 not answering, opening it again");
                power_manager_set_unknown(&s_pm);
                ina219_close(&s_ina219);
                opened = false;
            }
        }
        else
        {
            errors                = 0U;
            const int32_t pack_mv =reading.bus_mv + ((reading.current_ma * BATTERY_SHUNT_MOHM) / 1000);
            if (!started)
            {
                (void)power_manager_init(&s_pm, battery_load_charge(), pack_mv, reading.current_ma);
                started   = true;
                last_save = now_us;
            }
            else
            {
                (void)power_manager_update(&s_pm, pack_mv, reading.current_ma, dt_ms);
            }
        }

        if (started)
        {
            taskENTER_CRITICAL(&s_status_mux);
            s_status       = s_pm.status;
            s_status_valid = (POWER_LEVEL_UNKNOWN != s_pm.status.level);
            taskEXIT_CRITICAL(&s_status_mux);

            battery_report(&old, &s_pm.status);

            /* Save often enough to survive a reset, rarely enough to spare the flash; right away when power fails. */
            if (((now_us - last_save) >= ((int64_t)BATTERY_SAVE_MS * 1000))
                || (s_pm.status.power_lost && !old.power_lost))
            {
                battery_save_charge(s_pm.charge_uah);
                last_save = now_us;
            }

            const uint8_t soc   = s_pm.status.soc_pct;
            const uint8_t delta = (soc > logged_soc) ? (uint8_t)(soc - logged_soc) : (uint8_t)(logged_soc - soc);
            if ((delta >= 1U) || ((now_us - last_log) >= ((int64_t)BATTERY_LOG_MS * 1000)))
            {
                ESP_LOGI(BATTERY_TASK_TAG,
                         "SOC %u%%, %ld mV, %ld mA, level %d",
                         soc,
                         (long)s_pm.status.pack_mv,
                         (long)s_pm.status.current_ma,
                         (int)s_pm.status.level);
                logged_soc = soc;
                last_log   = now_us;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_PERIOD_MS));
    }
}

static bool battery_open(void)
{
    i2c_master_bus_handle_t bus = NULL;

    /* Created by the PN532, or by the keypad when there is no PN532. */
    if (ESP_OK != i2c_master_get_bus_handle(BATTERY_I2C_PORT, &bus))
    {
        ESP_LOGW(BATTERY_TASK_TAG, "No I2C bus yet");
        return false;
    }
    if (APP_SUCCESS != ina219_open(&s_ina219, bus))
    {
        ESP_LOGW(BATTERY_TASK_TAG, "No INA219, battery level unknown");
        return false;
    }

    ESP_LOGI(BATTERY_TASK_TAG, "INA219 ready");
    return true;
}

static int64_t battery_load_charge(void)
{
    nvs_handle_t handle;
    int64_t      charge_uah = -1;

    if (ESP_OK == nvs_open(BATTERY_NVS_NAMESPACE, NVS_READONLY, &handle))
    {
        if (ESP_OK != nvs_get_i64(handle, BATTERY_NVS_KEY_CHARGE, &charge_uah))
        {
            charge_uah = -1;
        }
        nvs_close(handle);
    }
    /* < 0: nothing saved, power_manager_init() then estimates from the voltage. */
    return charge_uah;
}

static void battery_save_charge(int64_t charge_uah)
{
    nvs_handle_t handle;

    if (ESP_OK != nvs_open(BATTERY_NVS_NAMESPACE, NVS_READWRITE, &handle))
    {
        ESP_LOGW(BATTERY_TASK_TAG, "Cannot open NVS to save the charge");
        return;
    }
    if ((ESP_OK != nvs_set_i64(handle, BATTERY_NVS_KEY_CHARGE, charge_uah)) || (ESP_OK != nvs_commit(handle)))
    {
        ESP_LOGW(BATTERY_TASK_TAG, "Saving the charge failed");
    }
    nvs_close(handle);
}

static void battery_report(const power_status_t *const p_old, const power_status_t *const p_new)
{
    event_log_instance_ctrl_t *const p_log = network_task_get_event_log();
    if (NULL == p_log)
    {
        return;
    }

    if (p_new->power_lost && !p_old->power_lost)
    {
        (void)event_log_add_alert(p_log,
                                  EVENT_LOG_LEVEL_CRITICAL,
                                  EVENT_LOG_ALERT_POWER_LOST,
                                  "Battery almost empty, use the mechanical key if the lock shuts down",
                                  NULL);
    }

    /* Once per step down (also when the lock boots on a low pack), not again after an UNKNOWN gap. Back to NORMAL
     * re-arms it. */
    if (POWER_LEVEL_NORMAL == p_new->level)
    {
        s_alerted_level = POWER_LEVEL_NORMAL;
    }
    else if ((POWER_LEVEL_UNKNOWN != p_new->level) && (p_new->level > s_alerted_level))
    {
        const bool critical = (POWER_LEVEL_CRITICAL == p_new->level);
        (void)event_log_add_alert(p_log,
                                  critical ? EVENT_LOG_LEVEL_CRITICAL : EVENT_LOG_LEVEL_WARNING,
                                  EVENT_LOG_ALERT_LOW_BATTERY,
                                  critical ? "Battery critical, charge it soon" : "Battery low",
                                  NULL);
        s_alerted_level = p_new->level;
    }
}
