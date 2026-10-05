#ifndef EVENT_LOG_H_
#define EVENT_LOG_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "event_log_config.h"
#include "app_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Pass as before_id to event_log_get_events() to start from the newest entry. */
#define EVENT_LOG_NEWEST 0U

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Values and their wire names are defined in docs/mqtt_protocol.md. */
typedef enum e_event_log_method
{
    EVENT_LOG_METHOD_PIN = 0, /* "pin" */
    EVENT_LOG_METHOD_NFC,     /* "nfc" */
    EVENT_LOG_METHOD_BUTTON,  /* "button": exit button inside */
    EVENT_LOG_METHOD_KEY,     /* "key": mechanical key */
    EVENT_LOG_METHOD_COUNT
} event_log_method_t;

typedef enum e_event_log_result
{
    EVENT_LOG_RESULT_GRANTED = 0, /* "granted" */
    EVENT_LOG_RESULT_DENIED,      /* "denied" */
    EVENT_LOG_RESULT_COUNT
} event_log_result_t;

typedef enum e_event_log_level
{
    EVENT_LOG_LEVEL_INFO = 0, /* "info" */
    EVENT_LOG_LEVEL_WARNING,  /* "warning" */
    EVENT_LOG_LEVEL_CRITICAL, /* "critical" */
    EVENT_LOG_LEVEL_COUNT
} event_log_level_t;

typedef enum e_event_log_alert_code
{
    EVENT_LOG_ALERT_PIN_BRUTEFORCE = 0, /* "pin_bruteforce" */
    EVENT_LOG_ALERT_UNKNOWN_CARD,       /* "unknown_card" */
    EVENT_LOG_ALERT_DOOR_AJAR,          /* "door_ajar" */
    EVENT_LOG_ALERT_TAMPER,             /* "tamper" */
    EVENT_LOG_ALERT_LOW_BATTERY,        /* "low_battery" */
    EVENT_LOG_ALERT_POWER_LOST,         /* "power_lost" */
    EVENT_LOG_ALERT_COUNT
} event_log_alert_code_t;

/* One access attempt. */
typedef struct st_event_log_event
{
    int64_t  ts;                        /* Unix time in seconds; small values mean the clock was not synced yet. */
    uint32_t id;                        /* Increasing, unique on this device, never reused (also across clear). */
    uint8_t  method;                    /* event_log_method_t. */
    uint8_t  result;                    /* event_log_result_t. */
    char     user[EVENT_LOG_USER_SIZE]; /* Empty when the user is unknown. */
} event_log_event_t;

/* One alert. Alert ids are a separate sequence from event ids. */
typedef struct st_event_log_alert
{
    int64_t  ts;
    uint32_t id;
    uint8_t  level;                         /* event_log_level_t. */
    uint8_t  code;                          /* event_log_alert_code_t. */
    char     msg[EVENT_LOG_ALERT_MSG_SIZE]; /* Optional human-readable text, may be empty. */
} event_log_alert_t;

typedef enum e_event_log_notice_type
{
    EVENT_LOG_NOTICE_EVENT = 0, /* A new event was added; see notice.event. */
    EVENT_LOG_NOTICE_ALERT,     /* A new alert was added; see notice.alert. */
    EVENT_LOG_NOTICE_CLEARED,   /* The log was cleared and gen changed. */
} event_log_notice_type_t;

typedef struct st_event_log_notice
{
    event_log_notice_type_t type;
    uint32_t                gen; /* Generation after the change. */
    union
    {
        event_log_event_t event;
        event_log_alert_t alert;
    };
} event_log_notice_t;

/* Called after every change, outside the internal lock, in the task that made the change. Keep it short and
 * non-blocking: the caller is lock logic that must not wait for the network. */
typedef void (*event_log_callback_t)(const event_log_notice_t *const p_notice, void *p_user_ctx);

/* Configuration remains caller-owned and must stay valid and unchanged for the lifetime of the instance. */
typedef struct st_event_log_config
{
    const char          *p_nvs_namespace; /* NULL selects EVENT_LOG_NVS_NAMESPACE. */
    event_log_callback_t p_callback;      /* Optional. */
    void                *p_user_ctx;      /* Passed unchanged to p_callback. */
} event_log_config_t;

/* Persisted state. Bump EVENT_LOG_STORE_VERSION in event_log.c whenever this layout changes. */
typedef struct st_event_log_store
{
    uint32_t          version;
    uint32_t          gen;           /* Changes on every clear, so clients know to drop what they cached. */
    uint32_t          next_event_id; /* Starts at 1. */
    uint32_t          next_alert_id; /* Starts at 1. */
    uint16_t          event_head;    /* Index where the next event is written. */
    uint16_t          event_count;
    uint16_t          alert_head;
    uint16_t          alert_count;
    event_log_event_t events[EVENT_LOG_EVENT_CAPACITY];
    event_log_alert_t alerts[EVENT_LOG_ALERT_CAPACITY];
} event_log_store_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_event_log_instance_ctrl
{
    const event_log_config_t *p_cfg;     /* Caller-owned immutable configuration. */
    SemaphoreHandle_t         mutex;     /* Guards store. */
    StaticSemaphore_t         mutex_buf; /* Storage for mutex. */
    event_log_store_t         store;     /* RAM copy of the persisted log. */
    bool                      persist;   /* False when NVS is unusable; the log then lives in RAM only. */
    bool                      init;      /* True after successful initialization. */
} event_log_instance_ctrl_t;

/***********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Interim event store until storage_manager writes the full log to the SD card. Local lock logic adds entries;
 * remote_service reads them. Full descriptions are beside the implementations in event_log.c. */
#ifdef __cplusplus
extern "C"
{
#endif

    /* Load the log from NVS. nvs_flash_init() must have run; if NVS is unusable the log works from RAM. */
    app_err_t event_log_init(event_log_instance_ctrl_t *const p_ctrl, const event_log_config_t *const p_cfg);

    /* Record an access attempt. p_user may be NULL or empty. p_out, when not NULL, receives the stored entry. */
    app_err_t event_log_add_event(event_log_instance_ctrl_t *const p_ctrl,
                                  event_log_method_t               method,
                                  event_log_result_t               result,
                                  const char *const                p_user,
                                  event_log_event_t *const         p_out);

    /* Record an alert. p_msg may be NULL or empty. p_out, when not NULL, receives the stored entry. */
    app_err_t event_log_add_alert(event_log_instance_ctrl_t *const p_ctrl,
                                  event_log_level_t                level,
                                  event_log_alert_code_t           code,
                                  const char *const                p_msg,
                                  event_log_alert_t *const         p_out);

    /* Drop every event and alert and change gen. Ids keep increasing so they stay unique. */
    app_err_t event_log_clear(event_log_instance_ctrl_t *const p_ctrl);

    /* Copy up to max_count events with id < before_id (EVENT_LOG_NEWEST: no bound), newest first. *p_more tells
     * whether older events remain. */
    app_err_t event_log_get_events(event_log_instance_ctrl_t *const p_ctrl,
                                   uint32_t                         before_id,
                                   event_log_event_t *const         p_out,
                                   size_t                           max_count,
                                   size_t *const                    p_count,
                                   bool *const                      p_more);

    /* Copy the current generation and every kept event and alert, newest first, in one consistent snapshot.
     * p_events must hold EVENT_LOG_EVENT_CAPACITY entries and p_alerts EVENT_LOG_ALERT_CAPACITY entries. */
    app_err_t event_log_get_snapshot(event_log_instance_ctrl_t *const p_ctrl,
                                     uint32_t *const                  p_gen,
                                     event_log_event_t *const         p_events,
                                     size_t *const                    p_event_count,
                                     event_log_alert_t *const         p_alerts,
                                     size_t *const                    p_alert_count);

    /* Wire names from docs/mqtt_protocol.md. Out-of-range values return NULL. */
    const char *event_log_method_name(event_log_method_t method);
    const char *event_log_result_name(event_log_result_t result);
    const char *event_log_level_name(event_log_level_t level);
    const char *event_log_alert_code_name(event_log_alert_code_t code);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_LOG_H_ */
