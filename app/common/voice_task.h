#ifndef VOICE_TASK_H
#define VOICE_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VOICE_TASK_TAG      "VOICE_TASK"
#define VOICE_TASK_STACK    5120U
#define VOICE_TASK_PRIORITY 2U /* Above audio_task: takes the speaker as soon as a key beep ends. */

/* Topics below "lock/<device_id>/", see docs/mqtt_protocol.md. */
#define VOICE_TOPIC_IN    "voice/in"    /* web -> door: a message from the owner for guests. */
#define VOICE_TOPIC_OUT   "voice/out"   /* door -> web: the audio of a guest's message. */
#define VOICE_TOPIC_INDEX "voice/index" /* door -> web, retained: the guests' messages kept on the SD card. */
#define VOICE_TOPIC_REQ   "voice/req"   /* web -> door: {"op":"get"|"del","id":n}, one of those messages. */

#define VOICE_CHUNK_HEADER_LEN 16U
#define VOICE_CHUNK_DATA_MAX   4096U
#define VOICE_IN_MAX_LEN       (VOICE_CHUNK_HEADER_LEN + VOICE_CHUNK_DATA_MAX) /* Largest voice/in payload. */
#define VOICE_REQ_MAX_LEN      64U                                             /* Largest voice/req payload. */

/***********************************************************************************************************************
 * Voice messages, kept on the SD card in /VOICE:
 *   - for the owner: what guests record at the door (hold * on Home). Sent to the web on voice/out and listed in the
 *     retained voice/index, so the owner gets them even when the page was closed. Never played at the door.
 *   - for guests: what the owner sends from the web (voice/in) or records in the admin menu. Played at the door.
 * One task does all recording, playing and sending, so the I2S bus and the files have one user.
 *
 * Call after boot_storage() (mounts the card) and boot_audio(). Without a card it tells system_control that messages
 * cannot be kept, and the rest of the lock works as before.
 **********************************************************************************************************************/
void boot_voice(void);

/* system_control hooks. They only queue the work and never block. */
void voice_task_record_start(bool owner); /* owner: a message for guests, else one for the owner. */
void voice_task_record_stop(bool keep);   /* keep: for the review. Not keep: drop it, recording or under review. */
void voice_task_record_send(void);        /* Review done: keep the recording (a guest's goes to the web). */
void voice_task_play(uint8_t slot, uint8_t token); /* slot SYSTEM_PLAY_RECORDING: the recording under review. */
void voice_task_play_stop(void);
void voice_task_delete_owner(uint8_t slot);

/* From the MQTT task: a message on VOICE_TOPIC_IN or VOICE_TOPIC_REQ. */
void voice_task_mqtt_rx(const char *p_suffix, const uint8_t *p_data, size_t len);

/* From the MQTT task, on every broker connection: publish voice/index again. */
void voice_task_mqtt_connected(void);

#endif
