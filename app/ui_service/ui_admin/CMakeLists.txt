SET(SOURCES screens/ui_AdminLogin.c
    screens/ui_AdminMenu.c
    screens/ui_AdminPin.c
    screens/ui_AdminFinger.c
    screens/ui_AdminScan.c
    screens/ui_AdminDone.c
    screens/ui_AdminInput.c
    screens/ui_AdminCard.c
    screens/ui_AdminFail.c
    screens/ui_AdminAlert.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
