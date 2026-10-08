/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include <stdio.h>
#include "network_task.h"
#include "device_config.h"
#include "door_sim.h"
#include "event_log.h"
#include "mqtt_conn.h"
#include "network_manager.h"
#include "remote_service.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define NETWORK_TAG              "NETWORK"
#define NETWORK_WAIT_LOG_MS      15000U
#define NETWORK_MQTT_TOPIC_PREFIX "lock"
#define NETWORK_HOSTNAME_SIZE    33U /* ESP-IDF accepts at most 32: "smartlock-" + 22 id chars. */

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* Network state lives for the whole application lifetime; the MQTT and Wi-Fi drivers keep pointers to it. */
static device_config_t                 s_device_cfg = {0};
static network_manager_config_t        s_net_cfg    = {0};
static network_manager_instance_ctrl_t s_net_ctrl   = {0};
static mqtt_conn_config_t              s_mqtt_cfg   = {0};
static mqtt_conn_instance_ctrl_t       s_mqtt_ctrl  = {0};
static char                            s_hostname[NETWORK_HOSTNAME_SIZE];

/* Event log (interim store until storage_manager logs to the SD card) and its MQTT bridge. */
static event_log_instance_ctrl_t      s_event_log_ctrl = {0};
static remote_service_instance_ctrl_t s_remote_ctrl    = {0};
static const remote_service_config_t  s_remote_cfg     = {
         .p_log  = &s_event_log_ctrl,
         .p_mqtt = &s_mqtt_ctrl,
};
static const event_log_config_t s_event_log_cfg = {
    .p_nvs_namespace = NULL,
    .p_callback      = remote_service_log_callback,
    .p_user_ctx      = &s_remote_ctrl,
};

#if CONFIG_DOOR_SIM_ENABLE
static door_sim_instance_ctrl_t s_door_sim_ctrl = {0};
static const door_sim_config_t  s_door_sim_cfg  = {
      .p_log       = &s_event_log_ctrl,
      .button_gpio = (gpio_num_t)CONFIG_DOOR_SIM_BUTTON_GPIO,
      .console     = true,
};
#endif

/* Inbound topics. There is deliberately no unlock topic: the door opens only on site (docs/mqtt_protocol.md). */
static const mqtt_conn_sub_t s_mqtt_subs[] = {
    {.p_topic_suffix  = REMOTE_SERVICE_TOPIC_HISTORY_REQ,
     .qos             = 1U,
     .max_payload_len = REMOTE_SERVICE_HISTORY_REQ_MAX_LEN},
};

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

void boot_event_log(void)
{
    app_err_t ret = remote_service_init(&s_remote_ctrl, &s_remote_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(NETWORK_TAG, "remote_service_init failed with application error: %d", ret);
    }

    ret = event_log_init(&s_event_log_ctrl, &s_event_log_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(NETWORK_TAG, "event_log_init failed with application error: %d", ret);
        return;
    }

#if CONFIG_DOOR_SIM_ENABLE
    ret = door_sim_init(&s_door_sim_ctrl, &s_door_sim_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(NETWORK_TAG, "door_sim_init failed with application error: %d", ret);
    }
#endif
}

void network_task(void *pvParameter)
{
    (void)pvParameter;

    app_err_t ret = device_config_load(&s_device_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGW(NETWORK_TAG, "Network disabled: no device credentials (error %d). Lock keeps working offline.", ret);
        vTaskDelete(NULL);
        return;
    }

    (void)snprintf(s_hostname, sizeof(s_hostname), "smartlock-%.22s", s_device_cfg.device_id);
    s_net_cfg = (network_manager_config_t) {
        .p_ssid     = s_device_cfg.wifi_ssid,
        .p_password = s_device_cfg.wifi_password,
        .p_hostname = s_hostname,
    };

    ret = network_manager_init(&s_net_ctrl, &s_net_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(NETWORK_TAG, "network_manager_init failed with application error: %d", ret);
        vTaskDelete(NULL);
        return;
    }

    /* TLS checks certificate dates, so MQTT must wait for SNTP as well as for an IP address. */
    while (network_manager_wait_ready(&s_net_ctrl, NETWORK_MANAGER_READY_ALL, pdMS_TO_TICKS(NETWORK_WAIT_LOG_MS))
           != APP_SUCCESS)
    {
        /* A silent wait is hard to diagnose on site; say which prerequisite is still missing. */
        if (!network_manager_is_connected(&s_net_ctrl))
        {
            ESP_LOGW(NETWORK_TAG, "Still waiting for Wi-Fi...");
        }
        else
        {
            ESP_LOGW(NETWORK_TAG, "Still waiting for SNTP time sync; the network may block NTP (UDP port 123)");
        }
    }

    s_mqtt_cfg = (mqtt_conn_config_t) {
        .p_broker_uri   = s_device_cfg.mqtt_uri,
        .p_ca_cert_pem  = s_device_cfg.p_mqtt_ca_pem,
        .p_username     = s_device_cfg.mqtt_username,
        .p_password     = s_device_cfg.mqtt_password,
        .p_topic_prefix = NETWORK_MQTT_TOPIC_PREFIX,
        .p_device_id    = s_device_cfg.device_id,
        .p_subs         = s_mqtt_subs,
        .sub_count      = (uint8_t)(sizeof(s_mqtt_subs) / sizeof(s_mqtt_subs[0])),
        .p_rx_cb        = remote_service_mqtt_rx_callback,
        .p_state_cb     = remote_service_mqtt_state_callback,
        .p_user_ctx     = &s_remote_ctrl,
    };

    ret = mqtt_conn_init(&s_mqtt_ctrl, &s_mqtt_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(NETWORK_TAG, "mqtt_conn_init failed with application error: %d", ret);
    }
    vTaskDelete(NULL);
}
