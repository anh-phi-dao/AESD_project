/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "network_manager.h"
#include "err_map.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define NETWORK_MANAGER_TAG         "NET_MGR"
#define NETWORK_MANAGER_US_PER_MS   1000ULL

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static app_err_t   network_manager_validate_init_args(network_manager_instance_ctrl_t *const p_ctrl,
                                                      const network_manager_config_t *const  p_cfg);
static bool        network_manager_is_allowed_auth(wifi_auth_mode_t auth_mode);
static esp_err_t   network_manager_setup_netif(network_manager_instance_ctrl_t *const p_ctrl);
static esp_err_t   network_manager_setup_wifi(network_manager_instance_ctrl_t *const p_ctrl);
static esp_err_t   network_manager_setup_sntp(network_manager_instance_ctrl_t *const p_ctrl);
static void        network_manager_schedule_reconnect(network_manager_instance_ctrl_t *const p_ctrl);
static const char *network_manager_reason_hint(uint8_t reason);
static void        network_manager_notify(network_manager_instance_ctrl_t *const p_ctrl, network_manager_event_t event);
static void        network_manager_wifi_event_handler(void            *p_handler_args,
                                                      esp_event_base_t base,
                                                      int32_t          event_id,
                                                      void            *p_data);
static void        network_manager_ip_event_handler(void            *p_handler_args,
                                                    esp_event_base_t base,
                                                    int32_t          event_id,
                                                    void            *p_data);
static void        network_manager_reconnect_timer_cb(void *p_arg);
static void        network_manager_sntp_sync_cb(struct timeval *p_tv);
static void        network_manager_release(network_manager_instance_ctrl_t *const p_ctrl);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* The SNTP callback has no user context, and Wi-Fi is a single global driver, so one instance is active at a time. */
static network_manager_instance_ctrl_t *volatile s_p_active_ctrl = NULL;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Starts the Wi-Fi station and SNTP client. Connection happens in the background; use network_manager_wait_ready() to
 * block until an IP address and a valid clock are available.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged until deinit.
 *
 * @retval APP_SUCCESS                  The station was started.
 * @retval APP_ERR_INVALID_ARGUMENT     SSID, password, or security setting is invalid.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        This control block or another instance is already initialized.
 * @retval APP_ERR_NO_MEMORY            A driver resource could not be allocated.
 * @return                              A mapped system error can also be returned, e.g. when NVS is not initialized.
 **********************************************************************************************************************/
app_err_t network_manager_init(network_manager_instance_ctrl_t *const p_ctrl,
                               const network_manager_config_t *const  p_cfg)
{
    app_err_t app_ret = network_manager_validate_init_args(p_ctrl, p_cfg);
    if (app_ret != APP_SUCCESS)
    {
        return app_ret;
    }
    APP_ERROR_RETURN(NULL == s_p_active_ctrl, APP_ERR_INVALID_STATE);

    p_ctrl->p_cfg              = p_cfg;
    p_ctrl->reconnect_delay_ms = (p_cfg->reconnect_min_ms != 0U) ? p_cfg->reconnect_min_ms
                                                                 : NETWORK_MANAGER_DEFAULT_RECONNECT_MIN_MS;

    /* Log timestamps and event history use local time; certificates and MQTT payloads use UTC epoch seconds. */
    (void)setenv("TZ", (p_cfg->p_timezone != NULL) ? p_cfg->p_timezone : NETWORK_MANAGER_DEFAULT_TIMEZONE, 1);
    tzset();

    esp_err_t ret          = ESP_OK;
    p_ctrl->p_ready_events = xEventGroupCreate();
    if (p_ctrl->p_ready_events == NULL)
    {
        p_ctrl->p_cfg = NULL;
        return APP_ERR_NO_MEMORY;
    }

    /* The RTC keeps counting across a soft reset, so TLS can start before the first SNTP reply. */
    if (time(NULL) >= NETWORK_MANAGER_MIN_VALID_EPOCH)
    {
        (void)xEventGroupSetBits(p_ctrl->p_ready_events, NETWORK_MANAGER_READY_TIME);
    }

    ret = network_manager_setup_netif(p_ctrl);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ret = network_manager_setup_wifi(p_ctrl);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ret = network_manager_setup_sntp(p_ctrl);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    /* Event handlers may run as soon as Wi-Fi starts, so mark the instance ready first. */
    s_p_active_ctrl = p_ctrl;
    p_ctrl->init    = true;

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        goto fail;
    }
    p_ctrl->wifi_started = true;

    ESP_LOGI(NETWORK_MANAGER_TAG, "Connecting to \"%s\"", p_cfg->p_ssid);
    return APP_SUCCESS;

fail:
    ESP_LOGE(NETWORK_MANAGER_TAG, "Initialization failed: %s", esp_err_to_name(ret));
    network_manager_release(p_ctrl);
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * Stops Wi-Fi and SNTP and releases resources owned by this instance.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 *
 * @retval APP_SUCCESS                  De-initialization succeeded.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 **********************************************************************************************************************/
app_err_t network_manager_deinit(network_manager_instance_ctrl_t *const p_ctrl)
{
#if NETWORK_MANAGER_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    network_manager_release(p_ctrl);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Blocks until every requested readiness bit is set.
 *
 * @param[in,out] p_ctrl      Pointer to the runtime control block.
 * @param[in]     ready_bits  Combination of NETWORK_MANAGER_READY_* bits.
 * @param[in]     wait_ticks  Maximum FreeRTOS ticks to wait; portMAX_DELAY waits forever.
 *
 * @retval APP_SUCCESS                  Every requested bit is set.
 * @retval APP_ERR_INVALID_ARGUMENT     ready_bits is empty or contains unknown bits.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 * @retval APP_ERR_TIMEOUT              The bits were not all set before the timeout.
 **********************************************************************************************************************/
app_err_t network_manager_wait_ready(network_manager_instance_ctrl_t *const p_ctrl,
                                     uint32_t                               ready_bits,
                                     TickType_t                             wait_ticks)
{
#if NETWORK_MANAGER_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN((ready_bits != 0U) && ((ready_bits & ~NETWORK_MANAGER_READY_ALL) == 0U), APP_ERR_INVALID_ARGUMENT);
#endif

    const EventBits_t bits
        = xEventGroupWaitBits(p_ctrl->p_ready_events, (EventBits_t)ready_bits, pdFALSE, pdTRUE, wait_ticks);
    return ((bits & ready_bits) == ready_bits) ? APP_SUCCESS : APP_ERR_TIMEOUT;
}

/***********************************************************************************************************************
 * Reports whether the station currently has an IP address.
 *
 * @param[in] p_ctrl  Pointer to the runtime control block.
 *
 * @return  True when initialized and connected.
 **********************************************************************************************************************/
bool network_manager_is_connected(const network_manager_instance_ctrl_t *const p_ctrl)
{
    return (p_ctrl != NULL) && p_ctrl->init
           && ((xEventGroupGetBits(p_ctrl->p_ready_events) & NETWORK_MANAGER_READY_IP) != 0U);
}

/***********************************************************************************************************************
 * Reports whether the wall clock can be trusted for TLS and event timestamps.
 *
 * @param[in] p_ctrl  Pointer to the runtime control block.
 *
 * @return  True when initialized and the clock is valid.
 **********************************************************************************************************************/
bool network_manager_is_time_valid(const network_manager_instance_ctrl_t *const p_ctrl)
{
    return (p_ctrl != NULL) && p_ctrl->init
           && ((xEventGroupGetBits(p_ctrl->p_ready_events) & NETWORK_MANAGER_READY_TIME) != 0U);
}

/***********************************************************************************************************************
 * Reads the signal strength of the connected access point.
 *
 * @param[in]  p_ctrl  Pointer to the runtime control block.
 * @param[out] p_rssi  Receives the RSSI in dBm.
 *
 * @retval APP_SUCCESS                  RSSI was read.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        Not initialized or not connected.
 * @return                              A mapped system error can also be returned.
 **********************************************************************************************************************/
app_err_t network_manager_get_rssi(const network_manager_instance_ctrl_t *const p_ctrl, int8_t *const p_rssi)
{
#if NETWORK_MANAGER_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_rssi);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif
    APP_ERROR_RETURN(network_manager_is_connected(p_ctrl), APP_ERR_INVALID_STATE);

    int       rssi = 0;
    esp_err_t ret  = esp_wifi_sta_get_rssi(&rssi);
    if (ret == ESP_OK)
    {
        *p_rssi = (int8_t)rssi;
    }
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Validates the initialization arguments.
 **********************************************************************************************************************/
static app_err_t network_manager_validate_init_args(network_manager_instance_ctrl_t *const p_ctrl,
                                                    const network_manager_config_t *const  p_cfg)
{
#if NETWORK_MANAGER_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);
    APP_ASSERT(NULL != p_cfg->p_ssid);
    APP_ASSERT(NULL != p_cfg->p_password);
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);

    const size_t ssid_len     = strnlen(p_cfg->p_ssid, NETWORK_MANAGER_SSID_MAX_LEN + 1U);
    const size_t password_len = strnlen(p_cfg->p_password, NETWORK_MANAGER_PASSWORD_MAX_LEN + 1U);
    APP_ERROR_RETURN((ssid_len > 0U) && (ssid_len <= NETWORK_MANAGER_SSID_MAX_LEN), APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((password_len >= NETWORK_MANAGER_PASSWORD_MIN_LEN)
                         && (password_len <= NETWORK_MANAGER_PASSWORD_MAX_LEN),
                     APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(network_manager_is_allowed_auth(p_cfg->min_auth_mode), APP_ERR_INVALID_ARGUMENT);

    const uint32_t min_ms = (p_cfg->reconnect_min_ms != 0U) ? p_cfg->reconnect_min_ms
                                                            : NETWORK_MANAGER_DEFAULT_RECONNECT_MIN_MS;
    const uint32_t max_ms = (p_cfg->reconnect_max_ms != 0U) ? p_cfg->reconnect_max_ms
                                                            : NETWORK_MANAGER_DEFAULT_RECONNECT_MAX_MS;
    APP_ERROR_RETURN(min_ms <= max_ms, APP_ERR_INVALID_ARGUMENT);
#else
    (void)p_ctrl;
    (void)p_cfg;
#endif

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * A door lock must not join open, WEP, or WPA1 networks. OPEN stands for "default" because configs are zero-filled.
 **********************************************************************************************************************/
static bool network_manager_is_allowed_auth(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode)
    {
        case WIFI_AUTH_OPEN:
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
        case WIFI_AUTH_WPA3_PSK:
            return true;
        default:
            return false;
    }
}

/***********************************************************************************************************************
 * Initializes the shared netif layer and default event loop, then creates the station interface.
 **********************************************************************************************************************/
static esp_err_t network_manager_setup_netif(network_manager_instance_ctrl_t *const p_ctrl)
{
    /* Other modules may already have created these shared services. */
    esp_err_t ret = esp_netif_init();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        return ret;
    }

    p_ctrl->p_netif = esp_netif_create_default_wifi_sta();
    if (p_ctrl->p_netif == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    /* The hostname is cosmetic (router client list), so a rejected one must not keep Wi-Fi down. */
    if ((p_ctrl->p_cfg->p_hostname != NULL)
        && (esp_netif_set_hostname(p_ctrl->p_netif, p_ctrl->p_cfg->p_hostname) != ESP_OK))
    {
        ESP_LOGW(NETWORK_MANAGER_TAG, "Hostname \"%s\" rejected (max 32 characters)", p_ctrl->p_cfg->p_hostname);
    }
    return ESP_OK;
}

/***********************************************************************************************************************
 * Initializes the Wi-Fi driver, event handlers, reconnect timer, and station credentials.
 **********************************************************************************************************************/
static esp_err_t network_manager_setup_wifi(network_manager_instance_ctrl_t *const p_ctrl)
{
    const network_manager_config_t *p_cfg = p_ctrl->p_cfg;

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t                ret      = esp_wifi_init(&init_cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }
    p_ctrl->wifi_initialized = true;

    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, network_manager_wifi_event_handler, p_ctrl, &p_ctrl->wifi_handler);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, network_manager_ip_event_handler, p_ctrl, &p_ctrl->ip_handler);
    if (ret != ESP_OK)
    {
        return ret;
    }

    const esp_timer_create_args_t timer_args = {
        .callback        = network_manager_reconnect_timer_cb,
        .arg             = p_ctrl,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "net_reconnect",
    };
    ret = esp_timer_create(&timer_args, &p_ctrl->p_reconnect_timer);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* The SSID and passphrase fields are fixed-size arrays and need not be NUL-terminated at full length. */
    wifi_config_t sta_cfg = {0};
    memcpy(sta_cfg.sta.ssid, p_cfg->p_ssid, strnlen(p_cfg->p_ssid, sizeof(sta_cfg.sta.ssid)));
    memcpy(sta_cfg.sta.password, p_cfg->p_password, strnlen(p_cfg->p_password, sizeof(sta_cfg.sta.password)));
    sta_cfg.sta.threshold.authmode = (p_cfg->min_auth_mode == WIFI_AUTH_OPEN) ? WIFI_AUTH_WPA2_PSK
                                                                              : p_cfg->min_auth_mode;
    sta_cfg.sta.sae_pwe_h2e        = WPA3_SAE_PWE_BOTH;
    sta_cfg.sta.pmf_cfg.capable    = true;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        return ret;
    }
    return esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
}

/***********************************************************************************************************************
 * Configures the SNTP client. It is started on the first IP address and then resyncs periodically on its own.
 **********************************************************************************************************************/
static esp_err_t network_manager_setup_sntp(network_manager_instance_ctrl_t *const p_ctrl)
{
    const char *p_server = (p_ctrl->p_cfg->p_ntp_server != NULL) ? p_ctrl->p_cfg->p_ntp_server
                                                                 : NETWORK_MANAGER_DEFAULT_NTP_SERVER;

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(p_server);
    sntp_cfg.start             = false;
    sntp_cfg.sync_cb           = network_manager_sntp_sync_cb;

    esp_err_t ret = esp_netif_sntp_init(&sntp_cfg);
    if (ret == ESP_OK)
    {
        p_ctrl->sntp_initialized = true;
    }
    return ret;
}

/***********************************************************************************************************************
 * Arms the one-shot reconnect timer and doubles the delay for the next failure.
 **********************************************************************************************************************/
static void network_manager_schedule_reconnect(network_manager_instance_ctrl_t *const p_ctrl)
{
    const uint32_t max_ms = (p_ctrl->p_cfg->reconnect_max_ms != 0U) ? p_ctrl->p_cfg->reconnect_max_ms
                                                                    : NETWORK_MANAGER_DEFAULT_RECONNECT_MAX_MS;

    /* ESP_ERR_INVALID_STATE means a retry is already pending, which is fine. */
    esp_err_t ret = esp_timer_start_once(p_ctrl->p_reconnect_timer,
                                         (uint64_t)p_ctrl->reconnect_delay_ms * NETWORK_MANAGER_US_PER_MS);
    if (ret == ESP_OK)
    {
        ESP_LOGI(NETWORK_MANAGER_TAG, "Retrying in %" PRIu32 " ms", p_ctrl->reconnect_delay_ms);
    }

    p_ctrl->reconnect_delay_ms = (p_ctrl->reconnect_delay_ms >= (max_ms / 2U)) ? max_ms
                                                                               : (p_ctrl->reconnect_delay_ms * 2U);
}

/***********************************************************************************************************************
 * Explains the disconnect reasons that usually come from a configuration mistake.
 **********************************************************************************************************************/
static const char *network_manager_reason_hint(uint8_t reason)
{
    switch (reason)
    {
        case WIFI_REASON_NO_AP_FOUND:
            return "access point not found; check the SSID and that it is 2.4 GHz";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "authentication failed; check the password";
        default:
            return "";
    }
}

/***********************************************************************************************************************
 * Forwards an event to the optional application callback.
 **********************************************************************************************************************/
static void network_manager_notify(network_manager_instance_ctrl_t *const p_ctrl, network_manager_event_t event)
{
    if (p_ctrl->p_cfg->p_callback != NULL)
    {
        p_ctrl->p_cfg->p_callback(event, p_ctrl->p_cfg->p_user_ctx);
    }
}

/***********************************************************************************************************************
 * Handles Wi-Fi driver events in the default event loop task.
 **********************************************************************************************************************/
static void network_manager_wifi_event_handler(void            *p_handler_args,
                                               esp_event_base_t base,
                                               int32_t          event_id,
                                               void            *p_data)
{
    (void)base;
    network_manager_instance_ctrl_t *p_ctrl = (network_manager_instance_ctrl_t *)p_handler_args;
    if ((p_ctrl == NULL) || (!p_ctrl->init))
    {
        return;
    }

    switch (event_id)
    {
        case WIFI_EVENT_STA_START:
            (void)esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            const wifi_event_sta_disconnected_t *p_event = (const wifi_event_sta_disconnected_t *)p_data;
            const EventBits_t before = xEventGroupClearBits(p_ctrl->p_ready_events, NETWORK_MANAGER_READY_IP);

            ESP_LOGW(NETWORK_MANAGER_TAG,
                     "Disconnected (reason %u) %s",
                     p_event->reason,
                     network_manager_reason_hint(p_event->reason));
            network_manager_schedule_reconnect(p_ctrl);

            if ((before & NETWORK_MANAGER_READY_IP) != 0U)
            {
                network_manager_notify(p_ctrl, NETWORK_MANAGER_EVENT_DISCONNECTED);
            }
            break;
        }

        default:
            break;
    }
}

/***********************************************************************************************************************
 * Handles IP_EVENT_STA_GOT_IP in the default event loop task.
 **********************************************************************************************************************/
static void network_manager_ip_event_handler(void            *p_handler_args,
                                             esp_event_base_t base,
                                             int32_t          event_id,
                                             void            *p_data)
{
    (void)base;
    (void)event_id;
    network_manager_instance_ctrl_t *p_ctrl = (network_manager_instance_ctrl_t *)p_handler_args;
    if ((p_ctrl == NULL) || (!p_ctrl->init))
    {
        return;
    }

    const ip_event_got_ip_t *p_event = (const ip_event_got_ip_t *)p_data;
    ESP_LOGI(NETWORK_MANAGER_TAG, "Got IP " IPSTR, IP2STR(&p_event->ip_info.ip));

    p_ctrl->reconnect_delay_ms = (p_ctrl->p_cfg->reconnect_min_ms != 0U) ? p_ctrl->p_cfg->reconnect_min_ms
                                                                         : NETWORK_MANAGER_DEFAULT_RECONNECT_MIN_MS;

    if (!p_ctrl->sntp_started)
    {
        if (esp_netif_sntp_start() == ESP_OK)
        {
            p_ctrl->sntp_started = true;
        }
        else
        {
            ESP_LOGE(NETWORK_MANAGER_TAG, "SNTP start failed; the clock may stay invalid");
        }
    }

    (void)xEventGroupSetBits(p_ctrl->p_ready_events, NETWORK_MANAGER_READY_IP);
    network_manager_notify(p_ctrl, NETWORK_MANAGER_EVENT_CONNECTED);
}

/***********************************************************************************************************************
 * Retries the connection. Runs in the esp_timer task.
 **********************************************************************************************************************/
static void network_manager_reconnect_timer_cb(void *p_arg)
{
    network_manager_instance_ctrl_t *p_ctrl = (network_manager_instance_ctrl_t *)p_arg;
    if ((p_ctrl == NULL) || (!p_ctrl->init))
    {
        return;
    }

    /* An immediate failure produces no disconnect event, so rearm the timer here or retries would stop. */
    const esp_err_t ret = esp_wifi_connect();
    if ((ret != ESP_OK) && (ret != ESP_ERR_WIFI_CONN))
    {
        ESP_LOGW(NETWORK_MANAGER_TAG, "Connect request failed: %s", esp_err_to_name(ret));
        network_manager_schedule_reconnect(p_ctrl);
    }
}

/***********************************************************************************************************************
 * Marks the clock valid after each SNTP sync. Runs in the lwIP task.
 **********************************************************************************************************************/
static void network_manager_sntp_sync_cb(struct timeval *p_tv)
{
    network_manager_instance_ctrl_t *p_ctrl = s_p_active_ctrl;
    if ((p_ctrl == NULL) || (!p_ctrl->init) || (p_tv == NULL))
    {
        return;
    }

    const time_t now = p_tv->tv_sec;
    struct tm    local;
    char         text[32];
    (void)localtime_r(&now, &local);
    (void)strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
    ESP_LOGI(NETWORK_MANAGER_TAG, "Time synced: %s", text);

    (void)xEventGroupSetBits(p_ctrl->p_ready_events, NETWORK_MANAGER_READY_TIME);
    network_manager_notify(p_ctrl, NETWORK_MANAGER_EVENT_TIME_SYNCED);
}

/***********************************************************************************************************************
 * Releases every resource acquired by the instance, in reverse order of creation.
 **********************************************************************************************************************/
static void network_manager_release(network_manager_instance_ctrl_t *const p_ctrl)
{
    /* Handlers check init, so clear it first to stop them from rearming the reconnect timer. */
    p_ctrl->init = false;
    if (s_p_active_ctrl == p_ctrl)
    {
        s_p_active_ctrl = NULL;
    }

    if (p_ctrl->ip_handler != NULL)
    {
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, p_ctrl->ip_handler);
        p_ctrl->ip_handler = NULL;
    }
    if (p_ctrl->wifi_handler != NULL)
    {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, p_ctrl->wifi_handler);
        p_ctrl->wifi_handler = NULL;
    }
    if (p_ctrl->p_reconnect_timer != NULL)
    {
        (void)esp_timer_stop(p_ctrl->p_reconnect_timer);
        (void)esp_timer_delete(p_ctrl->p_reconnect_timer);
        p_ctrl->p_reconnect_timer = NULL;
    }
    if (p_ctrl->sntp_initialized)
    {
        esp_netif_sntp_deinit();
        p_ctrl->sntp_initialized = false;
        p_ctrl->sntp_started     = false;
    }
    if (p_ctrl->wifi_started)
    {
        (void)esp_wifi_stop();
        p_ctrl->wifi_started = false;
    }
    if (p_ctrl->wifi_initialized)
    {
        (void)esp_wifi_deinit();
        p_ctrl->wifi_initialized = false;
    }
    if (p_ctrl->p_netif != NULL)
    {
        esp_netif_destroy_default_wifi(p_ctrl->p_netif);
        p_ctrl->p_netif = NULL;
    }
    if (p_ctrl->p_ready_events != NULL)
    {
        vEventGroupDelete(p_ctrl->p_ready_events);
        p_ctrl->p_ready_events = NULL;
    }

    p_ctrl->p_cfg = NULL;
}
