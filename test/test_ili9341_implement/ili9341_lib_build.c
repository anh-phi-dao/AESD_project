/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "ili9341.h"
#include "spi.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define ILI9341_TEST_TAG        "ILI9341_TEST"

/* SPI and control GPIO pins from ESP32-S3-WROOM-1 schematic */
#define LCD_PIN_CS              GPIO_NUM_10
#define LCD_PIN_MOSI            GPIO_NUM_11
#define LCD_PIN_SCK             GPIO_NUM_12
#define LCD_PIN_DC              GPIO_NUM_13
#define LCD_PIN_RST             GPIO_NUM_14

/* Landscape resolution (320x240) and buffer limit (40 lines = 12800 pixels) */
#define LCD_WIDTH               320
#define LCD_HEIGHT              240
#define LCD_BUF_LINES           40
#define LCD_BUF_PIXELS          (LCD_WIDTH * LCD_BUF_LINES)

/* Set to 1 to fix RGB565 byte-order inversion caused by ili9341_send_color() */
#define LCD_SWAP_RGB565_BYTES   1

#define LVGL_TICK_PERIOD_MS     2
#define UI_ANIM_PERIOD_MS       50

/* Automatically select the largest enabled Montserrat font in sdkconfig */
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
#define UI_FONT_LARGE           (&lv_font_montserrat_24)
#elif defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
#define UI_FONT_LARGE           (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
#define UI_FONT_LARGE           (&lv_font_montserrat_18)
#elif defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
#define UI_FONT_LARGE           (&lv_font_montserrat_16)
#else
#define UI_FONT_LARGE           LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
#define UI_FONT_MEDIUM          (&lv_font_montserrat_16)
#else
#define UI_FONT_MEDIUM          LV_FONT_DEFAULT
#endif

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Enumeration of simulated smart lock states for visual demonstration. */
typedef enum e_lock_ui_state
{
    LOCK_UI_STATE_LOCKED = 0,
    LOCK_UI_STATE_VERIFYING,
    LOCK_UI_STATE_UNLOCKED,
    LOCK_UI_STATE_COUNT
} lock_ui_state_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void ili9341_test_gpio_init(void);
static void ili9341_test_spi_init(void);
static void ili9341_test_flush_cb(lv_disp_drv_t *p_drv, const lv_area_t *p_area, lv_color_t *p_color_map);
static void ili9341_test_create_dashboard_ui(void);
static void ili9341_test_apply_state_theme(lock_ui_state_t state);
static void ili9341_test_ui_timer_cb(lv_timer_t *p_timer);
static void lvgl_tick_cb(void *p_arg);

/***********************************************************************************************************************
 * Global Variables
 **********************************************************************************************************************/

static spi_device_handle_t s_spi_handle;
static ili9341_handle_t    s_ili9341_handle;
static lv_disp_draw_buf_t  s_disp_buf;
static lv_disp_drv_t       s_disp_drv;
static lv_color_t          s_buf1[LCD_BUF_PIXELS];

/* LVGL UI widget pointers */
static lv_obj_t           *s_p_card_panel    = NULL;
static lv_obj_t           *s_p_state_badge   = NULL;
static lv_obj_t           *s_p_arc           = NULL;
static lv_obj_t           *s_p_percent_label = NULL;
static lv_obj_t           *s_p_state_label   = NULL;
static lv_obj_t           *s_p_sub_label     = NULL;
static lv_obj_t           *s_p_uptime_label  = NULL;
static lv_obj_t           *s_p_bottom_bar    = NULL;

/* Runtime state for animations */
static lock_ui_state_t     s_current_state   = LOCK_UI_STATE_LOCKED;
static uint16_t            s_progress_val    = 0;
static uint32_t            s_tick_counter    = 0;
static uint32_t            s_uptime_seconds  = 0;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Application entry point for testing the ILI9341 display with a high-contrast LVGL dashboard.
 **********************************************************************************************************************/
void app_main(void)
{
    ESP_LOGI(ILI9341_TEST_TAG, "Starting ILI9341 LVGL high-contrast dashboard...");

    /* 1. Initialize DC and RST control GPIOs */
    ili9341_test_gpio_init();

    /* 2. Initialize SPI bus and attach the ILI9341 device */
    ili9341_test_spi_init();

    /* 3. Initialize the ILI9341 controller */
    s_ili9341_handle.e_dc_pin     = LCD_PIN_DC;
    s_ili9341_handle.e_rst_pin    = LCD_PIN_RST;
    s_ili9341_handle.p_spi_Handle = &s_spi_handle;

    DEV_ILI9341_Init(&s_ili9341_handle);
    DEV_ILI9341_EnableBacklight(true);

    /* 4. Initialize LVGL core and register display driver */
    lv_init();
    lv_disp_draw_buf_init(&s_disp_buf, s_buf1, NULL, LCD_BUF_PIXELS);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_WIDTH;
    s_disp_drv.ver_res  = LCD_HEIGHT;
    s_disp_drv.flush_cb = ili9341_test_flush_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    lv_disp_drv_register(&s_disp_drv);

    /* 5. Start periodic esp_timer for LVGL tick timekeeping */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lvgl_tick_cb,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LVGL_TICK_PERIOD_MS * 1000));

    /* 6. Build the high-contrast Smart Lock dashboard user interface */
    ili9341_test_create_dashboard_ui();

    /* 7. Create an LVGL timer to animate UI widgets periodically */
    lv_timer_create(ili9341_test_ui_timer_cb, UI_ANIM_PERIOD_MS, NULL);

    ESP_LOGI(ILI9341_TEST_TAG, "Dashboard UI ready.");

    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/***********************************************************************************************************************
 * Private function
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Configures the DC and RST GPIO pins as push-pull outputs.
 **********************************************************************************************************************/
static void ili9341_test_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

/***********************************************************************************************************************
 * Initializes the SPI2 bus and adds the ILI9341 device handle.
 **********************************************************************************************************************/
static void ili9341_test_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num     = -1,
        .mosi_io_num     = LCD_PIN_MOSI,
        .sclk_io_num     = LCD_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_BUF_PIXELS * sizeof(uint16_t),
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26 * 1000 * 1000, /* 26 MHz for clean signal integrity */
        .mode           = 0,
        .spics_io_num   = LCD_PIN_CS,
        .queue_size     = 7,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle));
}

/***********************************************************************************************************************
 * Wrapper flush callback that corrects RGB565 byte ordering before invoking DEV_ILI9341_Flush.
 **********************************************************************************************************************/
static void ili9341_test_flush_cb(lv_disp_drv_t *p_drv, const lv_area_t *p_area, lv_color_t *p_color_map)
{
#if (LCD_SWAP_RGB565_BYTES == 1) && (LV_COLOR_16_SWAP == 0)
    uint32_t  pixel_count = lv_area_get_width(p_area) * lv_area_get_height(p_area);
    uint16_t *p_pixels    = (uint16_t *)p_color_map;

    for (uint32_t i = 0; i < pixel_count; i++)
    {
        p_pixels[i] = __builtin_bswap16(p_pixels[i]);
    }
#endif

    DEV_ILI9341_Flush(p_drv, p_area, p_color_map);
}

/***********************************************************************************************************************
 * Builds a bold, high-contrast Smart Lock dashboard layout optimized for readability.
 **********************************************************************************************************************/
static void ili9341_test_create_dashboard_ui(void)
{
    lv_obj_t *p_scr = lv_scr_act();
    lv_obj_set_style_bg_color(p_scr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(p_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Bold Top Header Bar */
    lv_obj_t *p_header = lv_obj_create(p_scr);
    lv_obj_set_size(p_header, 320, 38);
    lv_obj_align(p_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(p_header, lv_color_hex(0x1D3557), LV_PART_MAIN);
    lv_obj_set_style_border_width(p_header, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(p_header, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *p_title = lv_label_create(p_header);
    lv_label_set_text(p_title, LV_SYMBOL_HOME " AESD LOCK SYSTEM");
    lv_obj_set_style_text_font(p_title, UI_FONT_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_color(p_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(p_title, 1, LV_PART_MAIN);
    lv_obj_align(p_title, LV_ALIGN_LEFT_MID, 4, 0);

    s_p_uptime_label = lv_label_create(p_header);
    lv_label_set_text(s_p_uptime_label, "00:00 " LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(s_p_uptime_label, UI_FONT_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_p_uptime_label, lv_color_hex(0xFFD166), LV_PART_MAIN);
    lv_obj_align(s_p_uptime_label, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 2. Main Center Dashboard Card with Thick Border */
    s_p_card_panel = lv_obj_create(p_scr);
    lv_obj_set_size(s_p_card_panel, 308, 160);
    lv_obj_align(s_p_card_panel, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_color(s_p_card_panel, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_p_card_panel, lv_color_hex(0xFF0055), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_p_card_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_p_card_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_p_card_panel, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_p_card_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. Thick Circular Arc Gauge on the Left */
    s_p_arc = lv_arc_create(s_p_card_panel);
    lv_obj_set_size(s_p_arc, 120, 120);
    lv_arc_set_rotation(s_p_arc, 135);
    lv_arc_set_bg_angles(s_p_arc, 0, 270);
    lv_arc_set_range(s_p_arc, 0, 100);
    lv_arc_set_value(s_p_arc, 0);
    lv_obj_remove_style(s_p_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_p_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_p_arc, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_p_arc, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_p_arc, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_align(s_p_arc, LV_ALIGN_LEFT_MID, 4, 0);

    s_p_percent_label = lv_label_create(s_p_arc);
    lv_label_set_text(s_p_percent_label, "0%");
    lv_obj_set_style_text_font(s_p_percent_label, UI_FONT_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_p_percent_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_p_percent_label, 1, LV_PART_MAIN);
    lv_obj_center(s_p_percent_label);

    /* 4. High-Contrast Solid Badge for State Title on the Right */
    s_p_state_badge = lv_obj_create(s_p_card_panel);
    lv_obj_set_size(s_p_state_badge, 156, 42);
    lv_obj_align(s_p_state_badge, LV_ALIGN_TOP_RIGHT, -2, 8);
    lv_obj_set_style_bg_color(s_p_state_badge, lv_color_hex(0xFF0055), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_p_state_badge, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_p_state_badge, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_radius(s_p_state_badge, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_p_state_badge, LV_OBJ_FLAG_SCROLLABLE);

    s_p_state_label = lv_label_create(s_p_state_badge);
    lv_label_set_text(s_p_state_label, "LOCKED");
    lv_obj_set_style_text_font(s_p_state_label, UI_FONT_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_p_state_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_p_state_label, 1, LV_PART_MAIN);
    lv_obj_center(s_p_state_label);

    /* 5. Clear Subtitle Box */
    s_p_sub_label = lv_label_create(s_p_card_panel);
    lv_label_set_long_mode(s_p_sub_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_p_sub_label, 156);
    lv_label_set_text(s_p_sub_label, "SCAN RFID CARD\nOR ENTER PIN");
    lv_obj_set_style_text_font(s_p_sub_label, UI_FONT_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_p_sub_label, lv_color_hex(0xFFD166), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_p_sub_label, 1, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_p_sub_label, 6, LV_PART_MAIN);
    lv_obj_align(s_p_sub_label, LV_ALIGN_BOTTOM_RIGHT, -2, -12);

    /* 6. Thick Bottom Progress Bar */
    s_p_bottom_bar = lv_bar_create(p_scr);
    lv_obj_set_size(s_p_bottom_bar, 308, 18);
    lv_obj_align(s_p_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_bar_set_range(s_p_bottom_bar, 0, 100);
    lv_bar_set_value(s_p_bottom_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_p_bottom_bar, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_p_bottom_bar, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_p_bottom_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_p_bottom_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_p_bottom_bar, 4, LV_PART_INDICATOR);

    /* Apply initial state theme */
    ili9341_test_apply_state_theme(LOCK_UI_STATE_LOCKED);
}

/***********************************************************************************************************************
 * Updates colors and status texts according to the active simulated lock state.
 **********************************************************************************************************************/
static void ili9341_test_apply_state_theme(lock_ui_state_t state)
{
    lv_color_t theme_color;

    switch (state)
    {
        case LOCK_UI_STATE_LOCKED:
            theme_color = lv_color_hex(0xE63946); /* High-contrast Red */
            lv_label_set_text(s_p_state_label, LV_SYMBOL_CLOSE " LOCKED");
            lv_label_set_text(s_p_sub_label, "SYSTEM ARMED\nPRESENT RFID TAG");
            break;

        case LOCK_UI_STATE_VERIFYING:
            theme_color = lv_color_hex(0x0096C7); /* High-contrast Cyan/Blue */
            lv_label_set_text(s_p_state_label, LV_SYMBOL_REFRESH " CHECKING");
            lv_label_set_text(s_p_sub_label, "READING KEY...\nPLEASE WAIT");
            break;

        case LOCK_UI_STATE_UNLOCKED:
        default:
            theme_color = lv_color_hex(0x06D6A0); /* High-contrast Emerald Green */
            lv_label_set_text(s_p_state_label, LV_SYMBOL_OK " OPENED");
            lv_label_set_text(s_p_sub_label, "ACCESS GRANTED\nWELCOME HOME!");
            break;
    }

    lv_obj_set_style_border_color(s_p_card_panel, theme_color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_p_state_badge, theme_color, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_p_arc, theme_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_p_bottom_bar, theme_color, LV_PART_INDICATOR);
}

/***********************************************************************************************************************
 * LVGL periodic timer callback that drives the gauge animation, state transitions, and uptime clock.
 **********************************************************************************************************************/
static void ili9341_test_ui_timer_cb(lv_timer_t *p_timer)
{
    (void)p_timer;
    char buf[32];

    /* Advance progress value */
    s_progress_val += 2;
    if (s_progress_val > 100)
    {
        s_progress_val  = 0;
        s_current_state = (lock_ui_state_t)((s_current_state + 1) % LOCK_UI_STATE_COUNT);
        ili9341_test_apply_state_theme(s_current_state);
    }

    /* Update arc, percentage text, and bottom bar */
    lv_arc_set_value(s_p_arc, s_progress_val);
    lv_bar_set_value(s_p_bottom_bar, s_progress_val, LV_ANIM_OFF);

    snprintf(buf, sizeof(buf), "%u%%", (unsigned int)s_progress_val);
    lv_label_set_text(s_p_percent_label, buf);

    /* Update uptime clock every 1 second */
    s_tick_counter++;
    if (s_tick_counter >= (1000 / UI_ANIM_PERIOD_MS))
    {
        s_tick_counter = 0;
        s_uptime_seconds++;

        uint32_t mins = (s_uptime_seconds / 60) % 60;
        uint32_t secs = s_uptime_seconds % 60;
        snprintf(buf, sizeof(buf), "%02lu:%02lu " LV_SYMBOL_BATTERY_FULL,
                 (unsigned long)mins, (unsigned long)secs);
        lv_label_set_text(s_p_uptime_label, buf);
    }
}

/***********************************************************************************************************************
 * Periodic esp_timer callback to increment the internal LVGL tick counter.
 **********************************************************************************************************************/
static void lvgl_tick_cb(void *p_arg)
{
    (void)p_arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}