#ifndef REMOTE_SERVICE_H_
#define REMOTE_SERVICE_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include "remote_service_config.h"
#include "remote_service_json.h"
#include "app_err.h"
#include "event_log.h"
#include "mqtt_conn.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

typedef enum e_remote_service_job_type
{
    REMOTE_SERVICE_JOB_EVENT = 0, /* Publish job.event on "event". */
    REMOTE_SERVICE_JOB_ALERT,     /* Publish job.alert on "alert". */
    REMOTE_SERVICE_JOB_RECENT,    /* Publish the retained "recent" snapshot. */
    REMOTE_SERVICE_JOB_HISTORY,   /* Answer job.history on "history/resp". */
} remote_service_job_type_t;

/* One unit of work for the publisher task. Copied by value into the queue. */
typedef struct st_remote_service_job
{
    remote_service_job_type_t type;
    union
    {
        event_log_event_t            event;
        event_log_alert_t            alert;
        remote_service_history_req_t history;
    };
} remote_service_job_t;

/* Configuration remains caller-owned and must stay valid and unchanged for the lifetime of the instance. */
typedef struct st_remote_service_config
{
    event_log_instance_ctrl_t *p_log;  /* Initialized event log to read from. */
    mqtt_conn_instance_ctrl_t *p_mqtt; /* MQTT connection; may be initialized later, see remote_service.c. */
} remote_service_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_remote_service_instance_ctrl
{
    const remote_service_config_t *p_cfg;
    QueueHandle_t                  queue;
    StaticQueue_t                  queue_buf;
    uint8_t                        queue_storage[REMOTE_SERVICE_QUEUE_LEN * sizeof(remote_service_job_t)];
    TaskHandle_t                   task;
    /* Work buffers, used only by the publisher task. */
    event_log_event_t snap_events[EVENT_LOG_EVENT_CAPACITY];
    event_log_alert_t snap_alerts[EVENT_LOG_ALERT_CAPACITY];
    event_log_event_t page[REMOTE_SERVICE_HISTORY_LIMIT_MAX];
    /* history/req rate limit, used only by the MQTT task. */
    TickType_t    rate_window_start;
    uint8_t       rate_window_count;
    volatile bool mqtt_started;   /* Set on the first broker connection; publishing before that is pointless. */
    volatile bool recent_pending; /* A "recent" job is queued; further changes are covered by it. */
    bool          init;
} remote_service_instance_ctrl_t;

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Bridges the event log to MQTT: live "event"/"alert", the retained "recent" snapshot, and history/req answers. All
 * publishing happens in one task, so callers never wait for the network and the retained snapshot is never
 * overwritten by an older one. There is deliberately no inbound command: nothing here can open the door. */
#ifdef __cplusplus
extern "C"
{
#endif

    /* Create the queue and the publisher task. Call before event_log starts producing entries. */
    app_err_t remote_service_init(remote_service_instance_ctrl_t *const p_ctrl,
                                  const remote_service_config_t *const  p_cfg);

    /* event_log_config_t::p_callback, with p_user_ctx = the remote_service control block. */
    void remote_service_log_callback(const event_log_notice_t *const p_notice, void *p_user_ctx);

    /* mqtt_conn_config_t::p_rx_cb, with p_user_ctx = the remote_service control block. Subscribe only
     * REMOTE_SERVICE_TOPIC_HISTORY_REQ with max_payload_len REMOTE_SERVICE_HISTORY_REQ_MAX_LEN. */
    void remote_service_mqtt_rx_callback(const mqtt_conn_rx_msg_t *const p_msg, void *p_user_ctx);

    /* mqtt_conn_config_t::p_state_cb, with p_user_ctx = the remote_service control block. */
    void remote_service_mqtt_state_callback(mqtt_conn_state_t state, void *p_user_ctx);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_SERVICE_H_ */
