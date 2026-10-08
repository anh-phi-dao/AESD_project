/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <string.h>
#include <time.h>
#include "event_log.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define EVENT_LOG_TAG           "EVENT_LOG"
#define EVENT_LOG_NVS_KEY       "store"
#define EVENT_LOG_STORE_VERSION 1U
#define EVENT_LOG_FIRST_ID      1U

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void     event_log_reset_store(event_log_store_t *const p_store, uint32_t old_gen);
static bool     event_log_store_is_valid(const event_log_store_t *const p_store);
static void     event_log_load(event_log_instance_ctrl_t *const p_ctrl);
static void     event_log_save(event_log_instance_ctrl_t *const p_ctrl);
static void     event_log_copy_text(char *const p_dst, size_t dst_size, const char *const p_src);
static uint32_t event_log_next_id(uint32_t *const p_next);
static void event_log_notify(const event_log_instance_ctrl_t *const p_ctrl, const event_log_notice_t *const p_notice);
static app_err_t event_log_check_ready(const event_log_instance_ctrl_t *const p_ctrl);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static const char *const s_method_names[EVENT_LOG_METHOD_COUNT] = {"pin", "nfc", "button", "key"};
static const char *const s_result_names[EVENT_LOG_RESULT_COUNT] = {"granted", "denied"};
static const char *const s_level_names[EVENT_LOG_LEVEL_COUNT]   = {"info", "warning", "critical"};
static const char *const s_code_names[EVENT_LOG_ALERT_COUNT]    = {
    "pin_bruteforce",
    "unknown_card",
    "door_ajar",
    "tamper",
    "low_battery",
    "power_lost",
};

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Creates the lock and loads the persisted log.
 *
 * A missing, older-version, or corrupted blob starts an empty log with a new random generation, so web clients that
 * cached entries of the old log drop them.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged for the lifetime of the instance.
 *
 * @retval APP_SUCCESS                  The log is ready (possibly RAM-only, see the warning log).
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block is already initialized.
 * @retval APP_ERR_NO_MEMORY            The mutex could not be created.
 **********************************************************************************************************************/
app_err_t event_log_init(event_log_instance_ctrl_t *const p_ctrl, const event_log_config_t *const p_cfg)
{
#if EVENT_LOG_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    p_ctrl->mutex = xSemaphoreCreateMutexStatic(&p_ctrl->mutex_buf);
    APP_ERROR_RETURN(NULL != p_ctrl->mutex, APP_ERR_NO_MEMORY);

    p_ctrl->p_cfg = p_cfg;
    event_log_load(p_ctrl);
    p_ctrl->init = true;

    ESP_LOGI(EVENT_LOG_TAG,
             "Loaded %u events, %u alerts (gen %lu)%s",
             (unsigned)p_ctrl->store.event_count,
             (unsigned)p_ctrl->store.alert_count,
             (unsigned long)p_ctrl->store.gen,
             p_ctrl->persist ? "" : ", RAM only");
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Appends one access attempt, overwriting the oldest one when the log is full.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 * @param[in]     method  How the attempt was made.
 * @param[in]     result  Whether the door opened.
 * @param[in]     p_user  User name, or NULL / "" when unknown.
 * @param[out]    p_out   Optional copy of the stored entry, including its id and timestamp.
 *
 * @retval APP_SUCCESS                  The event was stored (in RAM even if the NVS write failed).
 * @retval APP_ERR_INVALID_ARGUMENT     method or result is out of range.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t event_log_add_event(event_log_instance_ctrl_t *const p_ctrl,
                              event_log_method_t               method,
                              event_log_result_t               result,
                              const char *const                p_user,
                              event_log_event_t *const         p_out)
{
    app_err_t ret = event_log_check_ready(p_ctrl);
    if (ret != APP_SUCCESS)
    {
        return ret;
    }
    APP_ERROR_RETURN(((unsigned)method < EVENT_LOG_METHOD_COUNT) && ((unsigned)result < EVENT_LOG_RESULT_COUNT),
                     APP_ERR_INVALID_ARGUMENT);

    event_log_notice_t notice = {.type = EVENT_LOG_NOTICE_EVENT};
    event_log_event_t *p_e    = &notice.event;
    p_e->ts                   = (int64_t)time(NULL);
    p_e->method               = (uint8_t)method;
    p_e->result               = (uint8_t)result;
    event_log_copy_text(p_e->user, sizeof(p_e->user), p_user);

    (void)xSemaphoreTake(p_ctrl->mutex, portMAX_DELAY);
    event_log_store_t *p_store           = &p_ctrl->store;
    p_e->id                              = event_log_next_id(&p_store->next_event_id);
    p_store->events[p_store->event_head] = *p_e;
    p_store->event_head                  = (uint16_t)((p_store->event_head + 1U) % EVENT_LOG_EVENT_CAPACITY);
    if (p_store->event_count < EVENT_LOG_EVENT_CAPACITY)
    {
        p_store->event_count++;
    }
    notice.gen = p_store->gen;
    event_log_save(p_ctrl);
    (void)xSemaphoreGive(p_ctrl->mutex);

    if (p_out != NULL)
    {
        *p_out = *p_e;
    }
    event_log_notify(p_ctrl, &notice);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Appends one alert, overwriting the oldest one when the log is full.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 * @param[in]     level   Severity; warning and critical are pushed to the user.
 * @param[in]     code    What happened.
 * @param[in]     p_msg   Optional human-readable text, or NULL / "".
 * @param[out]    p_out   Optional copy of the stored entry, including its id and timestamp.
 *
 * @retval APP_SUCCESS                  The alert was stored (in RAM even if the NVS write failed).
 * @retval APP_ERR_INVALID_ARGUMENT     level or code is out of range.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t event_log_add_alert(event_log_instance_ctrl_t *const p_ctrl,
                              event_log_level_t                level,
                              event_log_alert_code_t           code,
                              const char *const                p_msg,
                              event_log_alert_t *const         p_out)
{
    app_err_t ret = event_log_check_ready(p_ctrl);
    if (ret != APP_SUCCESS)
    {
        return ret;
    }
    APP_ERROR_RETURN(((unsigned)level < EVENT_LOG_LEVEL_COUNT) && ((unsigned)code < EVENT_LOG_ALERT_COUNT),
                     APP_ERR_INVALID_ARGUMENT);

    event_log_notice_t notice = {.type = EVENT_LOG_NOTICE_ALERT};
    event_log_alert_t *p_a    = &notice.alert;
    p_a->ts                   = (int64_t)time(NULL);
    p_a->level                = (uint8_t)level;
    p_a->code                 = (uint8_t)code;
    event_log_copy_text(p_a->msg, sizeof(p_a->msg), p_msg);

    (void)xSemaphoreTake(p_ctrl->mutex, portMAX_DELAY);
    event_log_store_t *p_store           = &p_ctrl->store;
    p_a->id                              = event_log_next_id(&p_store->next_alert_id);
    p_store->alerts[p_store->alert_head] = *p_a;
    p_store->alert_head                  = (uint16_t)((p_store->alert_head + 1U) % EVENT_LOG_ALERT_CAPACITY);
    if (p_store->alert_count < EVENT_LOG_ALERT_CAPACITY)
    {
        p_store->alert_count++;
    }
    notice.gen = p_store->gen;
    event_log_save(p_ctrl);
    (void)xSemaphoreGive(p_ctrl->mutex);

    if (p_out != NULL)
    {
        *p_out = *p_a;
    }
    event_log_notify(p_ctrl, &notice);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Drops every entry and moves to a new generation.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 *
 * @retval APP_SUCCESS                  The log was cleared.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t event_log_clear(event_log_instance_ctrl_t *const p_ctrl)
{
    app_err_t ret = event_log_check_ready(p_ctrl);
    if (ret != APP_SUCCESS)
    {
        return ret;
    }

    event_log_notice_t notice = {.type = EVENT_LOG_NOTICE_CLEARED};

    (void)xSemaphoreTake(p_ctrl->mutex, portMAX_DELAY);
    event_log_store_t *p_store       = &p_ctrl->store;
    const uint32_t     next_event_id = p_store->next_event_id;
    const uint32_t     next_alert_id = p_store->next_alert_id;
    event_log_reset_store(p_store, p_store->gen);
    /* Ids stay unique for the device's lifetime, so a client can never confuse an old entry with a new one. */
    p_store->next_event_id = next_event_id;
    p_store->next_alert_id = next_alert_id;
    notice.gen             = p_store->gen;
    event_log_save(p_ctrl);
    (void)xSemaphoreGive(p_ctrl->mutex);

    ESP_LOGW(EVENT_LOG_TAG, "Log cleared, new gen %lu", (unsigned long)notice.gen);
    event_log_notify(p_ctrl, &notice);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Copies one page of events, newest first.
 *
 * @param[in,out] p_ctrl     Pointer to the runtime control block.
 * @param[in]     before_id  Only events with a smaller id are returned; EVENT_LOG_NEWEST for the newest page.
 * @param[out]    p_out      Destination array of at least max_count entries.
 * @param[in]     max_count  Page size.
 * @param[out]    p_count    Number of entries written to p_out.
 * @param[out]    p_more     True when older events exist after this page.
 *
 * @retval APP_SUCCESS                  The page was copied.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t event_log_get_events(event_log_instance_ctrl_t *const p_ctrl,
                               uint32_t                         before_id,
                               event_log_event_t *const         p_out,
                               size_t                           max_count,
                               size_t *const                    p_count,
                               bool *const                      p_more)
{
    app_err_t ret = event_log_check_ready(p_ctrl);
    if (ret != APP_SUCCESS)
    {
        return ret;
    }
#if EVENT_LOG_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_count);
    APP_ASSERT(NULL != p_more);
    APP_ASSERT((NULL != p_out) || (0U == max_count));
#endif

    size_t count = 0U;
    bool   more  = false;

    (void)xSemaphoreTake(p_ctrl->mutex, portMAX_DELAY);
    const event_log_store_t *p_store = &p_ctrl->store;
    for (uint16_t i = 0U; i < p_store->event_count; i++)
    {
        /* Walk backwards from the newest entry; ids grow with position, so the first match is the newest. */
        const uint16_t index
            = (uint16_t)((p_store->event_head + EVENT_LOG_EVENT_CAPACITY - 1U - i) % EVENT_LOG_EVENT_CAPACITY);
        const event_log_event_t *p_e = &p_store->events[index];
        if ((before_id != EVENT_LOG_NEWEST) && (p_e->id >= before_id))
        {
            continue;
        }
        if (count == max_count)
        {
            more = true;
            break;
        }
        p_out[count++] = *p_e;
    }
    (void)xSemaphoreGive(p_ctrl->mutex);

    *p_count = count;
    *p_more  = more;
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Copies the generation and every kept entry in one consistent snapshot, newest first.
 *
 * @param[in,out] p_ctrl         Pointer to the runtime control block.
 * @param[out]    p_gen          Current generation.
 * @param[out]    p_events       Array of EVENT_LOG_EVENT_CAPACITY entries.
 * @param[out]    p_event_count  Number of events written.
 * @param[out]    p_alerts       Array of EVENT_LOG_ALERT_CAPACITY entries.
 * @param[out]    p_alert_count  Number of alerts written.
 *
 * @retval APP_SUCCESS                  The snapshot was copied.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t event_log_get_snapshot(event_log_instance_ctrl_t *const p_ctrl,
                                 uint32_t *const                  p_gen,
                                 event_log_event_t *const         p_events,
                                 size_t *const                    p_event_count,
                                 event_log_alert_t *const         p_alerts,
                                 size_t *const                    p_alert_count)
{
    app_err_t ret = event_log_check_ready(p_ctrl);
    if (ret != APP_SUCCESS)
    {
        return ret;
    }
#if EVENT_LOG_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_gen);
    APP_ASSERT(NULL != p_events);
    APP_ASSERT(NULL != p_event_count);
    APP_ASSERT(NULL != p_alerts);
    APP_ASSERT(NULL != p_alert_count);
#endif

    (void)xSemaphoreTake(p_ctrl->mutex, portMAX_DELAY);
    const event_log_store_t *p_store = &p_ctrl->store;
    for (uint16_t i = 0U; i < p_store->event_count; i++)
    {
        p_events[i]
            = p_store->events[(p_store->event_head + EVENT_LOG_EVENT_CAPACITY - 1U - i) % EVENT_LOG_EVENT_CAPACITY];
    }
    for (uint16_t i = 0U; i < p_store->alert_count; i++)
    {
        p_alerts[i]
            = p_store->alerts[(p_store->alert_head + EVENT_LOG_ALERT_CAPACITY - 1U - i) % EVENT_LOG_ALERT_CAPACITY];
    }
    *p_gen         = p_store->gen;
    *p_event_count = p_store->event_count;
    *p_alert_count = p_store->alert_count;
    (void)xSemaphoreGive(p_ctrl->mutex);

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Returns the wire name of a method, or NULL when out of range.
 **********************************************************************************************************************/
const char *event_log_method_name(event_log_method_t method)
{
    return ((unsigned)method < EVENT_LOG_METHOD_COUNT) ? s_method_names[method] : NULL;
}

/***********************************************************************************************************************
 * Returns the wire name of a result, or NULL when out of range.
 **********************************************************************************************************************/
const char *event_log_result_name(event_log_result_t result)
{
    return ((unsigned)result < EVENT_LOG_RESULT_COUNT) ? s_result_names[result] : NULL;
}

/***********************************************************************************************************************
 * Returns the wire name of an alert level, or NULL when out of range.
 **********************************************************************************************************************/
const char *event_log_level_name(event_log_level_t level)
{
    return ((unsigned)level < EVENT_LOG_LEVEL_COUNT) ? s_level_names[level] : NULL;
}

/***********************************************************************************************************************
 * Returns the wire name of an alert code, or NULL when out of range.
 **********************************************************************************************************************/
const char *event_log_alert_code_name(event_log_alert_code_t code)
{
    return ((unsigned)code < EVENT_LOG_ALERT_COUNT) ? s_code_names[code] : NULL;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Empties the store and picks a random generation different from old_gen.
 *
 * Random rather than counted: if NVS is ever erased, a counter would restart and could match a generation a client
 * still remembers, hiding the fact that the log was lost.
 **********************************************************************************************************************/
static void event_log_reset_store(event_log_store_t *const p_store, uint32_t old_gen)
{
    memset(p_store, 0, sizeof(*p_store));
    p_store->version       = EVENT_LOG_STORE_VERSION;
    p_store->next_event_id = EVENT_LOG_FIRST_ID;
    p_store->next_alert_id = EVENT_LOG_FIRST_ID;
    do
    {
        p_store->gen = esp_random();
    } while ((p_store->gen == old_gen) || (p_store->gen == 0U));
}

/***********************************************************************************************************************
 * Rejects blobs whose indexes could read outside the arrays.
 **********************************************************************************************************************/
static bool event_log_store_is_valid(const event_log_store_t *const p_store)
{
    return (p_store->version == EVENT_LOG_STORE_VERSION) && (p_store->gen != 0U)
           && (p_store->event_head < EVENT_LOG_EVENT_CAPACITY) && (p_store->event_count <= EVENT_LOG_EVENT_CAPACITY)
           && (p_store->alert_head < EVENT_LOG_ALERT_CAPACITY) && (p_store->alert_count <= EVENT_LOG_ALERT_CAPACITY)
           && (p_store->next_event_id >= EVENT_LOG_FIRST_ID) && (p_store->next_alert_id >= EVENT_LOG_FIRST_ID);
}

/***********************************************************************************************************************
 * Reads the blob from NVS into the RAM store, or starts a new log.
 **********************************************************************************************************************/
static void event_log_load(event_log_instance_ctrl_t *const p_ctrl)
{
    const char  *p_ns   = (p_ctrl->p_cfg->p_nvs_namespace != NULL) ? p_ctrl->p_cfg->p_nvs_namespace
                                                                   : EVENT_LOG_NVS_NAMESPACE;
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(p_ns, NVS_READWRITE, &handle);
    p_ctrl->persist     = (err == ESP_OK);
    if (!p_ctrl->persist)
    {
        ESP_LOGW(EVENT_LOG_TAG, "NVS unavailable (%s), events will be lost on reboot", esp_err_to_name(err));
        event_log_reset_store(&p_ctrl->store, 0U);
        return;
    }

    size_t size = sizeof(p_ctrl->store);
    err         = nvs_get_blob(handle, EVENT_LOG_NVS_KEY, &p_ctrl->store, &size);
    nvs_close(handle);

    if ((err == ESP_OK) && (size == sizeof(p_ctrl->store)) && event_log_store_is_valid(&p_ctrl->store))
    {
        /* Strings come from flash; make sure they are terminated before anyone prints them. */
        for (uint16_t i = 0U; i < EVENT_LOG_EVENT_CAPACITY; i++)
        {
            p_ctrl->store.events[i].user[EVENT_LOG_USER_SIZE - 1U] = '\0';
        }
        for (uint16_t i = 0U; i < EVENT_LOG_ALERT_CAPACITY; i++)
        {
            p_ctrl->store.alerts[i].msg[EVENT_LOG_ALERT_MSG_SIZE - 1U] = '\0';
        }
        return;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(EVENT_LOG_TAG,
                 "Stored log unusable (%s, %u bytes), starting a new one",
                 esp_err_to_name(err),
                 (unsigned)size);
    }
    event_log_reset_store(&p_ctrl->store, 0U);
    event_log_save(p_ctrl);
}

/***********************************************************************************************************************
 * Writes the RAM store to NVS. Call with the mutex held so the blob is never a mix of two updates.
 **********************************************************************************************************************/
static void event_log_save(event_log_instance_ctrl_t *const p_ctrl)
{
    if (!p_ctrl->persist)
    {
        return;
    }

    const char  *p_ns   = (p_ctrl->p_cfg->p_nvs_namespace != NULL) ? p_ctrl->p_cfg->p_nvs_namespace
                                                                   : EVENT_LOG_NVS_NAMESPACE;
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(p_ns, NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        err = nvs_set_blob(handle, EVENT_LOG_NVS_KEY, &p_ctrl->store, sizeof(p_ctrl->store));
        if (err == ESP_OK)
        {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(EVENT_LOG_TAG, "Saving the log failed (%s); the entry is kept in RAM only", esp_err_to_name(err));
    }
}

/***********************************************************************************************************************
 * Copies a string into a fixed buffer, never splitting a multi-byte UTF-8 character (Vietnamese names and messages).
 **********************************************************************************************************************/
static void event_log_copy_text(char *const p_dst, size_t dst_size, const char *const p_src)
{
    size_t len = 0U;
    if (p_src != NULL)
    {
        len = strnlen(p_src, dst_size - 1U);
        if ((len == dst_size - 1U) && (p_src[len] != '\0'))
        {
            /* Truncated: drop continuation bytes (10xxxxxx) and the lead byte they belong to. */
            while ((len > 0U) && (((uint8_t)p_src[len] & 0xC0U) == 0x80U))
            {
                len--;
            }
        }
        memcpy(p_dst, p_src, len);
    }
    p_dst[len] = '\0';
}

/***********************************************************************************************************************
 * Returns the next id and advances the counter. 2^32 entries will not happen in a lock's lifetime.
 **********************************************************************************************************************/
static uint32_t event_log_next_id(uint32_t *const p_next)
{
    const uint32_t id = *p_next;
    *p_next           = id + 1U;
    return id;
}

/***********************************************************************************************************************
 * Invokes the change callback, if any.
 **********************************************************************************************************************/
static void event_log_notify(const event_log_instance_ctrl_t *const p_ctrl, const event_log_notice_t *const p_notice)
{
    if (p_ctrl->p_cfg->p_callback != NULL)
    {
        p_ctrl->p_cfg->p_callback(p_notice, p_ctrl->p_cfg->p_user_ctx);
    }
}

/***********************************************************************************************************************
 * Common guard for the public APIs.
 **********************************************************************************************************************/
static app_err_t event_log_check_ready(const event_log_instance_ctrl_t *const p_ctrl)
{
#if EVENT_LOG_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#else
    (void)p_ctrl;
#endif
    return APP_SUCCESS;
}
