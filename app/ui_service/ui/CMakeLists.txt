SET(SOURCES screens/ui_Home.c
    screens/ui_Pin.c
    screens/ui_Granted.c
    screens/ui_Denied.c
    screens/ui_Voice.c
    screens/ui_VoiceDone.c
    screens/ui_Alert.c
    screens/ui_LockDoor.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
