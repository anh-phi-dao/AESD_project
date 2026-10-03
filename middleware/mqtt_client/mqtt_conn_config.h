#ifndef MQTT_CONN_CONFIG_H_
#define MQTT_CONN_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init/deinit ordering from the caller. */
#ifndef MQTT_CONN_CFG_PARAMETER_CHECKING
#define MQTT_CONN_CFG_PARAMETER_CHECKING 1
#endif

#if (MQTT_CONN_CFG_PARAMETER_CHECKING != 0) && (MQTT_CONN_CFG_PARAMETER_CHECKING != 1)
#error "MQTT_CONN_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* Full topic buffer size including the terminator, e.g. "lock/door01/history/resp". */
#ifndef MQTT_CONN_TOPIC_MAX_LEN
#define MQTT_CONN_TOPIC_MAX_LEN 96U
#endif

/* Client identifier buffer size including the terminator. */
#ifndef MQTT_CONN_CLIENT_ID_MAX_LEN
#define MQTT_CONN_CLIENT_ID_MAX_LEN 48U
#endif

/* Maximum number of inbound topics. Remote users are view-only, so the allowlist stays small. */
#ifndef MQTT_CONN_MAX_SUBS
#define MQTT_CONN_MAX_SUBS 4U
#endif

/* Hard ceiling for one inbound payload; each subscription can set a lower limit. */
#ifndef MQTT_CONN_RX_PAYLOAD_MAX_LEN
#define MQTT_CONN_RX_PAYLOAD_MAX_LEN 16384U
#endif

#define MQTT_CONN_QOS_MAX               2U
#define MQTT_CONN_DEFAULT_KEEPALIVE_S   30U

#endif /* MQTT_CONN_CONFIG_H_ */
