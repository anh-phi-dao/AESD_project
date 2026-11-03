/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "solenoid_lock.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define SOLENOID_LOCK_TAG          "SOLENOID"
#define SOLENOID_LOCK_INACTIVE_LVL (1U - SOLENOID_LOCK_ACTIVE_LEVEL)

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void solenoid_lock_drive(bool on);
static void solenoid_lock_guard_cb(void *p_arg);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static esp_timer_handle_t s_guard_timer   = NULL;
static volatile bool      s_unlocked      = false;
static volatile bool      s_guard_tripped = false;
/* Keeps the pin, the flag and the guard consistent between callers and the esp_timer task. */
static portMUX_TYPE       s_lock_mux      = portMUX_INITIALIZER_UNLOCKED;

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

app_err_t solenoid_lock_init(void)
{
    /* Latch LOW before the pin becomes an output, so it never glitches HIGH. */
    solenoid_lock_drive(false);

    const gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << SOLENOID_LOCK_CTRL_PIN) | (1ULL << SOLENOID_LOCK_LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, /* Weak help while the board has no external pull-down. */
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (ESP_OK != gpio_config(&io_cfg))
    {
        ESP_LOGE(SOLENOID_LOCK_TAG, "gpio_config failed");
        return APP_FAIL;
    }
    solenoid_lock_drive(false);

    if (NULL == s_guard_timer)
    {
        const esp_timer_create_args_t timer_args = {
            .callback        = solenoid_lock_guard_cb,
            .dispatch_method = ESP_TIMER_TASK,
            .name            = "lock_guard",
        };
        if (ESP_OK != esp_timer_create(&timer_args, &s_guard_timer))
        {
            ESP_LOGE(SOLENOID_LOCK_TAG, "esp_timer_create failed");
            s_guard_timer = NULL;
            return APP_ERR_NO_MEMORY;
        }
    }

    ESP_LOGI(SOLENOID_LOCK_TAG, "Locked (IO%d LOW), guard %u ms", SOLENOID_LOCK_CTRL_PIN, SOLENOID_LOCK_GUARD_MS);
    return APP_SUCCESS;
}

app_err_t solenoid_lock_unlock(void)
{
    /* No guard, no unlock: the solenoid must never stay ON without something to switch it off. */
    APP_ERROR_RETURN(NULL != s_guard_timer, APP_ERR_INVALID_STATE);

    taskENTER_CRITICAL(&s_lock_mux);
    esp_timer_stop(s_guard_timer); /* ESP_ERR_INVALID_STATE when not running: fine, it restarts below. */
    const esp_err_t ret = esp_timer_start_once(s_guard_timer, (uint64_t)SOLENOID_LOCK_GUARD_MS * 1000ULL);
    if (ESP_OK == ret)
    {
        solenoid_lock_drive(true);
    }
    taskEXIT_CRITICAL(&s_lock_mux);

    if (ESP_OK != ret)
    {
        ESP_LOGE(SOLENOID_LOCK_TAG, "Guard start failed (%s), staying locked", esp_err_to_name(ret));
        return APP_FAIL;
    }
    ESP_LOGI(SOLENOID_LOCK_TAG, "Unlocked");
    return APP_SUCCESS;
}

void solenoid_lock_lock(void)
{
    taskENTER_CRITICAL(&s_lock_mux);
    solenoid_lock_drive(false);
    if (NULL != s_guard_timer)
    {
        esp_timer_stop(s_guard_timer);
    }
    taskEXIT_CRITICAL(&s_lock_mux);

    ESP_LOGI(SOLENOID_LOCK_TAG, "Locked");
}

bool solenoid_lock_is_unlocked(void)
{
    return s_unlocked;
}

bool solenoid_lock_take_guard_tripped(void)
{
    taskENTER_CRITICAL(&s_lock_mux);
    const bool tripped = s_guard_tripped;
    s_guard_tripped    = false;
    taskEXIT_CRITICAL(&s_lock_mux);

    return tripped;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

static void solenoid_lock_drive(bool on)
{
    /* Solenoid first when locking, so the LED never shows locked while the coil is still ON. */
    gpio_set_level(SOLENOID_LOCK_CTRL_PIN, on ? SOLENOID_LOCK_ACTIVE_LEVEL : SOLENOID_LOCK_INACTIVE_LVL);
    gpio_set_level(SOLENOID_LOCK_LED_PIN, on ? 1U : 0U);
    s_unlocked = on;
}

static void solenoid_lock_guard_cb(void *p_arg)
{
    (void)p_arg;

    taskENTER_CRITICAL(&s_lock_mux);
    const bool was_unlocked = s_unlocked;
    solenoid_lock_drive(false);
    if (was_unlocked)
    {
        s_guard_tripped = true;
    }
    taskEXIT_CRITICAL(&s_lock_mux);

    if (was_unlocked)
    {
        ESP_LOGW(SOLENOID_LOCK_TAG, "Guard tripped after %u ms, relocked", SOLENOID_LOCK_GUARD_MS);
    }
}
