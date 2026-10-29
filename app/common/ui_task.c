/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "ui_task.h"

#if UI_TASK_ENABLED
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "ili9341.h"
#include "lvgl_port.h"
#include "ui_service.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define UI_TASK_TAG           "UI"

/* The ILI9341 driver always sets landscape, rotated 180 degrees (MADCTL 0xE8): 320 x 240. */
#define UI_TASK_LCD_HOR_RES   ILI9341_VER_RES_MAX
#define UI_TASK_LCD_VER_RES   ILI9341_HOR_RES_MAX
#define UI_TASK_LCD_BUF_BYTES (UI_TASK_LCD_HOR_RES * CONFIG_UI_LVGL_BUF_LINES * sizeof(uint16_t))
#define UI_TASK_SPI_QUEUE     7

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static app_err_t ui_task_lcd_bus_init(void);
static app_err_t ui_task_lvgl_flush(void *p_context, const lv_area_t *p_area, const void *p_pixels);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* Display chain state lives for the whole application lifetime; LVGL and the SPI driver keep pointers to it. */
static spi_device_handle_t        s_spi_handle = NULL;
static ili9341_handle_t           s_lcd_handle = {0};
static lvgl_port_instance_ctrl_t  s_lvgl_ctrl  = {0};
static ui_service_instance_ctrl_t s_ui_ctrl    = {0};
static bool                       s_ui_ready   = false;

/* LV_COLOR_16_SWAP=1 is required: DEV_ILI9341_Flush() sends the low byte of each pixel first. */
static const lvgl_port_config_t s_lvgl_cfg = {
    .p_flush         = ui_task_lvgl_flush,
    .p_flush_context = &s_lvgl_ctrl,
    .hor_res         = UI_TASK_LCD_HOR_RES,
    .ver_res         = UI_TASK_LCD_VER_RES,
    .buf_lines       = CONFIG_UI_LVGL_BUF_LINES,
    .use_keypad      = true,
};
static const ui_service_config_t s_ui_cfg = {
    .p_lvgl = &s_lvgl_ctrl,
};

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Sets up the DC, RESET and backlight GPIOs and the SPI bus the ILI9341 driver expects from its caller.
 **********************************************************************************************************************/
static app_err_t ui_task_lcd_bus_init(void)
{
    const gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_UI_LCD_DC_GPIO) | (1ULL << CONFIG_UI_LCD_RST_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_cfg) != ESP_OK)
    {
        return APP_FAIL;
    }

#if CONFIG_UI_LCD_BL_GPIO >= 0
    /* DEV_ILI9341_EnableBacklight() is not implemented yet, so the backlight is switched on here. */
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_UI_LCD_BL_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
#if CONFIG_UI_LCD_BL_ACTIVE_HIGH
    const uint32_t bl_on_level = 1U;
#else
    const uint32_t bl_on_level = 0U;
#endif
    if ((gpio_config(&bl_cfg) != ESP_OK) || (gpio_set_level(CONFIG_UI_LCD_BL_GPIO, bl_on_level) != ESP_OK))
    {
        return APP_FAIL;
    }
#endif

    const spi_bus_config_t bus_cfg = {
        .miso_io_num     = -1,
        .mosi_io_num     = CONFIG_UI_LCD_MOSI_GPIO,
        .sclk_io_num     = CONFIG_UI_LCD_SCLK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = UI_TASK_LCD_BUF_BYTES,
    };
    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = CONFIG_UI_LCD_PCLK_HZ,
        .mode           = 0,
        .spics_io_num   = CONFIG_UI_LCD_CS_GPIO,
        .queue_size     = UI_TASK_SPI_QUEUE,
    };
    const spi_host_device_t host = (spi_host_device_t)CONFIG_UI_LCD_SPI_HOST;

    if (spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK)
    {
        return APP_FAIL;
    }
    if (spi_bus_add_device(host, &dev_cfg, &s_spi_handle) != ESP_OK)
    {
        (void)spi_bus_free(host);
        return APP_FAIL;
    }
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Adapts the LVGL flush request to the ILI9341 driver. The driver sends synchronously and calls lv_disp_flush_ready()
 * itself, so nothing is left to do after it returns.
 **********************************************************************************************************************/
static app_err_t ui_task_lvgl_flush(void *p_context, const lv_area_t *p_area, const void *p_pixels)
{
    lvgl_port_instance_ctrl_t *const p_lvgl = (lvgl_port_instance_ctrl_t *)p_context;

    DEV_ILI9341_Flush(&p_lvgl->disp_drv, p_area, (lv_color_t *)p_pixels);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

void boot_ui(void)
{
    app_err_t ret = ui_task_lcd_bus_init();
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(UI_TASK_TAG, "LCD GPIO/SPI init failed, running without a screen");
        return;
    }

    s_lcd_handle = (ili9341_handle_t) {
        .e_dc_pin     = (gpio_num_t)CONFIG_UI_LCD_DC_GPIO,
        .e_rst_pin    = (gpio_num_t)CONFIG_UI_LCD_RST_GPIO,
        .p_spi_Handle = &s_spi_handle,
    };
    DEV_ILI9341_Init(&s_lcd_handle);

    ret = lvgl_port_init(&s_lvgl_ctrl, &s_lvgl_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(UI_TASK_TAG, "lvgl_port_init failed with application error: %d", ret);
        return;
    }

    ret = ui_service_init(&s_ui_ctrl, &s_ui_cfg);
    if (ret != APP_SUCCESS)
    {
        ESP_LOGE(UI_TASK_TAG, "ui_service_init failed with application error: %d", ret);
        return;
    }
    s_ui_ready = true;
}

void ui_task_post_new_keys(uint16_t pressed_keys, uint16_t previous_keys)
{
    if (!s_ui_ready)
    {
        return;
    }

    const uint16_t new_keys = pressed_keys & (uint16_t)~previous_keys;

    for (uint8_t key_index = 0U; key_index < (KEYPAD_MAX_ROW_COUNT * KEYPAD_MAX_COLUMN_COUNT); key_index++)
    {
        if ((new_keys & (uint16_t)(1U << key_index)) != 0U)
        {
            (void)ui_service_post_keypad_char(&s_ui_ctrl, s_keypad_keymap[key_index]);
        }
    }
}

ui_service_instance_ctrl_t *ui_task_get_ui(void)
{
    return s_ui_ready ? &s_ui_ctrl : NULL;
}

#else /* UI_TASK_ENABLED */

void boot_ui(void)
{
}

void ui_task_post_new_keys(uint16_t pressed_keys, uint16_t previous_keys)
{
    (void)pressed_keys;
    (void)previous_keys;
}

struct st_ui_service_instance_ctrl *ui_task_get_ui(void)
{
    return NULL;
}

#endif /* UI_TASK_ENABLED */
