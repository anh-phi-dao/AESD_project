#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "device_config_config.h"
#include "app_err.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Per-device credentials read from the devcfg NVS partition. Strings are always NUL-terminated. */
typedef struct st_device_config
{
    char  wifi_ssid[DEVICE_CONFIG_WIFI_SSID_SIZE];
    char  wifi_password[DEVICE_CONFIG_WIFI_PASSWORD_SIZE];
    char  device_id[DEVICE_CONFIG_DEVICE_ID_SIZE];
    char  mqtt_uri[DEVICE_CONFIG_MQTT_URI_SIZE];
    char  mqtt_username[DEVICE_CONFIG_MQTT_USERNAME_SIZE];
    char  mqtt_password[DEVICE_CONFIG_MQTT_PASSWORD_SIZE];
    char *p_mqtt_ca_pem; /* Optional; NULL means "verify with the certificate bundle". Heap, freed by release. */
} device_config_t;

/***********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* The devcfg partition is written at the factory (scripts/provision_device.py) and is read-only at runtime, so
 * re-provisioning a device never touches user data in the main nvs partition. */
#ifdef __cplusplus
extern "C"
{
#endif

    /* Read every credential. Returns APP_ERR_NOT_FOUND when the device has not been provisioned. */
    app_err_t device_config_load(device_config_t *const p_cfg);

    /* Wipe secrets from RAM and free the CA buffer. Safe to call on a zeroed or partially loaded struct. */
    void device_config_release(device_config_t *const p_cfg);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_CONFIG_H_ */
