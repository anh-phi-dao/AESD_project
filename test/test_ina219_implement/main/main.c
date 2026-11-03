/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ina219.h"
#include "power_manager.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define TEST_TAG          "INA219_TEST"
#define TEST_I2C_PORT     I2C_NUM_0
#define TEST_I2C_SDA_PIN  GPIO_NUM_15 /* Same bus as the lock: CONFIG_PCF8574_DEFAULT_SDA_PIN / SCL_PIN. */
#define TEST_I2C_SCL_PIN  GPIO_NUM_7
#define TEST_PERIOD_MS    1000U
#define TEST_SHUNT_MOHM   100 /* As battery_task: the bus voltage is read after the shunt. */

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Step 1: lists every address that answers, so a wrong A0/A1 strap or a missing pull-up shows at once.
 **********************************************************************************************************************/
static void test_scan_bus(i2c_master_bus_handle_t bus)
{
    uint8_t found = 0U;

    ESP_LOGI(TEST_TAG, "================ STEP 1: SCAN I2C BUS ================");
    for (uint16_t addr = 0x08U; addr < 0x78U; addr++)
    {
        if (ESP_OK == i2c_master_probe(bus, addr, 50))
        {
            ESP_LOGI(TEST_TAG, "Found device at 0x%02X%s", addr, (INA219_I2C_ADDRESS == addr) ? " (INA219)" : "");
            found++;
        }
    }
    if (0U == found)
    {
        ESP_LOGE(TEST_TAG, "Nothing answers: check SDA/SCL wiring, pull-ups and 3.3 V on the module");
    }
}

/***********************************************************************************************************************
 * Step 2: reads every second and runs the same SOC estimation as the lock. Runs forever.
 **********************************************************************************************************************/
static void test_read_loop(ina219_instance_ctrl_t *const p_ina)
{
    power_manager_t pm      = {0};
    bool            started = false;
    int64_t         last_us = esp_timer_get_time();

    ESP_LOGI(TEST_TAG, "================ STEP 2: READ BATTERY ================");
    ESP_LOGI(TEST_TAG, ">> Load the pack (open the solenoid) and plug the charger to see the current change sign.");

    while (1)
    {
        ina219_reading_t reading;
        app_err_t        ret = ina219_read(p_ina, &reading);
        if (APP_ERR_INVALID_STATE == ret)
        {
            ESP_LOGW(TEST_TAG, "Calibration lost, writing it again");
            ret = ina219_configure(p_ina);
            if (APP_SUCCESS == ret)
            {
                ret = ina219_read(p_ina, &reading);
            }
        }

        const int64_t  now_us = esp_timer_get_time();
        const uint32_t dt_ms  = (uint32_t)((now_us - last_us) / 1000);
        last_us               = now_us;

        if (APP_SUCCESS != ret)
        {
            ESP_LOGE(TEST_TAG, "Read failed (application error %d)", ret);
        }
        else
        {
            const int32_t pack_mv = reading.bus_mv + ((reading.current_ma * TEST_SHUNT_MOHM) / 1000);
            if (!started)
            {
                /* No saved charge here: start from the voltage, as the lock does on its first boot. */
                (void)power_manager_init(&pm, -1, pack_mv, reading.current_ma);
                started = true;
                ESP_LOGI(TEST_TAG, "OCV estimate at start: %u%%", power_manager_soc_from_ocv(pack_mv));
            }
            else
            {
                (void)power_manager_update(&pm, pack_mv, reading.current_ma, dt_ms);
            }

            ESP_LOGI(TEST_TAG,
                     "bus %ld mV | pack %ld mV | %ld mA (%s) | %ld mW | SOC %u%% | level %d%s",
                     (long)reading.bus_mv,
                     (long)pack_mv,
                     (long)reading.current_ma,
                     (reading.current_ma < -POWER_REST_MA) ? "charging" : "discharging",
                     (long)reading.power_mw,
                     pm.status.soc_pct,
                     (int)pm.status.level,
                     pm.status.power_lost ? " | POWER LOST" : "");
        }

        vTaskDelay(pdMS_TO_TICKS(TEST_PERIOD_MS));
    }
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

void app_main(void)
{
    static ina219_instance_ctrl_t s_ina = {0};
    i2c_master_bus_handle_t       bus   = NULL;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = TEST_I2C_PORT,
        .sda_io_num                   = TEST_I2C_SDA_PIN,
        .scl_io_num                   = TEST_I2C_SCL_PIN,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    if (ESP_OK != i2c_new_master_bus(&bus_cfg, &bus))
    {
        ESP_LOGE(TEST_TAG, "Cannot create the I2C bus");
        return;
    }

    test_scan_bus(bus);

    while (APP_SUCCESS != ina219_open(&s_ina, bus))
    {
        ESP_LOGW(TEST_TAG, "INA219 not answering at 0x%02X, retry in 2 seconds...", INA219_I2C_ADDRESS);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TEST_TAG, "INA219 ready (CAL %u, current LSB %d uA)", INA219_CALIBRATION, INA219_CURRENT_LSB_UA);

    test_read_loop(&s_ina);
}
