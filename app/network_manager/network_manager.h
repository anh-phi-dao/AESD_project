#ifndef NETWORK_MANAGER_H_
#define NETWORK_MANAGER_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include "network_manager_config.h"
#include "app_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Bits for network_manager_wait_ready(). */
#define NETWORK_MANAGER_READY_IP     (1U << 0) /* Station has an IPv4 address. */
#define NETWORK_MANAGER_READY_TIME   (1U << 1) /* Wall clock is valid (SNTP synced or kept across a soft reset). */
#define NETWORK_MANAGER_READY_ALL    (NETWORK_MANAGER_READY_IP | NETWORK_MANAGER_READY_TIME)

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

typedef enum e_network_manager_event
{
    NETWORK_MANAGER_EVENT_CONNECTED = 0, /* Got an IP address. */
    NETWORK_MANAGER_EVENT_DISCONNECTED,  /* Lost the IP address; reconnecting in the background. */
    NETWORK_MANAGER_EVENT_TIME_SYNCED,   /* SNTP updated the wall clock. Repeats on every periodic sync. */
} network_manager_event_t;

/* Called from the default event loop task or the lwIP task. Keep it short and never block. */
typedef void (*network_manager_callback_t)(network_manager_event_t event, void *p_user_ctx);

/* Configuration remains caller-owned and must stay valid and unchanged until deinit. */
typedef struct st_network_manager_config
{
    const char                *p_ssid;           /* 1..32 bytes. */
    const char                *p_password;       /* 8..64 bytes. Keep out of Git and sdkconfig. */
    wifi_auth_mode_t           min_auth_mode;    /* WPA2_PSK, WPA2_WPA3_PSK or WPA3_PSK; OPEN (0) selects WPA2_PSK. */
    const char                *p_hostname;       /* Optional DHCP hostname, NULL keeps the ESP-IDF default. */
    const char                *p_ntp_server;     /* NULL selects NETWORK_MANAGER_DEFAULT_NTP_SERVER. */
    const char                *p_timezone;       /* POSIX TZ string, NULL selects NETWORK_MANAGER_DEFAULT_TIMEZONE. */
    uint32_t                   reconnect_min_ms; /* 0 selects NETWORK_MANAGER_DEFAULT_RECONNECT_MIN_MS. */
    uint32_t                   reconnect_max_ms; /* 0 selects NETWORK_MANAGER_DEFAULT_RECONNECT_MAX_MS. */
    network_manager_callback_t p_callback;       /* Optional. */
    void                      *p_user_ctx;       /* Context passed unchanged to p_callback. */
} network_manager_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_network_manager_instance_ctrl
{
    const network_manager_config_t *p_cfg;              /* Caller-owned immutable configuration. */
    esp_netif_t                    *p_netif;            /* Default Wi-Fi station interface. */
    EventGroupHandle_t              p_ready_events;     /* NETWORK_MANAGER_READY_* bits. */
    esp_timer_handle_t              p_reconnect_timer;  /* One-shot timer that retries esp_wifi_connect(). */
    esp_event_handler_instance_t    wifi_handler;       /* WIFI_EVENT registration. */
    esp_event_handler_instance_t    ip_handler;         /* IP_EVENT_STA_GOT_IP registration. */
    uint32_t                        reconnect_delay_ms; /* Delay before the next retry. */
    bool                            wifi_initialized;   /* True after esp_wifi_init(). */
    bool                            wifi_started;       /* True after esp_wifi_start(). */
    bool                            sntp_initialized;   /* True after esp_netif_sntp_init(). */
    bool                            sntp_started;       /* SNTP starts on the first IP and keeps running. */
    bool                            init;               /* True after successful initialization. */
} network_manager_instance_ctrl_t;

/***********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Keep p_ctrl and p_cfg alive until deinit. Full descriptions are beside the implementations in network_manager.c.
 *
 * Only one instance can be active because the Wi-Fi driver and SNTP client are global. The caller must initialize
 * NVS (nvs_flash_init) first; this module never erases NVS because it may hold user credentials.
 *
 * Typical use:
 *     network_manager_init(&net_ctrl, &net_cfg);
 *     if (network_manager_wait_ready(&net_ctrl, NETWORK_MANAGER_READY_ALL, portMAX_DELAY) == APP_SUCCESS)
 *     {
 *         mqtt_conn_init(&mqtt_ctrl, &mqtt_cfg);  // TLS needs a valid clock to check certificates
 *     }
 */
#ifdef __cplusplus
extern "C"
{
#endif

    /* Start the Wi-Fi station and keep it connected in the background. Returns without waiting for an IP. */
    app_err_t network_manager_init(network_manager_instance_ctrl_t *const p_ctrl,
                                   const network_manager_config_t *const  p_cfg);

    /* Stop Wi-Fi and SNTP and release resources. The shared netif layer and default event loop are left running. */
    app_err_t network_manager_deinit(network_manager_instance_ctrl_t *const p_ctrl);

    /* Block until every bit in ready_bits is set. Returns APP_ERR_TIMEOUT otherwise. */
    app_err_t network_manager_wait_ready(network_manager_instance_ctrl_t *const p_ctrl,
                                         uint32_t                               ready_bits,
                                         TickType_t                             wait_ticks);

    /* Non-blocking status queries. */
    bool      network_manager_is_connected(const network_manager_instance_ctrl_t *const p_ctrl);
    bool      network_manager_is_time_valid(const network_manager_instance_ctrl_t *const p_ctrl);
    app_err_t network_manager_get_rssi(const network_manager_instance_ctrl_t *const p_ctrl, int8_t *const p_rssi);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_MANAGER_H_ */
