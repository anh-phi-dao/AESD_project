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

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_ui_service_instance_ctrl
{
    const ui_service_config_t *p_cfg;
    bool                       init;
} ui_service_instance_ctrl_t;

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Local screen UI. Builds the SquareLine Studio exports in app/ui_service/ui/ (user screens) and ui_admin/ (admin
 * screens, docs/tft_ui.md), or a built-in test screen when nothing is exported yet. Shows state only: nothing here may open the door; lock_service decides that.
 * Full descriptions are beside the implementations in ui_service.c. */
#ifdef __cplusplus
extern "C"
{
#endif

    app_err_t ui_service_init(ui_service_instance_ctrl_t *const p_ctrl, const ui_service_config_t *const p_cfg);

    /* True when the build contains a SquareLine export, false when it shows the built-in test screen. */
    bool ui_service_has_squareline(void);

    /* True when the build also contains the admin export (app/ui_service/ui_admin/). */
    bool ui_service_has_admin_ui(void);

    /* Switch between the two SquareLine projects. Screens inside one project change through its own events; these
     * cross from one project to the other, which SquareLine events cannot do. Both take the LVGL lock themselves. */
    app_err_t ui_service_show_admin(ui_service_instance_ctrl_t *const p_ctrl);
    app_err_t ui_service_show_home(ui_service_instance_ctrl_t *const p_ctrl);

    /* Feed one key of the 4x3 keypad ('0'-'9', '*', '#') to the focused LVGL object. Never blocks. */
    app_err_t ui_service_post_keypad_char(ui_service_instance_ctrl_t *const p_ctrl, char key);

    /* Hold while changing SquareLine objects (ui_*) from another task, e.g. lv_label_set_text(ui_LabelStatus, ...). */
    app_err_t ui_service_lock(ui_service_instance_ctrl_t *const p_ctrl, uint32_t timeout_ms);

    void ui_service_unlock(ui_service_instance_ctrl_t *const p_ctrl);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_H_ */
