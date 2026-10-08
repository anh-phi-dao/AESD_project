#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

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

#endif
