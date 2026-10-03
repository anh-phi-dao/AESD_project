#ifndef DEVICE_CONFIG_CONFIG_H_
#define DEVICE_CONFIG_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require a valid output pointer from the caller. */
#ifndef DEVICE_CONFIG_CFG_PARAMETER_CHECKING
#define DEVICE_CONFIG_CFG_PARAMETER_CHECKING 1
#endif

#if (DEVICE_CONFIG_CFG_PARAMETER_CHECKING != 0) && (DEVICE_CONFIG_CFG_PARAMETER_CHECKING != 1)
#error "DEVICE_CONFIG_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* Must match partitions.csv and config/device_config.csv.example. */
#define DEVICE_CONFIG_PARTITION            "devcfg"
#define DEVICE_CONFIG_NAMESPACE            "device_cfg"

/* NVS keys, at most 15 characters each. */
#define DEVICE_CONFIG_KEY_WIFI_SSID        "wifi_ssid"
#define DEVICE_CONFIG_KEY_WIFI_PASSWORD    "wifi_pass"
#define DEVICE_CONFIG_KEY_DEVICE_ID        "device_id"
#define DEVICE_CONFIG_KEY_MQTT_URI         "mqtt_uri"
#define DEVICE_CONFIG_KEY_MQTT_USERNAME    "mqtt_user"
#define DEVICE_CONFIG_KEY_MQTT_PASSWORD    "mqtt_pass"
#define DEVICE_CONFIG_KEY_MQTT_CA_PEM      "mqtt_ca"

/* Buffer sizes including the terminator. */
#define DEVICE_CONFIG_WIFI_SSID_SIZE       33U
#define DEVICE_CONFIG_WIFI_PASSWORD_SIZE   65U
#define DEVICE_CONFIG_DEVICE_ID_SIZE       33U
#define DEVICE_CONFIG_MQTT_URI_SIZE        128U
#define DEVICE_CONFIG_MQTT_USERNAME_SIZE   65U
#define DEVICE_CONFIG_MQTT_PASSWORD_SIZE   129U

/* NVS strings are limited to 4000 bytes, which fits one PEM CA certificate. */
#define DEVICE_CONFIG_MQTT_CA_MAX_SIZE     4000U

#endif /* DEVICE_CONFIG_CONFIG_H_ */
