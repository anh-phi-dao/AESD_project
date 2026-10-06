/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "keypad_task.h"

#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "device_config.h"
#include "mqtt_conn.h"
#include "network_manager.h"
#endif

#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#if USED_FREERTOS
#define SYSTEM_NET_TASK_STACK         6144U
#define SYSTEM_NET_TASK_PRIORITY      4U
#define SYSTEM_NET_WAIT_LOG_MS        15000U
#define SYSTEM_MQTT_TOPIC_PREFIX      "lock"
#define SYSTEM_HOSTNAME_SIZE          33U /* ESP-IDF accepts at most 32: "smartlock-" + 22 id chars. */
#define SYSTEM_HISTORY_REQ_MAX_LEN    256U
#define SYSTEM_HISTORY_REQ_ID_MAX_LEN 16U
#define SYSTEM_HISTORY_RESP_SIZE      96U
#endif

#define SYSTEM_TAG "SYSTEM"

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Initializes the main NVS partition used by the Wi-Fi driver and user data.
 *
 * Erasing is the documented recovery when the partition is full or was written by a newer NVS version. It only
 * touches "nvs"; device credentials live in the separate "devcfg" partition and survive it.
 **********************************************************************************************************************/
static esp_err_t system_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_LOGW(SYSTEM_TAG, "NVS partition is unusable (%s), erasing it", esp_err_to_name(ret));
        ret = nvs_flash_erase();
        if (ret == ESP_OK)
        {
            ret = nvs_flash_init();
        }
    }
    return ret;
}

#if USED_FREERTOS
/* Network state lives for the whole application lifetime; the MQTT and Wi-Fi drivers keep pointers to it. */
static device_config_t                 s_device_cfg = {0};
static network_manager_config_t        s_net_cfg    = {0};
static network_manager_instance_ctrl_t s_net_ctrl   = {0};
static mqtt_conn_config_t              s_mqtt_cfg   = {0};
static mqtt_conn_instance_ctrl_t       s_mqtt_ctrl  = {0};
static char                            s_hostname[SYSTEM_HOSTNAME_SIZE];

/* Inbound topics. There is deliberately no unlock topic: the door opens only on site (docs/mqtt_protocol.md). */
static const mqtt_conn_sub_t s_mqtt_subs[] = {
    {.p_topic_suffix = "history/req", .qos = 1U, .max_payload_len = SYSTEM_HISTORY_REQ_MAX_LEN},
};

/***********************************************************************************************************************
 * TEMPORARY HISTORY RESPONDER: answers every history request with an empty page so the web page works end to end.
 * Move this into app/remote_service once storage_manager provides the event log.
 **********************************************************************************************************************/
static void system_mqtt_rx_callback(const mqtt_conn_rx_msg_t *const p_msg, void *p_user_ctx)
{
    mqtt_conn_instance_ctrl_t *p_mqtt = (mqtt_conn_instance_ctrl_t *)p_user_ctx;

    cJSON *p_req = cJSON_ParseWithLength((const char *)p_msg->p_data, p_msg->data_len);
    if (p_req == NULL)
    {
        return;
    }

    /* req_id is echoed back, so accept only short alphanumeric ids to keep the response well-formed. */
    const cJSON *p_id  = cJSON_GetObjectItemCaseSensitive(p_req, "req_id");
    bool         valid = cJSON_IsString(p_id) && (p_id->valuestring[0] != '\0')
                         && (strlen(p_id->valuestring) <= SYSTEM_HISTORY_REQ_ID_MAX_LEN);
    for (const char *p_c = valid ? p_id->valuestring : ""; valid && (*p_c != '\0'); p_c++)
    {
        valid = ((*p_c >= '0') && (*p_c <= '9')) || ((*p_c >= 'a') && (*p_c <= 'z')) || ((*p_c >= 'A') && (*p_c <= 'Z'))
                || (*p_c == '-') || (*p_c == '_');
    }

    if (valid)
    {
        char      resp[SYSTEM_HISTORY_RESP_SIZE];
        const int len
            = snprintf(resp, sizeof(resp), "{\"req_id\":\"%s\",\"items\":[],\"more\":false}", p_id->valuestring);
        if ((len > 0) && ((size_t)len < sizeof(resp)))
        {
            (void)mqtt_conn_publish(p_mqtt, "history/resp", resp, (size_t)len, 1U, false);
        }
    }
    cJSON_Delete(p_req);
}

/***********************************************************************************************************************
 * Loads the device credentials, brings up Wi-Fi, waits for a valid clock, then starts MQTT. Runs in its own task so
 * the lock keeps working locally while the network is slow or down. Deletes itself once MQTT is started, because
 * both drivers reconnect on their own from then on.
 **********************************************************************************************************************/
static void system_network_task(void *p_arg)
{
    (void)p_arg;

    app_err_t ret = device_config_load(&s_device_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGW(SYSTEM_TAG, "Network disabled: no device credentials (error %d). Lock keeps working offline.", ret);
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
        ESP_LOGE(SYSTEM_TAG, "network_manager_init failed with application error: %d", ret);
        vTaskDelete(NULL);
        return;
    }

    /* TLS checks certificate dates, so MQTT must wait for SNTP as well as for an IP address. */
    while (network_manager_wait_ready(&s_net_ctrl, NETWORK_MANAGER_READY_ALL, pdMS_TO_TICKS(SYSTEM_NET_WAIT_LOG_MS))
           != APP_SUCCESS)
    {
        /* A silent wait is hard to diagnose on site; say which prerequisite is still missing. */
        if (!network_manager_is_connected(&s_net_ctrl))
        {
            ESP_LOGW(SYSTEM_TAG, "Still waiting for Wi-Fi...");
        }
        else
        {
            ESP_LOGW(SYSTEM_TAG, "Still waiting for SNTP time sync; the network may block NTP (UDP port 123)");
        }
    }

    s_mqtt_cfg = (mqtt_conn_config_t) {
        .p_broker_uri   = s_device_cfg.mqtt_uri,
        .p_ca_cert_pem  = s_device_cfg.p_mqtt_ca_pem,
        .p_username     = s_device_cfg.mqtt_username,
        .p_password     = s_device_cfg.mqtt_password,
        .p_topic_prefix = SYSTEM_MQTT_TOPIC_PREFIX,
        .p_device_id    = s_device_cfg.device_id,
        .p_subs         = s_mqtt_subs,
        .sub_count      = (uint8_t)(sizeof(s_mqtt_subs) / sizeof(s_mqtt_subs[0])),
        .p_rx_cb        = system_mqtt_rx_callback,
        .p_user_ctx     = &s_mqtt_ctrl,
    };

    ret = mqtt_conn_init(&s_mqtt_ctrl, &s_mqtt_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(SYSTEM_TAG, "mqtt_conn_init failed with application error: %d", ret);
    }
    vTaskDelete(NULL);
}
#endif

/***********************************************************************************************************************
 * Application entry point for the production firmware.
 **********************************************************************************************************************/
void app_main(void)
{

    esp_err_t nvs_ret = system_nvs_init();
    if (nvs_ret != ESP_OK)
    {
        ESP_LOGE(SYSTEM_TAG, "NVS init failed: %s", esp_err_to_name(nvs_ret));
    }

    boot_pcf7584();
    xTaskCreate(keypad_task, "keypad", 4096, NULL, 1, NULL);
}
