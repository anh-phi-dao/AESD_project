#ifndef REMOTE_SERVICE_JSON_H_
#define REMOTE_SERVICE_JSON_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "remote_service_config.h"
#include "event_log.h"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* A validated history/req. */
typedef struct st_remote_service_history_req
{
    char     req_id[REMOTE_SERVICE_HISTORY_REQ_ID_MAX_LEN + 1U]; /* Echoed back; [A-Za-z0-9_-] only. */
    uint32_t before_id;                                          /* EVENT_LOG_NEWEST when absent. */
    uint8_t  limit;                                              /* 1..REMOTE_SERVICE_HISTORY_LIMIT_MAX. */
} remote_service_history_req_t;

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* JSON encoding of the payloads in docs/mqtt_protocol.md. Kept apart from the task code so it can be unit tested on a
 * PC. Builders return a heap string to release with cJSON_free(), or NULL when out of memory. */
#ifdef __cplusplus
extern "C"
{
#endif

    char *remote_service_json_event(const event_log_event_t *const p_event);
    char *remote_service_json_alert(const event_log_alert_t *const p_alert);

    /* {"gen":..,"events":[..],"alerts":[..]}, both arrays newest first. */
    char *remote_service_json_recent(uint32_t                       gen,
                                     const event_log_event_t *const p_events,
                                     size_t                         event_count,
                                     const event_log_alert_t *const p_alerts,
                                     size_t                         alert_count);

    /* {"req_id":..,"items":[..],"more":..}, items newest first. */
    char *remote_service_json_history(const char *const              p_req_id,
                                      const event_log_event_t *const p_items,
                                      size_t                         item_count,
                                      bool                           more);

    /* Parse and validate a history/req payload. Returns false for anything malformed. */
    bool remote_service_json_parse_history_req(const char *const                   p_json,
                                               size_t                              len,
                                               remote_service_history_req_t *const p_req);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_SERVICE_JSON_H_ */
