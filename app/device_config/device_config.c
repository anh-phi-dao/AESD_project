/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "device_config.h"
#include "err_map.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define DEVICE_CONFIG_TAG "DEV_CFG"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static app_err_t device_config_read_str(nvs_handle_t handle, const char *const p_key, char *const p_buf, size_t size);
static app_err_t device_config_read_ca(nvs_handle_t handle, device_config_t *const p_cfg);
static app_err_t device_config_read_all(nvs_handle_t handle, device_config_t *const p_cfg);

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Reads the device credentials from the devcfg partition, then releases the partition again.
 *
 * @param[out] p_cfg  Receives the credentials. Call device_config_release() when they are no longer needed.
 *
 * @retval APP_SUCCESS                  Every required value was read.
 * @retval APP_ERR_INVALID_POINTER      p_cfg is NULL.
 * @retval APP_ERR_NOT_FOUND            The partition, the namespace, or a required key is missing.
 * @retval APP_ERR_INVALID_ARGUMENT     A value is empty or longer than its buffer.
 * @retval APP_ERR_NO_MEMORY            The CA certificate buffer could not be allocated.
 * @return                              A mapped system error can also be returned.
 **********************************************************************************************************************/
app_err_t device_config_load(device_config_t *const p_cfg)
{
#if DEVICE_CONFIG_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_cfg);
#endif

    memset(p_cfg, 0, sizeof(*p_cfg));

    /* A damaged factory partition is reported, never erased: erasing would lose the credentials for good. */
    esp_err_t ret = nvs_flash_init_partition(DEVICE_CONFIG_PARTITION);
    if (ret != ESP_OK)
    {
        /* Typical after changing the partition table: old app bytes still sit where devcfg now is. */
        ESP_LOGE(DEVICE_CONFIG_TAG,
                 "Cannot open partition \"%s\": %s. Run scripts/provision_device.py",
                 DEVICE_CONFIG_PARTITION,
                 esp_err_to_name(ret));
        return err_map_esp_to_app(ret);
    }

    nvs_handle_t handle  = 0;
    app_err_t    app_ret = APP_SUCCESS;
    ret = nvs_open_from_partition(DEVICE_CONFIG_PARTITION, DEVICE_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(DEVICE_CONFIG_TAG, "Device is not provisioned; see docs/provisioning.md");
        app_ret = APP_ERR_NOT_FOUND;
    }
    else if (ret != ESP_OK)
    {
        app_ret = err_map_esp_to_app(ret);
    }
    else
    {
        app_ret = device_config_read_all(handle, p_cfg);
        nvs_close(handle);
    }

    (void)nvs_flash_deinit_partition(DEVICE_CONFIG_PARTITION);

    if (app_ret != APP_SUCCESS)
    {
        device_config_release(p_cfg);
        return app_ret;
    }

    ESP_LOGI(DEVICE_CONFIG_TAG,
             "Loaded device \"%s\" (Wi-Fi \"%s\", broker %s, CA %s)",
             p_cfg->device_id,
             p_cfg->wifi_ssid,
             p_cfg->mqtt_uri,
             (p_cfg->p_mqtt_ca_pem != NULL) ? "custom" : "bundle");
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Wipes secrets from RAM and frees the CA buffer.
 *
 * @param[in,out] p_cfg  Credentials returned by device_config_load(), or a zeroed struct.
 **********************************************************************************************************************/
void device_config_release(device_config_t *const p_cfg)
{
    if (p_cfg == NULL)
    {
        return;
    }
    free(p_cfg->p_mqtt_ca_pem);
    memset(p_cfg, 0, sizeof(*p_cfg));
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Reads the required strings and the optional CA certificate.
 **********************************************************************************************************************/
static app_err_t device_config_read_all(nvs_handle_t handle, device_config_t *const p_cfg)
{
    const struct
    {
        const char *p_key;
        char       *p_buf;
        size_t      size;
    } fields[] = {
        {DEVICE_CONFIG_KEY_WIFI_SSID, p_cfg->wifi_ssid, sizeof(p_cfg->wifi_ssid)},
        {DEVICE_CONFIG_KEY_WIFI_PASSWORD, p_cfg->wifi_password, sizeof(p_cfg->wifi_password)},
        {DEVICE_CONFIG_KEY_DEVICE_ID, p_cfg->device_id, sizeof(p_cfg->device_id)},
        {DEVICE_CONFIG_KEY_MQTT_URI, p_cfg->mqtt_uri, sizeof(p_cfg->mqtt_uri)},
        {DEVICE_CONFIG_KEY_MQTT_USERNAME, p_cfg->mqtt_username, sizeof(p_cfg->mqtt_username)},
        {DEVICE_CONFIG_KEY_MQTT_PASSWORD, p_cfg->mqtt_password, sizeof(p_cfg->mqtt_password)},
    };

    for (size_t i = 0U; i < (sizeof(fields) / sizeof(fields[0])); i++)
    {
        app_err_t app_ret = device_config_read_str(handle, fields[i].p_key, fields[i].p_buf, fields[i].size);
        if (app_ret != APP_SUCCESS)
        {
            return app_ret;
        }
    }

    return device_config_read_ca(handle, p_cfg);
}

/***********************************************************************************************************************
 * Reads one required, non-empty string. Values are never logged because some of them are passwords.
 **********************************************************************************************************************/
static app_err_t device_config_read_str(nvs_handle_t handle, const char *const p_key, char *const p_buf, size_t size)
{
    size_t    len = size;
    esp_err_t ret = nvs_get_str(handle, p_key, p_buf, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(DEVICE_CONFIG_TAG, "Missing key \"%s\"", p_key);
        return APP_ERR_NOT_FOUND;
    }
    if (ret == ESP_ERR_NVS_INVALID_LENGTH)
    {
        ESP_LOGE(DEVICE_CONFIG_TAG, "Value of \"%s\" is longer than %u bytes", p_key, (unsigned)(size - 1U));
        return APP_ERR_INVALID_ARGUMENT;
    }
    if (ret != ESP_OK)
    {
        return err_map_esp_to_app(ret);
    }
    if (p_buf[0] == '\0')
    {
        ESP_LOGE(DEVICE_CONFIG_TAG, "Value of \"%s\" is empty", p_key);
        return APP_ERR_INVALID_ARGUMENT;
    }
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Reads the optional PEM CA certificate used for a self-hosted broker.
 **********************************************************************************************************************/
static app_err_t device_config_read_ca(nvs_handle_t handle, device_config_t *const p_cfg)
{
    size_t    len = 0U;
    esp_err_t ret = nvs_get_str(handle, DEVICE_CONFIG_KEY_MQTT_CA_PEM, NULL, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        return APP_SUCCESS;
    }
    if (ret != ESP_OK)
    {
        return err_map_esp_to_app(ret);
    }
    APP_ERROR_RETURN((len > 1U) && (len <= DEVICE_CONFIG_MQTT_CA_MAX_SIZE), APP_ERR_INVALID_ARGUMENT);

    p_cfg->p_mqtt_ca_pem = malloc(len);
    if (p_cfg->p_mqtt_ca_pem == NULL)
    {
        return APP_ERR_NO_MEMORY;
    }
    return err_map_esp_to_app(nvs_get_str(handle, DEVICE_CONFIG_KEY_MQTT_CA_PEM, p_cfg->p_mqtt_ca_pem, &len));
}
