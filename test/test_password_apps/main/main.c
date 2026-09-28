/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "keypad.h"
#include "pcf8574.h"
#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "password.h"
#include "sdkconfig.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define SYSTEM_TAG  "SYSTEM"
#define PCF_SDA_PIN CONFIG_PCF8574_DEFAULT_SDA_PIN
#define PCF_SCL_PIN CONFIG_PCF8574_DEFAULT_SCL_PIN
#if USED_FREERTOS
#define PCF_INT_PIN CONFIG_PCF8574_DEFAULT_INT_PIN
#else
#define PCF_INT_PIN GPIO_NUM_NC
#endif
#define PCF_I2C_ADDR   CONFIG_PCF8574_DEFAULT_I2C_ADDR
#define PCF_I2C_SPEED  CONFIG_PCF8574_DEFAULT_CLK_SPEED_HZ
#define PCF_I2C_CLK    I2C_CLK_SRC_DEFAULT
#define PCF_I2C_GLITCH 7U
#define PCF_I2C_PULLUP true

password_config_t pass_cfg;

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Adapts the generic keypad port-write operation to the PCF8574 driver.
 **********************************************************************************************************************/
static app_err_t system_keypad_port_write(void *p_context, uint8_t value)
{
    return pcf8574_write((pcf8574_instance_ctrl_t *)p_context, value);
}

/***********************************************************************************************************************
 * Adapts the generic keypad port-read operation to the PCF8574 driver.
 **********************************************************************************************************************/
static app_err_t system_keypad_port_read(void *p_context, uint8_t *p_value)
{
    return pcf8574_read((pcf8574_instance_ctrl_t *)p_context, p_value);
}

static const keypad_port_api_t s_keypad_port_api = {
    .p_write = system_keypad_port_write,
    .p_read  = system_keypad_port_read,
};

/***********************************************************************************************************************
 * TEMPORARY KEYPAD DEBUG: remove this helper when keypad input is connected to the application logic.
 **********************************************************************************************************************/
static void system_log_keypad_debug(const keypad_state_t *const p_state)
{
    bool has_pressed_key = false;

    for (uint8_t key_index = 0U; key_index < (KEYPAD_MAX_ROW_COUNT * KEYPAD_MAX_COLUMN_COUNT); key_index++)
    {
        if ((p_state->pressed_keys & (uint16_t)(1U << key_index)) != 0U)
        {
            ESP_LOGI(SYSTEM_TAG, "Keypad key: %c", s_keypad_keymap[key_index]);
            has_pressed_key = true;
        }
    }

    if (!has_pressed_key)
    {
        ESP_LOGI(SYSTEM_TAG, "Keypad key: released");
    }
}

/***********************************************************************************************************************
 * Handles a PCF8574 interrupt in the device worker task context.
 **********************************************************************************************************************/
#if USED_FREERTOS
static void system_pcf8574_intr_callback(uint8_t pin_state, void *p_user_ctx)
{
    (void)p_user_ctx;
    ESP_LOGI(SYSTEM_TAG, "PCF8574 input state changed: 0x%02X", pin_state);
}
#endif

/***********************************************************************************************************************
 * Application entry point for the production firmware.
 **********************************************************************************************************************/
void app_main(void)
{
    /* The control block and configuration remain alive for the entire application lifetime. */
    static pcf8574_instance_ctrl_t pcf_ctrl    = {0};
    static keypad_instance_ctrl_t  keypad_ctrl = {0};
    static const pcf8574_config_t pcf_cfg   = {
        .p_i2c_bus_handle = NULL,
        .i2c_bus_config = {
            .i2c_port                     = I2C_NUM_0,
            .sda_io_num                   = PCF_SDA_PIN,
            .scl_io_num                   = PCF_SCL_PIN,
            .clk_source                   = PCF_I2C_CLK,
            .glitch_ignore_cnt            = PCF_I2C_GLITCH,
            .flags.enable_internal_pullup = PCF_I2C_PULLUP,
        },
        .i2c_device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = PCF_I2C_ADDR,
            .scl_speed_hz    = PCF_I2C_SPEED,
        },
        .int_gpio = PCF_INT_PIN,
#if USED_FREERTOS
        .int_gpio_config = {
            .intr_type    = GPIO_INTR_NEGEDGE,
            .pin_bit_mask = (1ULL << PCF_INT_PIN),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        },
        .p_intr_cb  = system_pcf8574_intr_callback,
#else
        .p_intr_cb  = NULL,
#endif
        .p_user_ctx = NULL,
    };
    static const keypad_config_t keypad_cfg = {
        .p_port_api       = &s_keypad_port_api,
        .p_port_context   = &pcf_ctrl,
        .p_row_masks      = s_keypad_row_masks,
        .p_column_masks   = s_keypad_column_masks,
        .p_keymap         = s_keypad_keymap,
        .row_count        = KEYPAD_MAX_ROW_COUNT,
        .column_count     = KEYPAD_MAX_COLUMN_COUNT,
        .released_pattern = KEYPAD_RELEASED_PATTERN,
        .idle_pattern     = KEYPAD_IDLE_PATTERN,
    };

    ESP_LOGI(SYSTEM_TAG,
             "Initializing PCF8574 (SDA: IO%d, SCL: IO%d, INT: IO%d, Addr: 0x%02X)...",
             PCF_SDA_PIN,
             PCF_SCL_PIN,
             PCF_INT_PIN,
             PCF_I2C_ADDR);

    app_err_t ret = pcf8574_init(&pcf_ctrl, &pcf_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(SYSTEM_TAG, "pcf8574_init failed with application error: %d", ret);
#if USED_FREERTOS
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
#else
        return;
#endif
    }

    ESP_LOGI(SYSTEM_TAG, "PCF8574 initialized successfully.");

    ret = keypad_init(&keypad_ctrl, &keypad_cfg);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_TAG, "keypad_init failed with application error: %d", ret);
        (void)pcf8574_deinit(&pcf_ctrl);
#if USED_FREERTOS
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
#else
        return;
#endif
    }

    ESP_LOGI(SYSTEM_TAG, "Keypad initialized successfully.");

#if USED_FREERTOS
    /*******************************************************************************************************************
     * TEMPORARY KEYPAD TEST: remove this block after keypad scanning is integrated into the application state machine.
     ******************************************************************************************************************/
    keypad_state_t keypad_state       = {0};
    uint16_t       last_pressed_keys  = 0U;
    bool           has_previous_state = false;

    ret = init_device_password(&pass_cfg, 7, "1234567");
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_TAG, "init_device_password failed with application error: %d", ret);
        deinit_device_password(&pass_cfg);
#if USED_FREERTOS
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGI(SYSTEM_TAG, "Password initialized successfully. %s", pass_cfg.password);
#else
        return;
#endif
    while (1)
    {
        ret = keypad_scan(&keypad_ctrl, &keypad_state);
        if (APP_SUCCESS == ret)
        {
            if ((!has_previous_state) || (last_pressed_keys != keypad_state.pressed_keys))
            {
                ESP_LOGI(SYSTEM_TAG,
                         "Keypad state: 0x%04X%s",
                         keypad_state.pressed_keys,
                         keypad_state.ghost_detected ? " (ghost detected)" : "");
                system_log_keypad_debug(&keypad_state);
                last_pressed_keys  = keypad_state.pressed_keys;
                has_previous_state = true;
                switch (fill_password(&keypad_state, &pass_cfg))
                {
                    case AUTHEN_SUCCESS:
                        ESP_LOGI(SYSTEM_TAG, "True input password %s", pass_cfg.input_password);
                        break;
                    case AUTHEN_FAILED:
                        ESP_LOGI(SYSTEM_TAG, "False input password %s", pass_cfg.input_password);
                        break;
                    default:
                        break;
                }
            }
        }
        else
        {
            ESP_LOGE(SYSTEM_TAG, "keypad_scan failed with application error: %d", ret);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
#else
    /* Bare-metal builds have no scheduler, so a periodic scan must be called by the application loop. */
    (void)keypad_scan(&keypad_ctrl, &(keypad_state_t) {0});
#endif
}
