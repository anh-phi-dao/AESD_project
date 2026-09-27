/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "mqtt_conn.h"
#include "err_map.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define MQTT_CONN_TAG              "MQTT_CONN"
#define MQTT_CONN_RX_DROP          (-1)
#define MQTT_CONN_PUBLISH_FAILED   (-1)
#define MQTT_CONN_OUTBOX_FULL      (-2)

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static app_err_t mqtt_conn_validate_init_args(mqtt_conn_instance_ctrl_t *const p_ctrl,
                                              const mqtt_conn_config_t *const  p_cfg);
static bool      mqtt_conn_is_valid_suffix(const char *const p_topic_suffix);
static app_err_t mqtt_conn_build_topic(const mqtt_conn_instance_ctrl_t *const p_ctrl,
                                       const char *const                      p_topic_suffix,
                                       char *const                            p_topic,
                                       size_t                                 topic_size);
static size_t    mqtt_conn_rx_buffer_size(const mqtt_conn_config_t *const p_cfg);
static int32_t   mqtt_conn_match_sub(const mqtt_conn_instance_ctrl_t *const p_ctrl,
                                     const char *const                      p_topic,
                                     int                                    topic_len);
static void      mqtt_conn_on_connected(mqtt_conn_instance_ctrl_t *const p_ctrl);
static void      mqtt_conn_on_data(mqtt_conn_instance_ctrl_t *const p_ctrl, const esp_mqtt_event_t *const p_event);
static void      mqtt_conn_event_handler(void *p_handler_args, esp_event_base_t base, int32_t event_id, void *p_data);
static void      mqtt_conn_release(mqtt_conn_instance_ctrl_t *const p_ctrl);

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Creates the esp-mqtt client, registers the Last Will, and starts connecting in the background.
 *
 * Subscriptions and the "online" status are (re)sent on every successful connection, so a broker restart or a Wi-Fi
 * drop needs no action from the caller.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged until deinit.
 *
 * @retval APP_SUCCESS                  The client was created and started.
 * @retval APP_ERR_INVALID_ARGUMENT     A configuration value is invalid or a topic does not fit its buffer.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block is already initialized.
 * @retval APP_ERR_NO_MEMORY            The client or the receive buffer could not be allocated.
 * @return                              A mapped system error can also be returned.
 **********************************************************************************************************************/
app_err_t mqtt_conn_init(mqtt_conn_instance_ctrl_t *const p_ctrl, const mqtt_conn_config_t *const p_cfg)
{
    app_err_t app_ret = mqtt_conn_validate_init_args(p_ctrl, p_cfg);
    if (app_ret != APP_SUCCESS)
    {
        return app_ret;
    }

    int written
        = snprintf(p_ctrl->topic_root, sizeof(p_ctrl->topic_root), "%s/%s/", p_cfg->p_topic_prefix, p_cfg->p_device_id);
    APP_ERROR_RETURN((written > 0) && ((size_t)written < sizeof(p_ctrl->topic_root)), APP_ERR_INVALID_ARGUMENT);
    p_ctrl->topic_root_len = (size_t)written;

    written = snprintf(p_ctrl->client_id, sizeof(p_ctrl->client_id), "dev-%s", p_cfg->p_device_id);
    APP_ERROR_RETURN((written > 0) && ((size_t)written < sizeof(p_ctrl->client_id)), APP_ERR_INVALID_ARGUMENT);

    p_ctrl->p_cfg = p_cfg;
    app_ret = mqtt_conn_build_topic(p_ctrl, MQTT_CONN_STATUS_TOPIC, p_ctrl->status_topic, sizeof(p_ctrl->status_topic));
    if (app_ret != APP_SUCCESS)
    {
        p_ctrl->p_cfg = NULL;
        return app_ret;
    }

    /* Voice chunks can be several kilobytes, so prefer PSRAM for the reassembly buffer when it exists. */
    const size_t rx_size = mqtt_conn_rx_buffer_size(p_cfg);
    if (rx_size > 0U)
    {
        p_ctrl->p_rx_buf = heap_caps_malloc_prefer(rx_size + 1U, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
        if (p_ctrl->p_rx_buf == NULL)
        {
            p_ctrl->p_cfg = NULL;
            return APP_ERR_NO_MEMORY;
        }
    }
    p_ctrl->rx_sub_index = MQTT_CONN_RX_DROP;

    const esp_mqtt_client_config_t client_cfg = {
        .broker = {
            .address.uri  = p_cfg->p_broker_uri,
            .verification = {
                .certificate       = p_cfg->p_ca_cert_pem,
                .crt_bundle_attach = (p_cfg->p_ca_cert_pem == NULL) ? esp_crt_bundle_attach : NULL,
            },
        },
        .credentials = {
            .username                = p_cfg->p_username,
            .client_id               = p_ctrl->client_id,
            .authentication.password = p_cfg->p_password,
        },
        .session = {
            .last_will = {
                .topic  = p_ctrl->status_topic,
                .msg    = MQTT_CONN_STATUS_OFFLINE,
                .qos    = 1,
                .retain = 1,
            },
            .keepalive = (p_cfg->keepalive_s != 0U) ? p_cfg->keepalive_s : MQTT_CONN_DEFAULT_KEEPALIVE_S,
        },
    };

    esp_err_t ret    = ESP_OK;
    p_ctrl->p_client = esp_mqtt_client_init(&client_cfg);
    if (p_ctrl->p_client == NULL)
    {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    ret = esp_mqtt_client_register_event(p_ctrl->p_client, MQTT_EVENT_ANY, mqtt_conn_event_handler, p_ctrl);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    /* The MQTT task may deliver events as soon as the client starts, so mark the instance ready first. */
    p_ctrl->init = true;
    ret          = esp_mqtt_client_start(p_ctrl->p_client);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ESP_LOGI(MQTT_CONN_TAG, "Connecting to %s as %s", p_cfg->p_broker_uri, p_ctrl->client_id);
    return APP_SUCCESS;

fail:
    mqtt_conn_release(p_ctrl);
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * Publishes the offline status, stops the client, and releases resources owned by this instance.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 *
 * @retval APP_SUCCESS                  De-initialization succeeded.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t mqtt_conn_deinit(mqtt_conn_instance_ctrl_t *const p_ctrl)
{
#if MQTT_CONN_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    /* A clean disconnect does not trigger the Last Will, so report the offline state explicitly. */
    if (p_ctrl->connected)
    {
        (void)esp_mqtt_client_publish(p_ctrl->p_client,
                                      p_ctrl->status_topic,
                                      MQTT_CONN_STATUS_OFFLINE,
                                      (int)strlen(MQTT_CONN_STATUS_OFFLINE),
                                      1,
                                      1);
    }

    mqtt_conn_release(p_ctrl);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Publishes one message below this device's topic root.
 *
 * QoS 1/2 messages are queued by esp-mqtt while disconnected; QoS 0 messages fail instead. Payloads larger than the
 * esp-mqtt buffer are fragmented by the client, which can block the caller for several seconds.
 *
 * @param[in,out] p_ctrl          Pointer to the runtime control block.
 * @param[in]     p_topic_suffix  Topic below "<prefix>/<device_id>/", without wildcards.
 * @param[in]     p_data          Payload bytes.
 * @param[in]     data_len        Payload length in bytes; must be greater than zero.
 * @param[in]     qos             QoS 0..2.
 * @param[in]     retain          Retain flag. Never retain one-shot events.
 *
 * @retval APP_SUCCESS                  The message was sent or queued.
 * @retval APP_ERR_INVALID_ARGUMENT     The topic, length, or QoS is invalid.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 * @retval APP_ERR_NO_MEMORY            The esp-mqtt outbox is full.
 * @retval APP_FAIL                     esp-mqtt rejected the message, e.g. QoS 0 while disconnected.
 **********************************************************************************************************************/
app_err_t mqtt_conn_publish(mqtt_conn_instance_ctrl_t *const p_ctrl,
                            const char *const                p_topic_suffix,
                            const void *const                p_data,
                            size_t                           data_len,
                            uint8_t                          qos,
                            bool                             retain)
{
#if MQTT_CONN_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_topic_suffix);
    APP_ASSERT(NULL != p_data);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    /* esp-mqtt treats a zero length as "use strlen", which is wrong for binary payloads. */
    APP_ERROR_RETURN((data_len > 0U) && (data_len <= (size_t)INT32_MAX), APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(qos <= MQTT_CONN_QOS_MAX, APP_ERR_INVALID_ARGUMENT);
#endif

    char      topic[MQTT_CONN_TOPIC_MAX_LEN];
    app_err_t app_ret = mqtt_conn_build_topic(p_ctrl, p_topic_suffix, topic, sizeof(topic));
    if (app_ret != APP_SUCCESS)
    {
        return app_ret;
    }

    const int msg_id
        = esp_mqtt_client_publish(p_ctrl->p_client, topic, (const char *)p_data, (int)data_len, qos, retain ? 1 : 0);
    if (msg_id == MQTT_CONN_OUTBOX_FULL)
    {
        return APP_ERR_NO_MEMORY;
    }
    return (msg_id == MQTT_CONN_PUBLISH_FAILED) ? APP_FAIL : APP_SUCCESS;
}

/***********************************************************************************************************************
 * Reports whether the broker session is currently up.
 *
 * @param[in] p_ctrl  Pointer to the runtime control block.
 *
 * @return  True when initialized and connected.
 **********************************************************************************************************************/
bool mqtt_conn_is_connected(const mqtt_conn_instance_ctrl_t *const p_ctrl)
{
    return (p_ctrl != NULL) && p_ctrl->init && p_ctrl->connected;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Validates the initialization arguments and the inbound topic allowlist.
 **********************************************************************************************************************/
static app_err_t mqtt_conn_validate_init_args(mqtt_conn_instance_ctrl_t *const p_ctrl,
                                              const mqtt_conn_config_t *const  p_cfg)
{
#if MQTT_CONN_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);
    APP_ASSERT(NULL != p_cfg->p_broker_uri);
    APP_ASSERT(NULL != p_cfg->p_topic_prefix);
    APP_ASSERT(NULL != p_cfg->p_device_id);
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(mqtt_conn_is_valid_suffix(p_cfg->p_topic_prefix), APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(mqtt_conn_is_valid_suffix(p_cfg->p_device_id), APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(strchr(p_cfg->p_device_id, '/') == NULL, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(p_cfg->sub_count <= MQTT_CONN_MAX_SUBS, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((p_cfg->sub_count == 0U) || (p_cfg->p_subs != NULL), APP_ERR_INVALID_POINTER);
    APP_ERROR_RETURN((p_cfg->sub_count == 0U) || (p_cfg->p_rx_cb != NULL), APP_ERR_INVALID_POINTER);

    for (uint8_t i = 0U; i < p_cfg->sub_count; i++)
    {
        const mqtt_conn_sub_t *p_sub = &p_cfg->p_subs[i];
        APP_ASSERT(NULL != p_sub->p_topic_suffix);
        APP_ERROR_RETURN(mqtt_conn_is_valid_suffix(p_sub->p_topic_suffix), APP_ERR_INVALID_ARGUMENT);
        APP_ERROR_RETURN(p_sub->qos <= MQTT_CONN_QOS_MAX, APP_ERR_INVALID_ARGUMENT);
        APP_ERROR_RETURN((p_sub->max_payload_len > 0U) && (p_sub->max_payload_len <= MQTT_CONN_RX_PAYLOAD_MAX_LEN),
                         APP_ERR_INVALID_ARGUMENT);
        /* The device publishes its own status; letting anyone else write it would fake online/offline. */
        APP_ERROR_RETURN(strcmp(p_sub->p_topic_suffix, MQTT_CONN_STATUS_TOPIC) != 0, APP_ERR_INVALID_ARGUMENT);
    }

    if (strncmp(p_cfg->p_broker_uri, "mqtts://", 8) != 0)
    {
        ESP_LOGW(MQTT_CONN_TAG, "Broker URI is not mqtts://; traffic and credentials are sent in plain text");
    }
#else
    (void)p_ctrl;
    (void)p_cfg;
#endif

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Accepts non-empty topic levels without wildcards, leading or trailing '/', or empty levels.
 **********************************************************************************************************************/
static bool mqtt_conn_is_valid_suffix(const char *const p_topic_suffix)
{
    const size_t len = strlen(p_topic_suffix);
    if ((len == 0U) || (len >= MQTT_CONN_TOPIC_MAX_LEN))
    {
        return false;
    }
    if ((p_topic_suffix[0] == '/') || (p_topic_suffix[len - 1U] == '/') || (strstr(p_topic_suffix, "//") != NULL))
    {
        return false;
    }
    return strpbrk(p_topic_suffix, "#+") == NULL;
}

/***********************************************************************************************************************
 * Builds "<prefix>/<device_id>/<suffix>" into p_topic.
 **********************************************************************************************************************/
static app_err_t mqtt_conn_build_topic(const mqtt_conn_instance_ctrl_t *const p_ctrl,
                                       const char *const                      p_topic_suffix,
                                       char *const                            p_topic,
                                       size_t                                 topic_size)
{
    APP_ERROR_RETURN(mqtt_conn_is_valid_suffix(p_topic_suffix), APP_ERR_INVALID_ARGUMENT);

    const int written = snprintf(p_topic, topic_size, "%s%s", p_ctrl->topic_root, p_topic_suffix);
    APP_ERROR_RETURN((written > 0) && ((size_t)written < topic_size), APP_ERR_INVALID_ARGUMENT);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Returns the largest max_payload_len of the allowlist, i.e. the reassembly buffer size without the terminator.
 **********************************************************************************************************************/
static size_t mqtt_conn_rx_buffer_size(const mqtt_conn_config_t *const p_cfg)
{
    size_t size = 0U;
    for (uint8_t i = 0U; i < p_cfg->sub_count; i++)
    {
        if (p_cfg->p_subs[i].max_payload_len > size)
        {
            size = p_cfg->p_subs[i].max_payload_len;
        }
    }
    return size;
}

/***********************************************************************************************************************
 * Returns the allowlist index that exactly matches the received topic, or MQTT_CONN_RX_DROP.
 **********************************************************************************************************************/
static int32_t mqtt_conn_match_sub(const mqtt_conn_instance_ctrl_t *const p_ctrl,
                                   const char *const                      p_topic,
                                   int                                    topic_len)
{
    if ((p_topic == NULL) || (topic_len <= (int)p_ctrl->topic_root_len)
        || (strncmp(p_topic, p_ctrl->topic_root, p_ctrl->topic_root_len) != 0))
    {
        return MQTT_CONN_RX_DROP;
    }

    /* esp-mqtt topics are not NUL-terminated, so compare with explicit lengths. */
    const char  *p_suffix   = p_topic + p_ctrl->topic_root_len;
    const size_t suffix_len = (size_t)topic_len - p_ctrl->topic_root_len;
    for (uint8_t i = 0U; i < p_ctrl->p_cfg->sub_count; i++)
    {
        const char *p_allowed = p_ctrl->p_cfg->p_subs[i].p_topic_suffix;
        if ((strlen(p_allowed) == suffix_len) && (memcmp(p_allowed, p_suffix, suffix_len) == 0))
        {
            return (int32_t)i;
        }
    }
    return MQTT_CONN_RX_DROP;
}

/***********************************************************************************************************************
 * Re-subscribes the allowlist and publishes the retained online status after each (re)connection.
 **********************************************************************************************************************/
static void mqtt_conn_on_connected(mqtt_conn_instance_ctrl_t *const p_ctrl)
{
    char topic[MQTT_CONN_TOPIC_MAX_LEN];

    for (uint8_t i = 0U; i < p_ctrl->p_cfg->sub_count; i++)
    {
        const mqtt_conn_sub_t *p_sub = &p_ctrl->p_cfg->p_subs[i];
        if ((mqtt_conn_build_topic(p_ctrl, p_sub->p_topic_suffix, topic, sizeof(topic)) != APP_SUCCESS)
            || (esp_mqtt_client_subscribe(p_ctrl->p_client, topic, p_sub->qos) < 0))
        {
            ESP_LOGE(MQTT_CONN_TAG, "Subscribe failed: %s", p_sub->p_topic_suffix);
        }
    }

    (void)esp_mqtt_client_publish(
        p_ctrl->p_client, p_ctrl->status_topic, MQTT_CONN_STATUS_ONLINE, (int)strlen(MQTT_CONN_STATUS_ONLINE), 1, 1);
}

/***********************************************************************************************************************
 * Reassembles one inbound message and hands it to the caller when complete.
 *
 * esp-mqtt splits payloads larger than its buffer into several MQTT_EVENT_DATA events. Only the first one carries the
 * topic, so the match result is kept in rx_sub_index until the last fragment arrives.
 **********************************************************************************************************************/
static void mqtt_conn_on_data(mqtt_conn_instance_ctrl_t *const p_ctrl, const esp_mqtt_event_t *const p_event)
{
    if (p_event->current_data_offset == 0)
    {
        p_ctrl->rx_len       = 0U;
        p_ctrl->rx_sub_index = mqtt_conn_match_sub(p_ctrl, p_event->topic, p_event->topic_len);

        if (p_ctrl->rx_sub_index == MQTT_CONN_RX_DROP)
        {
            ESP_LOGW(MQTT_CONN_TAG, "Dropped message on a topic outside the allowlist");
            return;
        }
        /* A retained request would be replayed on every reconnect, so inbound retained messages are refused. */
        if (p_event->retain)
        {
            ESP_LOGW(MQTT_CONN_TAG, "Dropped retained message");
            p_ctrl->rx_sub_index = MQTT_CONN_RX_DROP;
            return;
        }
        if ((p_event->total_data_len <= 0)
            || ((size_t)p_event->total_data_len > p_ctrl->p_cfg->p_subs[p_ctrl->rx_sub_index].max_payload_len))
        {
            ESP_LOGW(MQTT_CONN_TAG, "Dropped message of %d bytes", p_event->total_data_len);
            p_ctrl->rx_sub_index = MQTT_CONN_RX_DROP;
            return;
        }
    }

    if (p_ctrl->rx_sub_index == MQTT_CONN_RX_DROP)
    {
        return;
    }

    /* A gap means a fragment was lost; drop the message rather than deliver corrupted data. */
    if (((size_t)p_event->current_data_offset != p_ctrl->rx_len) || (p_event->data_len < 0)
        || ((p_ctrl->rx_len + (size_t)p_event->data_len) > (size_t)p_event->total_data_len))
    {
        p_ctrl->rx_sub_index = MQTT_CONN_RX_DROP;
        return;
    }

    memcpy(&p_ctrl->p_rx_buf[p_ctrl->rx_len], p_event->data, (size_t)p_event->data_len);
    p_ctrl->rx_len += (size_t)p_event->data_len;

    if (p_ctrl->rx_len == (size_t)p_event->total_data_len)
    {
        const uint8_t            index = (uint8_t)p_ctrl->rx_sub_index;
        const mqtt_conn_rx_msg_t msg   = {
              .sub_index      = index,
              .p_topic_suffix = p_ctrl->p_cfg->p_subs[index].p_topic_suffix,
              .p_data         = p_ctrl->p_rx_buf,
              .data_len       = p_ctrl->rx_len,
        };

        p_ctrl->p_rx_buf[p_ctrl->rx_len] = '\0';
        p_ctrl->rx_sub_index             = MQTT_CONN_RX_DROP;
        p_ctrl->p_cfg->p_rx_cb(&msg, p_ctrl->p_cfg->p_user_ctx);
    }
}

/***********************************************************************************************************************
 * Dispatches esp-mqtt events. Runs in the esp-mqtt task context.
 **********************************************************************************************************************/
static void mqtt_conn_event_handler(void *p_handler_args, esp_event_base_t base, int32_t event_id, void *p_data)
{
    (void)base;
    mqtt_conn_instance_ctrl_t *p_ctrl  = (mqtt_conn_instance_ctrl_t *)p_handler_args;
    esp_mqtt_event_handle_t    p_event = (esp_mqtt_event_handle_t)p_data;

    if ((p_ctrl == NULL) || (!p_ctrl->init))
    {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_CONN_TAG, "Connected");
            mqtt_conn_on_connected(p_ctrl);
            p_ctrl->connected = true;
            if (p_ctrl->p_cfg->p_state_cb != NULL)
            {
                p_ctrl->p_cfg->p_state_cb(MQTT_CONN_STATE_CONNECTED, p_ctrl->p_cfg->p_user_ctx);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(MQTT_CONN_TAG, "Disconnected, retrying in the background");
            p_ctrl->connected    = false;
            p_ctrl->rx_sub_index = MQTT_CONN_RX_DROP;
            if (p_ctrl->p_cfg->p_state_cb != NULL)
            {
                p_ctrl->p_cfg->p_state_cb(MQTT_CONN_STATE_DISCONNECTED, p_ctrl->p_cfg->p_user_ctx);
            }
            break;

        case MQTT_EVENT_DATA:
            mqtt_conn_on_data(p_ctrl, p_event);
            break;

        case MQTT_EVENT_ERROR:
            if ((p_event->error_handle != NULL)
                && (p_event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED))
            {
                /* Usually wrong credentials or an ACL that rejects this client id. */
                ESP_LOGE(MQTT_CONN_TAG,
                         "Broker refused the connection (code %d)",
                         p_event->error_handle->connect_return_code);
            }
            else
            {
                ESP_LOGE(MQTT_CONN_TAG, "Transport error");
            }
            break;

        default:
            break;
    }
}

/***********************************************************************************************************************
 * Stops the client and releases every resource acquired by the instance.
 **********************************************************************************************************************/
static void mqtt_conn_release(mqtt_conn_instance_ctrl_t *const p_ctrl)
{
    if (p_ctrl->p_client != NULL)
    {
        /* Stop before destroy so no event handler runs against a half-released control block. */
        (void)esp_mqtt_client_stop(p_ctrl->p_client);
        (void)esp_mqtt_client_destroy(p_ctrl->p_client);
        p_ctrl->p_client = NULL;
    }
    if (p_ctrl->p_rx_buf != NULL)
    {
        heap_caps_free(p_ctrl->p_rx_buf);
        p_ctrl->p_rx_buf = NULL;
    }
    p_ctrl->connected    = false;
    p_ctrl->rx_len       = 0U;
    p_ctrl->rx_sub_index = MQTT_CONN_RX_DROP;
    p_ctrl->p_cfg        = NULL;
    p_ctrl->init         = false;
}
