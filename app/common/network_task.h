#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

#include <stdbool.h>
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define NETWORK_TASK_STACK    6144U
#define NETWORK_TASK_PRIORITY 4U

/***********************************************************************************************************************
 * Starts the event log and its MQTT bridge, then the dev-board door simulator when enabled. Call before network_task,
 * so entries made while Wi-Fi is still connecting are kept and published on the first broker connection.
 **********************************************************************************************************************/
void boot_event_log(void);

/***********************************************************************************************************************
 * Loads the device credentials, brings up Wi-Fi, waits for a valid clock, then starts MQTT. Deletes itself once MQTT
 * is started, because both drivers reconnect on their own from then on. Requires an initialized NVS.
 **********************************************************************************************************************/
void network_task(void *pvParameter);

/***********************************************************************************************************************
 * The event log for local lock logic to record access attempts and alerts. NULL when boot_event_log() failed.
 **********************************************************************************************************************/
event_log_instance_ctrl_t *network_task_get_event_log(void);

/***********************************************************************************************************************
 * True while Wi-Fi has an IP address. False before network_task started it or without device credentials.
 **********************************************************************************************************************/
bool network_task_is_wifi_connected(void);

#endif
