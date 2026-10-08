#ifndef DOOR_SIM_H_
#define DOOR_SIM_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include "door_sim_config.h"
#include "app_err.h"
#include "event_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Configuration remains caller-owned and must stay valid and unchanged for the lifetime of the instance. */
typedef struct st_door_sim_config
{
    event_log_instance_ctrl_t *p_log;       /* Initialized event log that receives the simulated entries. */
    gpio_num_t                 button_gpio; /* Active-low button, e.g. BOOT (GPIO0). GPIO_NUM_NC disables it. */
    bool                       console;     /* Register the "sim" command on a serial console (idf.py monitor). */
} door_sim_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_door_sim_instance_ctrl
{
    const door_sim_config_t *p_cfg;
    TaskHandle_t             button_task;
    bool                     init;
} door_sim_instance_ctrl_t;

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* DEV BOARD ONLY. Produces access events and alerts without keypad, NFC or sensors, through the same event_log API the
 * real lock_service will use, so the whole MQTT and web path can be tested on a bare ESP32-S3.
 *
 * Inputs are local on purpose (a button and the USB serial console): nothing received over the network can create
 * entries, which keeps the "no remote control" rule of docs/mqtt_protocol.md. Disable with CONFIG_DOOR_SIM_ENABLE. */
#ifdef __cplusplus
extern "C"
{
#endif

    app_err_t door_sim_init(door_sim_instance_ctrl_t *const p_ctrl, const door_sim_config_t *const p_cfg);

#ifdef __cplusplus
}
#endif

#endif /* DOOR_SIM_H_ */
