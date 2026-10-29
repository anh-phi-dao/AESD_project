#ifndef UI_SERVICE_H_
#define UI_SERVICE_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "app_err.h"
#include "lvgl_port.h"
#include "ui_service_config.h"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Configuration remains caller-owned and must stay valid and unchanged for the lifetime of the instance. */
typedef struct st_ui_service_config
{
    lvgl_port_instance_ctrl_t *p_lvgl; /* Initialized LVGL port. */
} ui_service_config_t;

/* The screens of design/squareline/SmartLock_UI.spj. Admin menus reuse Input, Menu and Status with other text.
 * Overlay is not listed: its TopBar and Popup are drawn above every screen. */
typedef enum e_ui_service_screen
{
    UI_SERVICE_SCREEN_HOME = 0,    /* Lock state, new messages, key hints. */
    UI_SERVICE_SCREEN_INPUT,       /* One text area: PIN, user ID, new PIN. */
    UI_SERVICE_SCREEN_VOICE,       /* Recording a voice message: progress bar and time. */
    UI_SERVICE_SCREEN_ADMIN_LOGIN, /* Admin ID and password, or admin card / fingerprint. */
    UI_SERVICE_SCREEN_MENU,        /* Title, count line and four numbered rows: admin menus. */
    UI_SERVICE_SCREEN_STATUS,      /* Icon, title, detail, progress bar: lockout, scanning. */
} ui_service_screen_t;

/* Popup color and icon. */
typedef enum e_ui_service_popup_level
{
    UI_SERVICE_POPUP_INFO = 0,
    UI_SERVICE_POPUP_SUCCESS,
    UI_SERVICE_POPUP_ERROR,
} ui_service_popup_level_t;

/* # pressed on a filled text area. INPUT: p_first is ui_InputField, p_second NULL. ADMIN_LOGIN: p_first is the ID,
 * p_second the password. Runs in the LVGL task holding the lock: copy the texts (they are only valid during the call),
 * hand them to your own task and return; check them there, locally. */
typedef void (*ui_service_submit_cb_t)(ui_service_screen_t screen,
                                       const char         *p_first,
                                       const char         *p_second,
                                       void               *p_context);

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_ui_service_instance_ctrl
{
    const ui_service_config_t *p_cfg;
    ui_service_submit_cb_t     p_submit_cb;
    void                      *p_submit_context;
    bool                       init;
} ui_service_instance_ctrl_t;

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Local screen UI. Builds the SquareLine Studio export in app/ui_service/ui/ (docs/tft_ui.md), or a built-in test
 * screen when nothing is exported yet. Shows state only: nothing here may open the door; lock_service decides that.
 * Full descriptions are beside the implementations in ui_service.c. */
#ifdef __cplusplus
extern "C"
{
#endif

    app_err_t ui_service_init(ui_service_instance_ctrl_t *const p_ctrl, const ui_service_config_t *const p_cfg);

    /* True when the build contains a SquareLine export, false when it shows the built-in test screen. */
    bool ui_service_has_squareline(void);

    /* Show a screen and give its first text area the keypad (on AdminLogin: the ID; # moves to the password, * on an
     * empty password back to the ID). Text areas are emptied, except that Home -> Input keeps the digit that left Home;
     * showing the screen already shown resets it (e.g. after a wrong PIN). Home, Voice, Menu and Status have no text
     * area: LVGL ignores keys there and the caller reads them from the keypad. The top bar stays above every screen; a
     * popup that is showing stays too. */
    app_err_t ui_service_show(ui_service_instance_ctrl_t *const p_ctrl, ui_service_screen_t screen);

    /* Called when # submits Input or AdminLogin; NULL stops it. See ui_service_submit_cb_t. */
    app_err_t ui_service_set_submit_cb(ui_service_instance_ctrl_t *const p_ctrl,
                                       ui_service_submit_cb_t            p_cb,
                                       void                             *p_context);

    /* Small box above the current screen, e.g. "Authentication failed" / "3 tries left", without changing screen.
     * Hides itself after duration_ms (0 = until ui_service_popup_hide()). Texts are copied; detail may be NULL. */
    app_err_t ui_service_popup(ui_service_instance_ctrl_t *const p_ctrl,
                               ui_service_popup_level_t          level,
                               const char                       *p_title,
                               const char                       *p_detail,
                               uint32_t                          duration_ms);

    app_err_t ui_service_popup_hide(ui_service_instance_ctrl_t *const p_ctrl);

    /* Feed one key of the 4x3 keypad ('0'-'9', '*', '#') to the focused LVGL object. Never blocks. */
    app_err_t ui_service_post_keypad_char(ui_service_instance_ctrl_t *const p_ctrl, char key);

    /* Hold while changing SquareLine objects (ui_*) from another task, e.g. lv_label_set_text(ui_HomeState, ...). */
    app_err_t ui_service_lock(ui_service_instance_ctrl_t *const p_ctrl, uint32_t timeout_ms);

    void ui_service_unlock(ui_service_instance_ctrl_t *const p_ctrl);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_H_ */
