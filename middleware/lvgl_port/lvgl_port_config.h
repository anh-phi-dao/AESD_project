#ifndef LVGL_PORT_CONFIG_H_
#define LVGL_PORT_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init ordering from the caller. */
#ifndef LVGL_PORT_CFG_PARAMETER_CHECKING
#define LVGL_PORT_CFG_PARAMETER_CHECKING 1
#endif

#if (LVGL_PORT_CFG_PARAMETER_CHECKING != 0) && (LVGL_PORT_CFG_PARAMETER_CHECKING != 1)
#error "LVGL_PORT_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* lv_tick_inc() period. CONFIG_LV_TICK_CUSTOM must stay off, because this timer is the tick source. */
#define LVGL_PORT_TICK_PERIOD_MS 2U

/* lv_timer_handler() runs in its own task. Below the network task (4) so drawing never delays MQTT. Rendering
 * SquareLine screens needs more stack than the LVGL examples; 6 KB leaves room for gradients and image decoding. */
#define LVGL_PORT_TASK_NAME          "lvgl"
#define LVGL_PORT_TASK_STACK         6144U
#define LVGL_PORT_TASK_PRIO          2U
#define LVGL_PORT_TASK_MIN_WAIT_MS   5U
#define LVGL_PORT_TASK_MAX_WAIT_MS   500U

/* Keys queued between lvgl_port_post_key() and the LVGL keypad read callback. */
#define LVGL_PORT_KEY_QUEUE_LEN      8U

#endif /* LVGL_PORT_CONFIG_H_ */
