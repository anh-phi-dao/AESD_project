idf_component_register(SRCS "ns4168_speaker.c" "inm441_mic.c" "audio_helper.c"
                       INCLUDE_DIRS "."
                       REQUIRES util driver freertos)