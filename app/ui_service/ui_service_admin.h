#ifndef UI_SERVICE_ADMIN_H_
#define UI_SERVICE_ADMIN_H_

/* Private to ui_service: the only code that sees the admin export (app/ui_service/ui_admin/).
 * ui/ui.h and ui_admin/ui.h cannot be included in the same .c file: each export has its own ui_helpers.h include
 * guard, so ui_anim_user_data_t would be defined twice. ui_service.c includes ui/ui.h, ui_service_admin.c the other.
 * Call with the LVGL lock held. */

void ui_service_admin_open(void);

#endif /* UI_SERVICE_ADMIN_H_ */
