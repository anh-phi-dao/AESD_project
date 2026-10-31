#include "system_task.h"

#include "event_log.h"
#include "fingerprint_task.h"
#include "network_task.h"

static void system_on_access(system_method_t method, bool granted, uint8_t user);
static void system_on_alert(system_alert_t alert);
static void system_on_door(bool unlocked);
static void system_on_voice(system_voice_t action);
static void system_on_play(void);

static const system_control_hooks_t s_hooks = {
    .on_access        = system_on_access,
    .on_alert         = system_on_alert,
    .on_door          = system_on_door,
    .on_voice         = system_on_voice,
    .on_play          = system_on_play,
    .on_finger_delete = fingerprint_task_request_delete,
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
    while (1)
    {
        system_control_tick();
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_CONTROL_TICK_MS));
    }
#endif
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
        (void)snprintf(name, sizeof(name), "User %u", user);
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

static void system_on_voice(system_voice_t action)
{
    /* TODO(audio_service): start / stop / cancel recording from the INMP441; on a saved message call
     * system_control_set_message_count(). */
    static const char *const s_names[] = {"start", "stop", "cancel"};
    ESP_LOGI(SYSTEM_CONTROL_TAG, "Voice recording: %s", s_names[action]);
}

static void system_on_play(void)
{
    /* TODO(audio_service): play the newest message from the owner on the speaker. */
    ESP_LOGI(SYSTEM_CONTROL_TAG, "Play the newest message");
}
