#ifndef LVGL_PORT_H_
#define LVGL_PORT_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "app_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_port_config.h"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Starts sending p_area (inclusive coordinates) to the display. The display driver must call lvgl_port_flush_ready()
 * once p_pixels may be reused, from a task or an interrupt. */
typedef app_err_t (*lvgl_port_flush_t)(void *p_context, const lv_area_t *p_area, const void *p_pixels);

/* Configuration remains caller-owned and must stay valid and unchanged for the lifetime of the instance. */
typedef struct st_lvgl_port_config
{
    lvgl_port_flush_t p_flush;         /* Display write, see lvgl_port_flush_t. */
    void             *p_flush_context; /* Passed to p_flush. */
    uint16_t          hor_res;         /* Must match the display orientation and the SquareLine project. */
    uint16_t          ver_res;
    uint16_t          buf_lines; /* Lines per draw buffer. Two buffers of hor_res * buf_lines pixels are taken
                                  * from internal DMA RAM: 20 lines at 320 px = 2 x 12.5 KB. */
    bool use_keypad;             /* Create a keypad input device and a default group for lvgl_port_post_key(). */
} lvgl_port_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_lvgl_port_instance_ctrl
{
    const lvgl_port_config_t *p_cfg;
    lv_disp_draw_buf_t        draw_buf;
    lv_disp_drv_t             disp_drv;
    lv_disp_t                *p_disp;
    lv_color_t               *p_buf1;
    lv_color_t               *p_buf2;
    lv_indev_drv_t            keypad_drv;
    lv_indev_t               *p_keypad;
    lv_group_t               *p_group;
    SemaphoreHandle_t         mutex;
    StaticSemaphore_t         mutex_buf;
    QueueHandle_t             key_queue;
    StaticQueue_t             key_queue_buf;
    uint8_t                   key_queue_storage[LVGL_PORT_KEY_QUEUE_LEN * sizeof(uint32_t)];
    uint32_t                  last_key;            /* Used only by the LVGL task. */
    bool                      key_release_pending; /* Used only by the LVGL task. */
    esp_timer_handle_t        tick_timer;
    TaskHandle_t              task;
    bool                      init;
} lvgl_port_instance_ctrl_t;

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Runs LVGL 8 on FreeRTOS: tick timer, draw buffers, display driver, one task for lv_timer_handler() and a keypad
 * input fed from any task. LVGL is a global library, so there is one instance and no deinit.
 *
 * LVGL is not thread-safe. Any task other than the LVGL task must hold lvgl_port_lock() while calling lv_* functions
 * or touching SquareLine objects (ui_*). LVGL event callbacks already run under the lock.
 * Full descriptions are beside the implementations in lvgl_port.c. */
#ifdef __cplusplus
extern "C"
{
#endif

    app_err_t lvgl_port_init(lvgl_port_instance_ctrl_t *const p_ctrl, const lvgl_port_config_t *const p_cfg);

    app_err_t lvgl_port_lock(lvgl_port_instance_ctrl_t *const p_ctrl, uint32_t timeout_ms);

    void lvgl_port_unlock(lvgl_port_instance_ctrl_t *const p_ctrl);

    /* Call from the display driver's transfer-done callback; safe in an interrupt. Returns false (no task woken). */
    bool lvgl_port_flush_ready(lvgl_port_instance_ctrl_t *const p_ctrl);

    /* Queue one key press for the keypad input: a printable character or an LV_KEY_* code. Never blocks. */
    app_err_t lvgl_port_post_key(lvgl_port_instance_ctrl_t *const p_ctrl, uint32_t key);

    /* Default group that new focusable objects join, NULL without use_keypad. Call under the lock. */
    lv_group_t *lvgl_port_get_group(lvgl_port_instance_ctrl_t *const p_ctrl);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_PORT_H_ */
