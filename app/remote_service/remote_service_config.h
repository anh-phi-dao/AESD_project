#ifndef REMOTE_SERVICE_CONFIG_H_
#define REMOTE_SERVICE_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init ordering from the caller. */
#ifndef REMOTE_SERVICE_CFG_PARAMETER_CHECKING
#define REMOTE_SERVICE_CFG_PARAMETER_CHECKING 1
#endif

#if (REMOTE_SERVICE_CFG_PARAMETER_CHECKING != 0) && (REMOTE_SERVICE_CFG_PARAMETER_CHECKING != 1)
#error "REMOTE_SERVICE_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* The publisher task builds JSON with cJSON and may block inside esp-mqtt while the network is slow. */
#ifndef REMOTE_SERVICE_TASK_STACK
#define REMOTE_SERVICE_TASK_STACK 6144U
#endif

#ifndef REMOTE_SERVICE_TASK_PRIORITY
#define REMOTE_SERVICE_TASK_PRIORITY 4U
#endif

/* Pending publish jobs. When full, new live events are dropped but still reach the web via "recent". */
#ifndef REMOTE_SERVICE_QUEUE_LEN
#define REMOTE_SERVICE_QUEUE_LEN 16U
#endif

/* history/req limits, see docs/mqtt_protocol.md. */
#define REMOTE_SERVICE_HISTORY_REQ_MAX_LEN    256U
#define REMOTE_SERVICE_HISTORY_REQ_ID_MAX_LEN 16U
#define REMOTE_SERVICE_HISTORY_LIMIT_DEFAULT  20U
#define REMOTE_SERVICE_HISTORY_LIMIT_MAX      50U

/* Requests above this rate are ignored, so a misbehaving client cannot keep the device busy. Several family members
 * opening the page at the same time stay well below it. */
#ifndef REMOTE_SERVICE_HISTORY_MAX_PER_S
#define REMOTE_SERVICE_HISTORY_MAX_PER_S 4U
#endif

/* Topic suffixes below "<prefix>/<device_id>/". */
#define REMOTE_SERVICE_TOPIC_EVENT        "event"
#define REMOTE_SERVICE_TOPIC_ALERT        "alert"
#define REMOTE_SERVICE_TOPIC_RECENT       "recent"
#define REMOTE_SERVICE_TOPIC_HISTORY_REQ  "history/req"
#define REMOTE_SERVICE_TOPIC_HISTORY_RESP "history/resp"

#endif /* REMOTE_SERVICE_CONFIG_H_ */
