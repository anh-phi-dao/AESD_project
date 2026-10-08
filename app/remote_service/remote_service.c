/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <string.h>
#include "remote_service.h"
#include "cJSON.h"
#include "esp_log.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define REMOTE_SERVICE_TAG "REMOTE"
#define REMOTE_SERVICE_QOS 1U

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void remote_service_task(void *p_arg);
static bool remote_service_enqueue(remote_service_instance_ctrl_t *const p_ctrl,
                                   const remote_service_job_t *const     p_job);
static void remote_service_request_recent(remote_service_instance_ctrl_t *const p_ctrl);
static void remote_service_publish_text(remote_service_instance_ctrl_t *const p_ctrl,
                                        const char *const                     p_topic_suffix,
                                        char *const                           p_text,
                                        bool                                  retain);
static void remote_service_publish_recent(remote_service_instance_ctrl_t *const p_ctrl);
static void remote_service_answer_history(remote_service_instance_ctrl_t *const     p_ctrl,
                                          const remote_service_history_req_t *const p_req);
static bool remote_service_rate_ok(remote_service_instance_ctrl_t *const p_ctrl);

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Creates the job queue and the publisher task.
 *
 * The MQTT connection does not have to exist yet: jobs are discarded until the first broker connection, which
 * publishes the full "recent" snapshot anyway.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged for the lifetime of the instance.
 *
 * @retval APP_SUCCESS                  The service is running.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block is already initialized.
 * @retval APP_ERR_NO_MEMORY            The queue or the task could not be created.
 **********************************************************************************************************************/
app_err_t remote_service_init(remote_service_instance_ctrl_t *const p_ctrl, const remote_service_config_t *const p_cfg)
{
#if REMOTE_SERVICE_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);
    APP_ASSERT(NULL != p_cfg->p_log);
    APP_ASSERT(NULL != p_cfg->p_mqtt);
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    p_ctrl->p_cfg = p_cfg;
    p_ctrl->queue = xQueueCreateStatic(
        REMOTE_SERVICE_QUEUE_LEN, sizeof(remote_service_job_t), p_ctrl->queue_storage, &p_ctrl->queue_buf);
    APP_ERROR_RETURN(NULL != p_ctrl->queue, APP_ERR_NO_MEMORY);

    /* Mark ready before the task starts, because callbacks may fire as soon as it exists. */
    p_ctrl->init = true;
    if (xTaskCreate(remote_service_task,
                    "remote_svc",
                    REMOTE_SERVICE_TASK_STACK,
                    p_ctrl,
                    REMOTE_SERVICE_TASK_PRIORITY,
                    &p_ctrl->task)
        != pdPASS)
    {
        p_ctrl->init = false;
        vQueueDelete(p_ctrl->queue);
        p_ctrl->queue = NULL;
        return APP_ERR_NO_MEMORY;
    }
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Queues the publication of a new entry and of the updated snapshot. Never blocks.
 **********************************************************************************************************************/
void remote_service_log_callback(const event_log_notice_t *const p_notice, void *p_user_ctx)
{
    remote_service_instance_ctrl_t *p_ctrl = (remote_service_instance_ctrl_t *)p_user_ctx;
    if ((p_ctrl == NULL) || (!p_ctrl->init) || (p_notice == NULL))
    {
        return;
    }

    remote_service_job_t job = {0};
    switch (p_notice->type)
    {
        case EVENT_LOG_NOTICE_EVENT:
            job.type  = REMOTE_SERVICE_JOB_EVENT;
            job.event = p_notice->event;
            (void)remote_service_enqueue(p_ctrl, &job);
            break;

        case EVENT_LOG_NOTICE_ALERT:
            job.type  = REMOTE_SERVICE_JOB_ALERT;
            job.alert = p_notice->alert;
            (void)remote_service_enqueue(p_ctrl, &job);
            break;

        case EVENT_LOG_NOTICE_CLEARED:
        default:
            break;
    }
    remote_service_request_recent(p_ctrl);
}

/***********************************************************************************************************************
 * Validates a history request in the MQTT task and hands it to the publisher task.
 **********************************************************************************************************************/
void remote_service_mqtt_rx_callback(const mqtt_conn_rx_msg_t *const p_msg, void *p_user_ctx)
{
    remote_service_instance_ctrl_t *p_ctrl = (remote_service_instance_ctrl_t *)p_user_ctx;
    if ((p_ctrl == NULL) || (!p_ctrl->init) || (p_msg == NULL))
    {
        return;
    }
    if (strcmp(p_msg->p_topic_suffix, REMOTE_SERVICE_TOPIC_HISTORY_REQ) != 0)
    {
        return;
    }

    remote_service_job_t job = {.type = REMOTE_SERVICE_JOB_HISTORY};
    if (!remote_service_json_parse_history_req((const char *)p_msg->p_data, p_msg->data_len, &job.history))
    {
        ESP_LOGW(REMOTE_SERVICE_TAG, "Ignored malformed history request");
        return;
    }
    if (!remote_service_rate_ok(p_ctrl))
    {
        ESP_LOGW(
            REMOTE_SERVICE_TAG, "Ignored history request: more than %u per second", REMOTE_SERVICE_HISTORY_MAX_PER_S);
        return;
    }
    (void)remote_service_enqueue(p_ctrl, &job);
}

/***********************************************************************************************************************
 * Republishes the snapshot after every (re)connection, so the retained copy reflects entries made while offline.
 **********************************************************************************************************************/
void remote_service_mqtt_state_callback(mqtt_conn_state_t state, void *p_user_ctx)
{
    remote_service_instance_ctrl_t *p_ctrl = (remote_service_instance_ctrl_t *)p_user_ctx;
    if ((p_ctrl == NULL) || (!p_ctrl->init) || (state != MQTT_CONN_STATE_CONNECTED))
    {
        return;
    }
    p_ctrl->mqtt_started = true;
    remote_service_request_recent(p_ctrl);
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Publisher task: the only place that publishes, so jobs go out in the order they were queued.
 **********************************************************************************************************************/
static void remote_service_task(void *p_arg)
{
    remote_service_instance_ctrl_t *p_ctrl = (remote_service_instance_ctrl_t *)p_arg;
    remote_service_job_t            job;

    while (1)
    {
        if (xQueueReceive(p_ctrl->queue, &job, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        if (job.type == REMOTE_SERVICE_JOB_RECENT)
        {
            /* Clear first: a change made while this snapshot is taken queues a fresh one. */
            p_ctrl->recent_pending = false;
        }
        if (!p_ctrl->mqtt_started)
        {
            continue; /* The first connection publishes the snapshot, which already contains this entry. */
        }

        switch (job.type)
        {
            case REMOTE_SERVICE_JOB_EVENT:
                remote_service_publish_text(
                    p_ctrl, REMOTE_SERVICE_TOPIC_EVENT, remote_service_json_event(&job.event), false);
                break;

            case REMOTE_SERVICE_JOB_ALERT:
                remote_service_publish_text(
                    p_ctrl, REMOTE_SERVICE_TOPIC_ALERT, remote_service_json_alert(&job.alert), false);
                break;

            case REMOTE_SERVICE_JOB_RECENT:
                remote_service_publish_recent(p_ctrl);
                break;

            case REMOTE_SERVICE_JOB_HISTORY:
                remote_service_answer_history(p_ctrl, &job.history);
                break;

            default:
                break;
        }
    }
}

/***********************************************************************************************************************
 * Adds a job without waiting. Returns false when the queue is full.
 **********************************************************************************************************************/
static bool remote_service_enqueue(remote_service_instance_ctrl_t *const p_ctrl,
                                   const remote_service_job_t *const     p_job)
{
    if (xQueueSend(p_ctrl->queue, p_job, 0) != pdTRUE)
    {
        ESP_LOGW(REMOTE_SERVICE_TAG, "Publish queue full, dropped job %d", (int)p_job->type);
        return false;
    }
    return true;
}

/***********************************************************************************************************************
 * Queues one snapshot publication unless one is already waiting; the waiting one will include the latest change.
 **********************************************************************************************************************/
static void remote_service_request_recent(remote_service_instance_ctrl_t *const p_ctrl)
{
    if (p_ctrl->recent_pending)
    {
        return;
    }
    p_ctrl->recent_pending         = true;
    const remote_service_job_t job = {.type = REMOTE_SERVICE_JOB_RECENT};
    if (!remote_service_enqueue(p_ctrl, &job))
    {
        p_ctrl->recent_pending = false;
    }
}

/***********************************************************************************************************************
 * Publishes a cJSON-allocated string at QoS 1 and frees it. A NULL string means the JSON builder ran out of memory.
 **********************************************************************************************************************/
static void remote_service_publish_text(remote_service_instance_ctrl_t *const p_ctrl,
                                        const char *const                     p_topic_suffix,
                                        char *const                           p_text,
                                        bool                                  retain)
{
    if (p_text == NULL)
    {
        ESP_LOGE(REMOTE_SERVICE_TAG, "Out of memory while encoding %s", p_topic_suffix);
        return;
    }

    /* QoS 1 is queued by esp-mqtt while disconnected and sent after the reconnection. */
    const app_err_t ret
        = mqtt_conn_publish(p_ctrl->p_cfg->p_mqtt, p_topic_suffix, p_text, strlen(p_text), REMOTE_SERVICE_QOS, retain);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGW(REMOTE_SERVICE_TAG, "Publishing %s failed with application error: %d", p_topic_suffix, ret);
    }
    cJSON_free(p_text);
}

/***********************************************************************************************************************
 * Publishes the retained snapshot of the most recent events and alerts.
 **********************************************************************************************************************/
static void remote_service_publish_recent(remote_service_instance_ctrl_t *const p_ctrl)
{
    uint32_t gen         = 0U;
    size_t   event_count = 0U;
    size_t   alert_count = 0U;

    if (event_log_get_snapshot(
            p_ctrl->p_cfg->p_log, &gen, p_ctrl->snap_events, &event_count, p_ctrl->snap_alerts, &alert_count)
        != APP_SUCCESS)
    {
        return;
    }
    remote_service_publish_text(
        p_ctrl,
        REMOTE_SERVICE_TOPIC_RECENT,
        remote_service_json_recent(gen, p_ctrl->snap_events, event_count, p_ctrl->snap_alerts, alert_count),
        true);
}

/***********************************************************************************************************************
 * Answers one history request from the event log.
 **********************************************************************************************************************/
static void remote_service_answer_history(remote_service_instance_ctrl_t *const     p_ctrl,
                                          const remote_service_history_req_t *const p_req)
{
    size_t count = 0U;
    bool   more  = false;

    if (event_log_get_events(p_ctrl->p_cfg->p_log, p_req->before_id, p_ctrl->page, p_req->limit, &count, &more)
        != APP_SUCCESS)
    {
        return;
    }
    remote_service_publish_text(p_ctrl,
                                REMOTE_SERVICE_TOPIC_HISTORY_RESP,
                                remote_service_json_history(p_req->req_id, p_ctrl->page, count, more),
                                false);
}

/***********************************************************************************************************************
 * Fixed one-second window limiter for history requests. Runs only in the MQTT task.
 **********************************************************************************************************************/
static bool remote_service_rate_ok(remote_service_instance_ctrl_t *const p_ctrl)
{
    const TickType_t now = xTaskGetTickCount();
    if ((p_ctrl->rate_window_count == 0U) || ((now - p_ctrl->rate_window_start) >= pdMS_TO_TICKS(1000U)))
    {
        p_ctrl->rate_window_start = now;
        p_ctrl->rate_window_count = 0U;
    }
    if (p_ctrl->rate_window_count >= REMOTE_SERVICE_HISTORY_MAX_PER_S)
    {
        return false;
    }
    p_ctrl->rate_window_count++;
    return true;
}
