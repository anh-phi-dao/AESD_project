#include "system_task.h"

#include "esp_heap_caps.h"
#include "event_log.h"
#include "fingerprint_task.h"
#include "network_task.h"
#include "voice_task.h"

static void system_on_access(system_method_t method, bool granted, uint8_t user);
static void system_task_log_stacks(void);
static void system_on_alert(system_alert_t alert);
static void system_on_door(bool unlocked);
static void system_on_voice(system_voice_t action, bool owner);

static const system_control_hooks_t s_hooks = {
    .on_access         = system_on_access,
    .on_alert          = system_on_alert,
    .on_door           = system_on_door,
    .on_voice          = system_on_voice,
    .on_play           = voice_task_play,
    .on_play_stop      = voice_task_play_stop,
    .on_message_delete = voice_task_delete_owner,
    .on_finger_delete  = fingerprint_task_request_delete,
};

void boot_system_control(void)
{
    app_err_t ret = system_control_init(&s_hooks);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_CONTROL_TAG, "system_control_init failed with application error: %d", ret);
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    ESP_LOGI(SYSTEM_CONTROL_TAG, "System control initialized successfully");
}

void system_control_task(void *pvParameter)
{
    (void)pvParameter;
#if USED_FREERTOS
    uint32_t ticks = 0U;
    while (1)
    {
        system_control_tick();
        if (0U == (++ticks % SYSTEM_RAM_LOG_TICKS))
        {
            /* No PSRAM: the lowest point shows how close TLS, voice uploads and Wi-Fi came to running out. */
            ESP_LOGI(SYSTEM_CONTROL_TAG,
                     "RAM free %u, lowest since boot %u, largest block %u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            system_task_log_stacks();
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_CONTROL_TICK_MS));
    }
#endif
}

/* Unused stack of the busiest tasks, in bytes: near 0 means the next deeper call overflows. */
static void system_task_log_stacks(void)
{
    static const char *const s_names[] = {"ui_flow", "lvgl", "voice", "keypad", "rfid_task", "fingerprint", "storage"};

    for (size_t i = 0U; i < (sizeof(s_names) / sizeof(s_names[0])); i++)
    {
        const TaskHandle_t task = xTaskGetHandle(s_names[i]);
        if (NULL != task)
        {
            ESP_LOGI(
                SYSTEM_CONTROL_TAG, "Stack left %-11s %u", s_names[i], (unsigned)uxTaskGetStackHighWaterMark(task));
        }
    }
}

/* Access attempts for the history on the web page. */
static void system_on_access(system_method_t method, bool granted, uint8_t user)
{
    static const event_log_method_t s_methods[]
        = {EVENT_LOG_METHOD_PIN, EVENT_LOG_METHOD_NFC, EVENT_LOG_METHOD_FINGERPRINT};
    event_log_instance_ctrl_t *const p_log                     = network_task_get_event_log();
    char                             name[EVENT_LOG_USER_SIZE] = "";

    if (NULL == p_log)
    {
        return;
    }
    if (0U != user)
    {
        (void)snprintf(name, sizeof(name), "%s", system_control_user_name(user));
    }

    const app_err_t ret = event_log_add_event(
        p_log, s_methods[method], granted ? EVENT_LOG_RESULT_GRANTED : EVENT_LOG_RESULT_DENIED, name, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(SYSTEM_CONTROL_TAG, "event_log_add_event failed with application error: %d", ret);
    }
}

static void system_on_alert(system_alert_t alert)
{
    event_log_instance_ctrl_t *const p_log = network_task_get_event_log();
    event_log_alert_code_t           code  = EVENT_LOG_ALERT_PIN_BRUTEFORCE;
    const char                      *p_msg = "Too many failed attempts (PIN, card, fingerprint), keypad locked";

    if (NULL == p_log)
    {
        return;
    }
    if (SYSTEM_ALERT_ADMIN_LOCKOUT == alert)
    {
        p_msg = "Too many wrong admin logins, keypad locked";
    }
    else if (SYSTEM_ALERT_UNKNOWN_CARD == alert)
    {
        code  = EVENT_LOG_ALERT_UNKNOWN_CARD;
        p_msg = "Unknown card at the reader";
    }

    const app_err_t ret = event_log_add_alert(p_log, EVENT_LOG_LEVEL_WARNING, code, p_msg, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(SYSTEM_CONTROL_TAG, "event_log_add_alert failed with application error: %d", ret);
    }
}

static void system_on_door(bool unlocked)
{
    /* TODO(lock_service): drive the solenoid. */
    ESP_LOGI(SYSTEM_CONTROL_TAG, "Door %s", unlocked ? "unlocked" : "locked");
}

static void system_on_voice(system_voice_t action, bool owner)
{
    switch (action)
    {
        case SYSTEM_VOICE_START:
            voice_task_record_start(owner);
            break;
        case SYSTEM_VOICE_SEND:
            voice_task_record_send();
            break;
        default:
            voice_task_record_stop(SYSTEM_VOICE_STOP == action);
            break;
    }
}
