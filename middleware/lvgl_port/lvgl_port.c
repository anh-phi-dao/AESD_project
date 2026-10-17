/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stddef.h>

#include "lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define LVGL_PORT_TAG "LVGL_PORT"

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static app_err_t lvgl_port_validate_init_args(lvgl_port_instance_ctrl_t *const p_ctrl,
                                              const lvgl_port_config_t *const  p_cfg);
static app_err_t lvgl_port_alloc(lvgl_port_instance_ctrl_t *const p_ctrl);
static void      lvgl_port_free(lvgl_port_instance_ctrl_t *const p_ctrl);
static void      lvgl_port_register_display(lvgl_port_instance_ctrl_t *const p_ctrl);
static void      lvgl_port_register_keypad(lvgl_port_instance_ctrl_t *const p_ctrl);
static void      lvgl_port_flush_cb(lv_disp_drv_t *p_drv, const lv_area_t *p_area, lv_color_t *p_color_map);
static void      lvgl_port_keypad_read_cb(lv_indev_drv_t *p_drv, lv_indev_data_t *p_data);
static void      lvgl_port_tick_cb(void *p_arg);
static void      lvgl_port_task(void *p_arg);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* lv_init() has no counterpart in LVGL 8, so a second instance would corrupt the first. */
static bool s_lvgl_started = false;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Initializes LVGL, registers the display (and keypad), then starts the tick timer and the LVGL task. Call once,
 * after the display driver is ready, and before creating any LVGL object.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged for the lifetime of the instance.
 *
 * @retval APP_SUCCESS               LVGL is running.
 * @retval APP_ERR_INVALID_POINTER   A required API argument is NULL.
 * @retval APP_ERR_INVALID_STATE     LVGL was already started.
 * @retval APP_ERR_INVALID_ARGUMENT  A configuration member is invalid.
 * @retval APP_ERR_NO_MEMORY         The draw buffers, timer or task could not be allocated.
 **********************************************************************************************************************/
app_err_t lvgl_port_init(lvgl_port_instance_ctrl_t *const p_ctrl, const lvgl_port_config_t *const p_cfg)
{
    app_err_t ret = lvgl_port_validate_init_args(p_ctrl, p_cfg);
    if (APP_SUCCESS != ret)
    {
        return ret;
    }

    p_ctrl->p_cfg = p_cfg;

    /* Allocate everything that can fail before lv_init(), which cannot be undone. */
    ret = lvgl_port_alloc(p_ctrl);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(LVGL_PORT_TAG, "Out of memory for draw buffers or RTOS objects");
        lvgl_port_free(p_ctrl);
        return ret;
    }

    lv_init();
    s_lvgl_started = true;

    lvgl_port_register_display(p_ctrl);
    if (p_cfg->use_keypad)
    {
        lvgl_port_register_keypad(p_ctrl);
    }

    if (ESP_OK != esp_timer_start_periodic(p_ctrl->tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000U))
    {
        ESP_LOGE(LVGL_PORT_TAG, "Tick timer start failed");
        return APP_FAIL;
    }

    p_ctrl->init = true;

    if (pdPASS
        != xTaskCreate(
            lvgl_port_task, LVGL_PORT_TASK_NAME, LVGL_PORT_TASK_STACK, p_ctrl, LVGL_PORT_TASK_PRIO, &p_ctrl->task))
    {
        ESP_LOGE(LVGL_PORT_TAG, "LVGL task creation failed");
        (void)esp_timer_stop(p_ctrl->tick_timer);
        p_ctrl->init = false;
        return APP_ERR_NO_MEMORY;
    }

    ESP_LOGI(LVGL_PORT_TAG,
             "LVGL %d.%d.%d running at %ux%u",
             LVGL_VERSION_MAJOR,
             LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH,
             p_cfg->hor_res,
             p_cfg->ver_res);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Takes the LVGL lock. Recursive, so a task holding it may call functions that take it again.
 *
 * @param[in,out] p_ctrl      Pointer to an initialized runtime control block.
 * @param[in]     timeout_ms  Longest wait; UINT32_MAX waits forever.
 *
 * @retval APP_SUCCESS              The caller holds the lock and must call lvgl_port_unlock().
 * @retval APP_ERR_INVALID_POINTER  The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE    The control block is not initialized.
 * @retval APP_ERR_TIMEOUT          The LVGL task kept the lock for longer than timeout_ms.
 **********************************************************************************************************************/
app_err_t lvgl_port_lock(lvgl_port_instance_ctrl_t *const p_ctrl, uint32_t timeout_ms)
{
#if LVGL_PORT_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    const TickType_t ticks = (UINT32_MAX == timeout_ms) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    return (pdTRUE == xSemaphoreTakeRecursive(p_ctrl->mutex, ticks)) ? APP_SUCCESS : APP_ERR_TIMEOUT;
}

/***********************************************************************************************************************
 * Releases the lock taken by lvgl_port_lock().
 **********************************************************************************************************************/
void lvgl_port_unlock(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    if ((NULL != p_ctrl) && p_ctrl->init)
    {
        (void)xSemaphoreGiveRecursive(p_ctrl->mutex);
    }
}

/***********************************************************************************************************************
 * Tells LVGL the last flushed buffer may be reused. Only clears flags inside LVGL, so it is safe in an interrupt.
 *
 * @return  false: no task was woken.
 **********************************************************************************************************************/
bool lvgl_port_flush_ready(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    if (NULL != p_ctrl)
    {
        lv_disp_flush_ready(&p_ctrl->disp_drv);
    }

    return false;
}

/***********************************************************************************************************************
 * Queues one key for the keypad input device. LVGL sees a press and a release on its next read.
 *
 * @param[in,out] p_ctrl  Pointer to an initialized runtime control block created with use_keypad.
 * @param[in]     key     A printable character or an LV_KEY_* code.
 *
 * @retval APP_SUCCESS              The key was queued.
 * @retval APP_ERR_INVALID_POINTER  The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE    The control block is not initialized or has no keypad input.
 * @retval APP_ERR_TIMEOUT          The queue is full; the key was dropped.
 **********************************************************************************************************************/
app_err_t lvgl_port_post_key(lvgl_port_instance_ctrl_t *const p_ctrl, uint32_t key)
{
#if LVGL_PORT_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(true == p_ctrl->p_cfg->use_keypad, APP_ERR_INVALID_STATE);
#endif

    return (pdTRUE == xQueueSend(p_ctrl->key_queue, &key, 0)) ? APP_SUCCESS : APP_ERR_TIMEOUT;
}

/***********************************************************************************************************************
 * Returns the default group that focusable objects join, or NULL when the keypad is not used.
 **********************************************************************************************************************/
lv_group_t *lvgl_port_get_group(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    return ((NULL != p_ctrl) && p_ctrl->init) ? p_ctrl->p_group : NULL;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

static app_err_t lvgl_port_validate_init_args(lvgl_port_instance_ctrl_t *const p_ctrl,
                                              const lvgl_port_config_t *const  p_cfg)
{
#if LVGL_PORT_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);
    APP_ASSERT(NULL != p_cfg->p_flush);
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(false == s_lvgl_started, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN((0U != p_cfg->hor_res) && (0U != p_cfg->ver_res), APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((0U != p_cfg->buf_lines) && (p_cfg->buf_lines <= p_cfg->ver_res), APP_ERR_INVALID_ARGUMENT);
#else
    (void)p_ctrl;
    (void)p_cfg;
#endif

    return APP_SUCCESS;
}

static app_err_t lvgl_port_alloc(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    const lvgl_port_config_t *const p_cfg     = p_ctrl->p_cfg;
    const size_t                    buf_bytes = (size_t)p_cfg->hor_res * p_cfg->buf_lines * sizeof(lv_color_t);

    /* Two buffers let LVGL render the next stripe while DMA sends the previous one. DMA cannot read PSRAM here. */
    p_ctrl->p_buf1    = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    p_ctrl->p_buf2    = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    p_ctrl->mutex     = xSemaphoreCreateRecursiveMutexStatic(&p_ctrl->mutex_buf);
    p_ctrl->key_queue = xQueueCreateStatic(
        LVGL_PORT_KEY_QUEUE_LEN, sizeof(uint32_t), p_ctrl->key_queue_storage, &p_ctrl->key_queue_buf);

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_port_tick_cb,
        .arg      = NULL,
        .name     = "lvgl_tick",
    };
    p_ctrl->tick_timer = NULL;
    (void)esp_timer_create(&tick_args, &p_ctrl->tick_timer);

    if ((NULL == p_ctrl->p_buf1) || (NULL == p_ctrl->p_buf2) || (NULL == p_ctrl->mutex) || (NULL == p_ctrl->key_queue)
        || (NULL == p_ctrl->tick_timer))
    {
        return APP_ERR_NO_MEMORY;
    }

    return APP_SUCCESS;
}

static void lvgl_port_free(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    heap_caps_free(p_ctrl->p_buf1);
    heap_caps_free(p_ctrl->p_buf2);
    if (NULL != p_ctrl->tick_timer)
    {
        (void)esp_timer_delete(p_ctrl->tick_timer);
    }
    if (NULL != p_ctrl->mutex)
    {
        vSemaphoreDelete(p_ctrl->mutex);
    }
    if (NULL != p_ctrl->key_queue)
    {
        vQueueDelete(p_ctrl->key_queue);
    }

    p_ctrl->p_buf1     = NULL;
    p_ctrl->p_buf2     = NULL;
    p_ctrl->tick_timer = NULL;
    p_ctrl->mutex      = NULL;
    p_ctrl->key_queue  = NULL;
    p_ctrl->p_cfg      = NULL;
}

static void lvgl_port_register_display(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    const lvgl_port_config_t *const p_cfg = p_ctrl->p_cfg;

    lv_disp_draw_buf_init(
        &p_ctrl->draw_buf, p_ctrl->p_buf1, p_ctrl->p_buf2, (uint32_t)p_cfg->hor_res * p_cfg->buf_lines);

    lv_disp_drv_init(&p_ctrl->disp_drv);
    p_ctrl->disp_drv.hor_res   = (lv_coord_t)p_cfg->hor_res;
    p_ctrl->disp_drv.ver_res   = (lv_coord_t)p_cfg->ver_res;
    p_ctrl->disp_drv.flush_cb  = lvgl_port_flush_cb;
    p_ctrl->disp_drv.draw_buf  = &p_ctrl->draw_buf;
    p_ctrl->disp_drv.user_data = p_ctrl;

    p_ctrl->p_disp = lv_disp_drv_register(&p_ctrl->disp_drv);
}

static void lvgl_port_register_keypad(lvgl_port_instance_ctrl_t *const p_ctrl)
{
    lv_indev_drv_init(&p_ctrl->keypad_drv);
    p_ctrl->keypad_drv.type      = LV_INDEV_TYPE_KEYPAD;
    p_ctrl->keypad_drv.read_cb   = lvgl_port_keypad_read_cb;
    p_ctrl->keypad_drv.user_data = p_ctrl;
    p_ctrl->p_keypad             = lv_indev_drv_register(&p_ctrl->keypad_drv);

    /* A keypad only reaches objects in its group. Making it the default group means every button or text area that
     * SquareLine creates later joins it without generated code having to know about the keypad. */
    p_ctrl->p_group = lv_group_create();
    lv_group_set_default(p_ctrl->p_group);
    lv_indev_set_group(p_ctrl->p_keypad, p_ctrl->p_group);
}

static void lvgl_port_flush_cb(lv_disp_drv_t *p_drv, const lv_area_t *p_area, lv_color_t *p_color_map)
{
    lvgl_port_instance_ctrl_t *const p_ctrl = (lvgl_port_instance_ctrl_t *)p_drv->user_data;

    if (APP_SUCCESS != p_ctrl->p_cfg->p_flush(p_ctrl->p_cfg->p_flush_context, p_area, p_color_map))
    {
        /* No transfer started, so no completion will come; release the buffer or LVGL waits forever. */
        lv_disp_flush_ready(p_drv);
    }
}

/* Turns each queued key into a press followed by a release, as LVGL expects from a keypad. */
static void lvgl_port_keypad_read_cb(lv_indev_drv_t *p_drv, lv_indev_data_t *p_data)
{
    lvgl_port_instance_ctrl_t *const p_ctrl = (lvgl_port_instance_ctrl_t *)p_drv->user_data;
    uint32_t                         key    = 0U;

    if (p_ctrl->key_release_pending)
    {
        p_ctrl->key_release_pending = false;
        p_data->key                 = p_ctrl->last_key;
        p_data->state               = LV_INDEV_STATE_RELEASED;
        p_data->continue_reading    = (0U != uxQueueMessagesWaiting(p_ctrl->key_queue));
    }
    else if (pdTRUE == xQueueReceive(p_ctrl->key_queue, &key, 0))
    {
        p_ctrl->last_key            = key;
        p_ctrl->key_release_pending = true;
        p_data->key                 = key;
        p_data->state               = LV_INDEV_STATE_PRESSED;
        p_data->continue_reading    = true; /* Deliver the release in the same cycle. */
    }
    else
    {
        p_data->key   = p_ctrl->last_key;
        p_data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_port_tick_cb(void *p_arg)
{
    (void)p_arg;
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *p_arg)
{
    lvgl_port_instance_ctrl_t *const p_ctrl = (lvgl_port_instance_ctrl_t *)p_arg;

    for (;;)
    {
        uint32_t wait_ms = LVGL_PORT_TASK_MAX_WAIT_MS;

        if (pdTRUE == xSemaphoreTakeRecursive(p_ctrl->mutex, portMAX_DELAY))
        {
            wait_ms = lv_timer_handler();
            (void)xSemaphoreGiveRecursive(p_ctrl->mutex);
        }

        if (wait_ms < LVGL_PORT_TASK_MIN_WAIT_MS)
        {
            wait_ms = LVGL_PORT_TASK_MIN_WAIT_MS;
        }
        else if (wait_ms > LVGL_PORT_TASK_MAX_WAIT_MS)
        {
            wait_ms = LVGL_PORT_TASK_MAX_WAIT_MS;
        }

        /* At 100 Hz, 5 ms rounds to 0 ticks, which would starve lower-priority tasks. */
        const TickType_t ticks = pdMS_TO_TICKS(wait_ms);
        vTaskDelay((0U == ticks) ? 1U : ticks);
    }
}
