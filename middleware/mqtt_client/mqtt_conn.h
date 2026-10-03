#ifndef MQTT_CONN_H_
#define MQTT_CONN_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mqtt_conn_config.h"
#include "app_err.h"
#include "mqtt_client.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Retained payloads of "<prefix>/<device_id>/status". The broker publishes OFFLINE as the Last Will. */
#define MQTT_CONN_STATUS_TOPIC     "status"
#define MQTT_CONN_STATUS_ONLINE    "online"
#define MQTT_CONN_STATUS_OFFLINE   "offline"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

typedef enum e_mqtt_conn_state
{
    MQTT_CONN_STATE_DISCONNECTED = 0, /* Not connected; the client keeps reconnecting in the background. */
    MQTT_CONN_STATE_CONNECTED,        /* Connected, subscribed, and status published as online. */
} mqtt_conn_state_t;

/* One complete inbound message. The payload is only valid during the callback. */
typedef struct st_mqtt_conn_rx_msg
{
    uint8_t        sub_index;      /* Index into mqtt_conn_config_t::p_subs. */
    const char    *p_topic_suffix; /* Matched suffix, e.g. "history/req". */
    const uint8_t *p_data;         /* Payload, always followed by a '\0' so JSON can be parsed in place. */
    size_t         data_len;       /* Payload length without the terminator. */
} mqtt_conn_rx_msg_t;

typedef void (*mqtt_conn_rx_callback_t)(const mqtt_conn_rx_msg_t *const p_msg, void *p_user_ctx);
typedef void (*mqtt_conn_state_callback_t)(mqtt_conn_state_t state, void *p_user_ctx);

/* Inbound topic allowlist entry. Anything not listed here is never subscribed and is dropped if received. */
typedef struct st_mqtt_conn_sub
{
    const char *p_topic_suffix;  /* Relative to "<prefix>/<device_id>/"; wildcards are rejected. */
    uint8_t     qos;             /* Subscription QoS, 0..2. */
    size_t      max_payload_len; /* Larger messages are dropped. Must not exceed MQTT_CONN_RX_PAYLOAD_MAX_LEN. */
} mqtt_conn_sub_t;

/* Configuration remains caller-owned and must stay valid and unchanged until deinit. */
typedef struct st_mqtt_conn_config
{
    const char                *p_broker_uri;   /* "mqtts://host:8883"; use "mqtt://" only for LAN bring-up. */
    const char                *p_ca_cert_pem;  /* NULL: verify with the ESP-IDF certificate bundle (cloud brokers). */
    const char                *p_username;     /* Broker credentials of this device. */
    const char                *p_password;     /* Keep out of Git and sdkconfig; load from NVS or a local header. */
    const char                *p_topic_prefix; /* First topic level, e.g. "lock". */
    const char                *p_device_id;    /* Second topic level, e.g. "door01". Also used in the client id. */
    const mqtt_conn_sub_t     *p_subs;         /* Inbound allowlist, may be NULL when sub_count is 0. */
    uint8_t                    sub_count;      /* Number of p_subs entries, at most MQTT_CONN_MAX_SUBS. */
    uint16_t                   keepalive_s;    /* 0 selects MQTT_CONN_DEFAULT_KEEPALIVE_S. */
    mqtt_conn_rx_callback_t    p_rx_cb;        /* Called from the MQTT task; keep it short and copy what you need. */
    mqtt_conn_state_callback_t p_state_cb;     /* Optional, called from the MQTT task. */
    void                      *p_user_ctx;     /* Context passed unchanged to both callbacks. */
} mqtt_conn_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_mqtt_conn_instance_ctrl
{
    esp_mqtt_client_handle_t  p_client;                               /* esp-mqtt client owned by this instance. */
    const mqtt_conn_config_t *p_cfg;                                  /* Caller-owned immutable configuration. */
    char                      topic_root[MQTT_CONN_TOPIC_MAX_LEN];    /* "<prefix>/<device_id>/". */
    size_t                    topic_root_len;                         /* strlen(topic_root). */
    char                      status_topic[MQTT_CONN_TOPIC_MAX_LEN];  /* Also used as the Last Will topic. */
    char                      client_id[MQTT_CONN_CLIENT_ID_MAX_LEN]; /* "dev-<device_id>". */
    uint8_t                  *p_rx_buf;                               /* Reassembly buffer for fragmented payloads. */
    size_t                    rx_len;                                 /* Bytes collected for the current message. */
    int32_t                   rx_sub_index;                           /* -1 while the current message is dropped. */
    volatile bool             connected;                              /* Written by the MQTT task. */
    bool                      init;                                   /* True after successful initialization. */
} mqtt_conn_instance_ctrl_t;

/***********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Keep p_ctrl and p_cfg alive until deinit. Full descriptions are beside the implementations in mqtt_conn.c.
 *
 * This middleware only moves bytes. It deliberately knows nothing about the lock: the door can never be opened
 * over MQTT because no component that owns this connection may depend on lock_service. */
#ifdef __cplusplus
extern "C"
{
#endif

    /* Create the client and start connecting. Wi-Fi must already be up, and SNTP synced when TLS is used. */
    app_err_t mqtt_conn_init(mqtt_conn_instance_ctrl_t *const p_ctrl, const mqtt_conn_config_t *const p_cfg);

    /* Publish "offline", stop the client, and release resources. Never call it from an MQTT callback. */
    app_err_t mqtt_conn_deinit(mqtt_conn_instance_ctrl_t *const p_ctrl);

    /* Publish to "<prefix>/<device_id>/<p_topic_suffix>". Thread-safe; may block while the network is slow. */
    app_err_t mqtt_conn_publish(mqtt_conn_instance_ctrl_t *const p_ctrl,
                                const char *const                p_topic_suffix,
                                const void *const                p_data,
                                size_t                           data_len,
                                uint8_t                          qos,
                                bool                             retain);

    /* Return true while the broker session is up. */
    bool mqtt_conn_is_connected(const mqtt_conn_instance_ctrl_t *const p_ctrl);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CONN_H_ */
