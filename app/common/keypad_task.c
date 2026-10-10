/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "keypad_task.h"

static app_err_t system_keypad_port_write(void *p_context, uint8_t value);
static app_err_t system_keypad_port_read(void *p_context, uint8_t *p_value);
static void      timer_callback(void *param);
#if USED_FREERTOS
static void system_pcf8574_intr_callback(uint8_t pin_state, void *p_user_ctx);
static void user_pcf8574_intr_callback(void);
#endif
password_handle_t              pass_cfg;
pcf8574_instance_ctrl_t        pcf_ctrl;
keypad_instance_ctrl_t         keypad_ctrl;
static const keypad_port_api_t s_keypad_port_api = {
    .p_write = system_keypad_port_write,
    .p_read  = system_keypad_port_read,
};
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

keypad_state_t keypad_state       = {0};
uint16_t       last_pressed_keys  = 0U;
bool           has_previous_state = false;

uint8_t                       timer_callback_excecute;
const esp_timer_create_args_t my_timer_args = {.callback = &timer_callback, .name = "Keypad timer"};
esp_timer_handle_t            timer_handler;
/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

static void timer_callback(void *param)
{
    keypad_state_t temp_keypad_state = {0};
    app_err_t      ret               = keypad_scan(&keypad_ctrl, &temp_keypad_state);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_TAG, "keypad_scan failed with application error: %d", ret);
        return;
    }
    if (keypad_state.state != BUTTON_PRESSED)
    {
        return;
    }
    if (keypad_state.pressed_keys == temp_keypad_state.pressed_keys)
    {
        keypad_state.state = BUTTON_PRESSED_HOLD;
    }
    else
    {
        keypad_state.state = BUTTON_PRESSED_RELEASED;
    }
}

static void user_pcf8574_intr_callback(void)
{
    switch (keypad_state.state)
    {
        case BUTTON_PRESSED:
            keypad_state.state = BUTTON_PRESSED_RELEASED;
            break;
        case BUTTON_PRESSED_RELEASED:
            break;
        case BUTTON_HOLDING:
            keypad_state.state = BUTTON_RELEASE;
            ESP_LOGI(SYSTEM_TAG, "Keypad key: released");
            break;
        default:
            break;
    }
}

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
            if (p_state->state == BUTTON_PRESSED_RELEASED)
            {
                ESP_LOGI(SYSTEM_TAG, "Keypad key: %c", s_keypad_keymap[key_index]);
            }
            else if (p_state->state == BUTTON_PRESSED_HOLD)
            {
                ESP_LOGI(SYSTEM_TAG, "Keypad key: %c", s_hold_keypad_keymap[key_index]);
            }
            has_pressed_key = true;
        }
    }
}

/***********************************************************************************************************************
 * Handles a PCF8574 interrupt in the device worker task context.
 **********************************************************************************************************************/
#if USED_FREERTOS
static void system_pcf8574_intr_callback(uint8_t pin_state, void *p_user_ctx)
{
    (void)p_user_ctx;
    user_pcf8574_intr_callback();
    ESP_LOGI(SYSTEM_TAG, "PCF8574 input state changed: 0x%02X", pin_state);
}
#endif

static void pcf7584_keypad_released_action(void)
{
    memset(&keypad_state, 0, sizeof(keypad_state));
    if (esp_timer_is_active(timer_handler) == true)
    {
        ESP_ERROR_CHECK(esp_timer_stop(timer_handler));
    }
    app_err_t ret = keypad_scan(&keypad_ctrl, &keypad_state);

    if (APP_SUCCESS != ret)
    {
        keypad_state.state = BUTTON_RELEASE;
        ESP_LOGE(SYSTEM_TAG, "keypad_scan failed with application error: %d", ret);
        return;
    }
    if (keypad_state.pressed_keys == 0x00)
    {
        keypad_state.state = BUTTON_RELEASE;
        return;
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, 3000000));
    keypad_state.state = BUTTON_PRESSED;
}

static void pcf7584_keypad_pressed_released_action(void)
{
    ESP_LOGI(SYSTEM_TAG,
             "PRESSED_RELEASE Keypad state: 0x%04X%s",
             keypad_state.pressed_keys,
             keypad_state.ghost_detected ? " (ghost detected)" : "");
    system_log_keypad_debug(&keypad_state);
    /*Chỗ này phải update thêm*/
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
    keypad_state.state = BUTTON_RELEASE;
    ESP_LOGI(SYSTEM_TAG, "Keypad key: released");
}

static void pcf7584_keypad_pressed_hold_action(void)
{
    ESP_LOGI(SYSTEM_TAG,
             "PRESSED_HOLD Keypad state: 0x%04X%s",
             keypad_state.pressed_keys,
             keypad_state.ghost_detected ? " (ghost detected)" : "");
    system_log_keypad_debug(&keypad_state);
    char hold_key = keypad_state_to_map_read(&keypad_state);
    switch (hold_key)
    {
        case RECORD:
            ESP_LOGI(SYSTEM_TAG, "Keypad recording");
            break;
        case PLAY:
            ESP_LOGI(SYSTEM_TAG, "Keypad playing");
            break;
        case USER_MODE:
            ESP_LOGI(SYSTEM_TAG, "Keypad set up user mode");
            break;
        case ADMINSTRATOR_MODE:
            ESP_LOGI(SYSTEM_TAG, "Keypad set up adminstration mode");
            break;
        case CHANGE_PASSWORD:
            ESP_LOGI(SYSTEM_TAG, "Keypad change password");
            break;
        default:
            break;
    }
    keypad_state.state = BUTTON_HOLDING;
    ESP_LOGI(SYSTEM_TAG, "Keypad key: holding");
}

void boot_pcf7584(void)
{
    ESP_LOGI(SYSTEM_TAG,
             "Initializing PCF8574 (SDA: IO%d, SCL: IO%d, INT: IO%d, Addr: 0x%02X)...",
             PCF_SDA_PIN,
             PCF_SCL_PIN,
             PCF_INT_PIN,
             PCF_I2C_ADDR);

    app_err_t ret = pcf8574_init(&pcf_ctrl, &pcf_cfg, false);
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
    ret = init_device_password(&pass_cfg, 7, "1234567");
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_TAG, "init_device_password failed with application error: %d", ret);
        deinit_device_password(&pass_cfg);
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGI(SYSTEM_TAG, "Password initialized successfully. %s", pass_cfg.password);

    ESP_ERROR_CHECK(esp_timer_create(&my_timer_args, &timer_handler));
    ESP_LOGI(SYSTEM_TAG, "Timer initialized successfully ");

#else
    return;
#endif
}

void keypad_task(void *pvParameter)
{
#if USED_FREERTOS
    while (1)
    {
        switch (keypad_state.state)
        {
            case BUTTON_RELEASE:
                pcf7584_keypad_released_action();
                break;
            case BUTTON_PRESSED:
                break;
            case BUTTON_PRESSED_RELEASED:
                pcf7584_keypad_pressed_released_action();
                break;
            case BUTTON_PRESSED_HOLD:
                pcf7584_keypad_pressed_hold_action();
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
#else
    /* Bare-metal builds have no scheduler, so a periodic scan must be called by the application loop. */
    (void)keypad_scan(&keypad_ctrl, &(keypad_state_t) {0});
#endif
}
