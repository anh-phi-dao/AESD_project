#ifndef BATTERY_TASK_H
#define BATTERY_TASK_H

#include <stdbool.h>

#include "power_manager.h"

#define BATTERY_TASK_TAG   "BATTERY"
#define BATTERY_TASK_STACK 4096U /* Publishes over TLS itself (mqtt_conn_publish). */

/* Topic below "lock/<device_id>/", see docs/mqtt_protocol.md. */
#define BATTERY_TOPIC "battery" /* door -> web, retained: percent, voltage, current and level of the pack. */

/* Starts the battery task (INA219 on the shared I2C bus). Call after boot_rfid() and boot_pcf7584(), which create
 * that bus. Without an INA219 it keeps retrying in the background and the level stays UNKNOWN: a missing or low
 * battery reading never blocks the lock. */
void boot_battery(void);

/* Latest status. Safe from any task. false while there is no valid reading yet. */
bool battery_task_get_status(power_status_t *const p_out);

/* From the MQTT task, on every broker connection: publish BATTERY_TOPIC again. Never blocks. */
void battery_task_mqtt_connected(void);

#endif
