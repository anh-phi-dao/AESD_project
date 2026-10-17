/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <string.h>

#include "lvgl.h"
#include "ui_service_config.h"
#include "ui_service_fallback.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define UI_FALLBACK_BG_COLOR       0x101820U
#define UI_FALLBACK_ACCENT_COLOR   0x2DB6A3U
#define UI_FALLBACK_TEXT_COLOR     0xE8ECEFU
#define UI_FALLBACK_MUTED_COLOR    0x8A96A0U
#define UI_FALLBACK_PIN_MAX_LEN    8U

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void ui_fallback_pin_ready_cb(lv_event_t *p_event);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static lv_obj_t *s_p_status = NULL;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Test screen: proves the panel, colors, orientation and keypad input work before any SquareLine design exists. The
 * PIN box only echoes keys; it checks nothing and opens nothing.
 **********************************************************************************************************************/
void ui_init(void)
{
    lv_obj_t *p_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(p_scr, lv_color_hex(UI_FALLBACK_BG_COLOR), 0);
    lv_obj_set_style_text_color(p_scr, lv_color_hex(UI_FALLBACK_TEXT_COLOR), 0);
    lv_obj_set_flex_flow(p_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p_scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p_scr, 10, 0);
    lv_obj_clear_flag(p_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *p_title = lv_label_create(p_scr);
    lv_label_set_text(p_title, LV_SYMBOL_HOME "  SMART LOCK");
    lv_obj_set_style_text_color(p_title, lv_color_hex(UI_FALLBACK_ACCENT_COLOR), 0);

    lv_obj_t *p_hint = lv_label_create(p_scr);
    lv_label_set_text(p_hint, "No SquareLine UI exported yet\nsee docs/tft_ui.md");
    lv_obj_set_style_text_align(p_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(p_hint, lv_color_hex(UI_FALLBACK_MUTED_COLOR), 0);

    /* Joins the keypad's default group on creation, so digits typed on the keypad land here. */
    lv_obj_t *p_pin = lv_textarea_create(p_scr);
    lv_textarea_set_one_line(p_pin, true);
    lv_textarea_set_password_mode(p_pin, true);
    lv_textarea_set_max_length(p_pin, UI_FALLBACK_PIN_MAX_LEN);
    lv_textarea_set_accepted_chars(p_pin, "0123456789");
    lv_textarea_set_placeholder_text(p_pin, "Keypad test");
    lv_obj_set_width(p_pin, 180);
    lv_obj_add_event_cb(p_pin, ui_fallback_pin_ready_cb, LV_EVENT_READY, NULL);

    s_p_status = lv_label_create(p_scr);
    lv_label_set_text_fmt(
        s_p_status, "%c = Enter    %c = Delete", UI_SERVICE_KEY_CHAR_ENTER, UI_SERVICE_KEY_CHAR_BACKSPACE);
    lv_obj_set_style_text_color(s_p_status, lv_color_hex(UI_FALLBACK_MUTED_COLOR), 0);

    /* Red, green and blue swatches: wrong byte order or BGR shows up here at a glance. */
    lv_obj_t *p_row = lv_obj_create(p_scr);
    lv_obj_remove_style_all(p_row);
    lv_obj_set_size(p_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p_row, 8, 0);

    static const uint32_t swatch_colors[] = {0xFF0000U, 0x00FF00U, 0x0000FFU};
    for (uint32_t i = 0U; i < (sizeof(swatch_colors) / sizeof(swatch_colors[0])); i++)
    {
        lv_obj_t *p_swatch = lv_obj_create(p_row);
        lv_obj_remove_style_all(p_swatch);
        lv_obj_set_size(p_swatch, 40, 16);
        lv_obj_set_style_bg_opa(p_swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(p_swatch, lv_color_hex(swatch_colors[i]), 0);
        lv_obj_set_style_radius(p_swatch, 3, 0);
    }

    lv_disp_load_scr(p_scr);
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/* Enter on the PIN box: report the length only, never the digits, then clear it. */
static void ui_fallback_pin_ready_cb(lv_event_t *p_event)
{
    lv_obj_t *const p_pin = lv_event_get_target(p_event);
    const size_t    len   = strlen(lv_textarea_get_text(p_pin));

    lv_label_set_text_fmt(s_p_status, "Received %u digit(s)", (unsigned)len);
    lv_textarea_set_text(p_pin, "");
}
