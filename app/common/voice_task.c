#include "voice_task.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "audio_task.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "inm441_mic.h"
#include "mqtt_conn.h"
#include "network_task.h"
#include "ns4168_speaker.h"
#include "sd_card.h"
#include "system_control.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define VOICE_DIR         SD_CARD_MOUNT_POINT "/VOICE"
#define VOICE_FILE_REC    VOICE_DIR "/REC.TMP" /* Being recorded. */
#define VOICE_FILE_IN     VOICE_DIR "/IN.TMP"  /* Arriving from the web. */
#define VOICE_PATH_SIZE   32U
#define VOICE_GUEST_SLOTS 10U /* Guests' messages kept for the web (G0..G9.PCM); the oldest is replaced. */
#define VOICE_RATE        16000U
#define VOICE_BYTES_PER_S (VOICE_RATE * 2U) /* 16-bit mono. */
#define VOICE_MIN_BYTES   VOICE_BYTES_PER_S /* A recording under 1 s is dropped. */
#define VOICE_REC_MAX_BYTES                                                                  \
    ((VOICE_BYTES_PER_S / 1000U) * (SYSTEM_VOICE_MAX_MS + 500U)) /* system_control stops it. \
                                                                  */
#define VOICE_IN_MAX_BYTES   (VOICE_BYTES_PER_S * 20U) /* The web sends 15 s at most, the protocol allows ~20 s. */
#define VOICE_IN_TIMEOUT_MS  30000U                    /* A message from the web not complete by then is dropped. */
#define VOICE_MIC_GAIN       4                         /* The INMP441 is quiet. */
#define VOICE_MIC_TIMEOUT_MS 500U
#define VOICE_LOCK_WAIT_MS   4000U /* A key beep lasts up to 3 s. */
#define VOICE_QUEUE_LEN      8U
#define VOICE_LATER_MAX      4U
#define VOICE_OUTBOX_MAX     (2U * VOICE_IN_MAX_LEN) /* Unacknowledged bytes before the next chunk waits. */
#define VOICE_OUTBOX_WAIT_MS 5000U
#define VOICE_PLAY_SAMPLES   256U /* In s_buf: 512 B mono read, then 1 KB stereo. */
#define VOICE_SPK_TIMEOUT_MS 200U
#define VOICE_TAIL_MS        150U /* The last DMA buffers still play after the last write. */
#define VOICE_INDEX_SIZE     640U
#define VOICE_TS_VALID       1700000000 /* Before this the clock was not set by SNTP. */
#define VOICE_SLOT_NONE      (-1)

/* Chunk header of docs/mqtt_protocol.md, little-endian. */
#define VOICE_CHUNK_MAGIC0  'V'
#define VOICE_CHUNK_MAGIC1  'M'
#define VOICE_CHUNK_VERSION 1U
#define VOICE_CODEC_PCM16   0U

#define VOICE_FILE_MAGIC0  'V'
#define VOICE_FILE_MAGIC1  'F'
#define VOICE_FILE_VERSION 1U

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* First 16 bytes of every message file, then 16-bit mono PCM at VOICE_RATE. The length is the file size. */
typedef struct __attribute__((packed)) st_voice_file_header
{
    uint8_t  magic[2];
    uint8_t  version;
    uint8_t  source; /* system_msg_source_t. */
    uint32_t id;     /* Random: the web keeps messages by id, and ids must not repeat after a restart. */
    uint32_t seq;    /* Order of arrival on this lock: newest first, oldest replaced. */
    uint32_t ts;     /* Unix time, 0 without SNTP. */
} voice_file_header_t;

_Static_assert(sizeof(voice_file_header_t) == 16U, "voice file header must be 16 bytes");

typedef struct st_voice_entry
{
    bool     used;
    uint8_t  source;
    uint32_t id;
    uint32_t seq;
    uint32_t ts;
    uint32_t bytes; /* Audio only, without the header. */
} voice_entry_t;

/* Local commands come from the keys at the door and end whatever the task is doing; the others come from the web. */
typedef enum e_voice_cmd_type
{
    VOICE_CMD_REC_START = 0, /* arg: owner. */
    VOICE_CMD_REC_STOP,      /* arg: keep (for the review). */
    VOICE_CMD_REC_SEND,      /* Review done: keep it. */
    VOICE_CMD_PLAY,          /* arg: slot, id: token. */
    VOICE_CMD_PLAY_STOP,
    VOICE_CMD_DELETE_OWNER, /* arg: slot. */
    VOICE_CMD_GET,          /* id: guest message to send. */
    VOICE_CMD_DELETE_GUEST, /* id. */
    VOICE_CMD_INDEX,
} voice_cmd_type_t;

typedef struct st_voice_cmd
{
    uint8_t  type;
    uint8_t  arg;
    uint32_t id;
} voice_cmd_t;

/* A message arriving on voice/in. MQTT task only. */
typedef struct st_voice_in
{
    FILE               *p_file;
    voice_file_header_t header;
    uint32_t            msg_id;
    uint16_t            next_seq;
    uint16_t            total;
    uint32_t            bytes;
    TickType_t          start;
} voice_in_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void     voice_task(void *pvParameter);
static void     voice_handle(const voice_cmd_t *const p_cmd);
static void     voice_send(uint8_t type, uint8_t arg, uint32_t id);
static bool     voice_poll_local(voice_cmd_t *const p_cmd);
static void     voice_record(bool owner);
static void     voice_record_send(void);
static void     voice_record_drop(void);
static void     voice_play(uint8_t slot, uint8_t token);
static bool     voice_upload(uint8_t slot);
static bool     voice_wait_outbox(mqtt_conn_instance_ctrl_t *const p_mqtt);
static void     voice_publish_index(void);
static void     voice_publish_owner_list(void);
static void     voice_delete_owner(uint8_t slot);
static void     voice_delete_guest(uint32_t id);
static int      voice_find_guest(uint32_t id);
static int      voice_commit(bool owner, const char *p_src, const voice_file_header_t *const p_header, uint32_t bytes);
static void     voice_header_init(voice_file_header_t *const p_header, uint8_t source, uint32_t id);
static void     voice_scan(void);
static bool     voice_load(const char *p_path, voice_entry_t *const p_entry);
static void     voice_path(char *p_out, bool owner, uint8_t slot);
static void     voice_in_chunk(const uint8_t *p_data, size_t len);
static void     voice_in_drop(void);
static void     voice_req(const uint8_t *p_data, size_t len);
static uint16_t voice_get16(const uint8_t *p);
static uint32_t voice_get32(const uint8_t *p);
static void     voice_put16(uint8_t *p, uint16_t value);
static void     voice_put32(uint8_t *p, uint32_t value);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static QueueHandle_t     s_queue = NULL;
static SemaphoreHandle_t s_mutex = NULL; /* s_owner, s_guest, s_next_seq, s_playing: the MQTT task adds messages. */
static bool              s_ready = false;

static voice_entry_t s_owner[SYSTEM_MSG_SLOTS]; /* For guests: O0..O4.PCM. */
static voice_entry_t s_guest[VOICE_GUEST_SLOTS];
static uint32_t      s_next_seq = 1U;
static int           s_playing  = VOICE_SLOT_NONE; /* Owner slot being played: a message from the web skips it. */

/* Voice task only. */
static voice_cmd_t s_later[VOICE_LATER_MAX]; /* Web requests that came during a recording, playback or upload. */
static uint8_t     s_later_count = 0U;
static voice_cmd_t s_next; /* A key command that ended the last activity, handled next. */
static bool        s_has_next = false;
static uint8_t     s_buf[VOICE_IN_MAX_LEN] __attribute__((aligned(4)));
static char        s_index[VOICE_INDEX_SIZE];

static voice_in_t s_in = {0};

/* The recording under review, in REC.TMP. Voice task only. */
static bool                s_pending = false;
static bool                s_pending_owner;
static voice_file_header_t s_pending_header;
static uint32_t            s_pending_bytes;

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

void boot_voice(void)
{
    struct stat st;

    s_mutex = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(VOICE_QUEUE_LEN, sizeof(voice_cmd_t));
    if ((NULL == s_mutex) || (NULL == s_queue))
    {
        ESP_LOGE(VOICE_TASK_TAG, "No memory for voice messages");
        return;
    }

    if (sd_card_is_mounted())
    {
        (void)mkdir(VOICE_DIR, 0775);
        s_ready = (0 == stat(VOICE_DIR, &st)) && S_ISDIR(st.st_mode);
    }
    if (s_ready)
    {
        (void)remove(VOICE_FILE_REC); /* Left by a power cut. */
        (void)remove(VOICE_FILE_IN);
        voice_scan();
    }
    else
    {
        ESP_LOGW(VOICE_TASK_TAG, "No SD card: voice messages cannot be kept");
    }

    if (pdPASS != xTaskCreatePinnedToCore(voice_task, "voice", VOICE_TASK_STACK, NULL, VOICE_TASK_PRIORITY, NULL, 0))
    {
        ESP_LOGE(VOICE_TASK_TAG, "Failed to create the voice task");
        s_ready = false;
    }
    voice_publish_owner_list();
    voice_send(VOICE_CMD_INDEX, 0U, 0U); /* MQTT may have connected before this ran, while the queue did not exist. */
}

void voice_task_record_start(bool owner)
{
    voice_send(VOICE_CMD_REC_START, owner ? 1U : 0U, 0U);
}

void voice_task_record_stop(bool keep)
{
    voice_send(VOICE_CMD_REC_STOP, keep ? 1U : 0U, 0U);
}

void voice_task_record_send(void)
{
    voice_send(VOICE_CMD_REC_SEND, 0U, 0U);
}

void voice_task_play(uint8_t slot, uint8_t token)
{
    voice_send(VOICE_CMD_PLAY, slot, token);
}

void voice_task_play_stop(void)
{
    voice_send(VOICE_CMD_PLAY_STOP, 0U, 0U);
}

void voice_task_delete_owner(uint8_t slot)
{
    voice_send(VOICE_CMD_DELETE_OWNER, slot, 0U);
}

void voice_task_mqtt_rx(const char *p_suffix, const uint8_t *p_data, size_t len)
{
    if ((NULL == p_suffix) || (NULL == p_data) || !s_ready)
    {
        return;
    }
    if (0 == strcmp(p_suffix, VOICE_TOPIC_IN))
    {
        voice_in_chunk(p_data, len);
    }
    else if (0 == strcmp(p_suffix, VOICE_TOPIC_REQ))
    {
        voice_req(p_data, len);
    }
}

void voice_task_mqtt_connected(void)
{
    voice_send(VOICE_CMD_INDEX, 0U, 0U);
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * One command at a time. A key command that cut the last activity short comes first, then what is queued, then the
 * web requests that waited.
 **********************************************************************************************************************/
static void voice_task(void *pvParameter)
{
    (void)pvParameter;
    voice_cmd_t cmd;

    while (1)
    {
        if (s_has_next)
        {
            cmd        = s_next;
            s_has_next = false;
        }
        else if (pdTRUE == xQueueReceive(s_queue, &cmd, 0))
        {
        }
        else if (0U != s_later_count)
        {
            cmd = s_later[0];
            s_later_count--;
            memmove(&s_later[0], &s_later[1], s_later_count * sizeof(s_later[0]));
        }
        else if (pdTRUE != xQueueReceive(s_queue, &cmd, portMAX_DELAY))
        {
            continue;
        }
        voice_handle(&cmd);
    }
}

static void voice_handle(const voice_cmd_t *const p_cmd)
{
    int slot;

    switch (p_cmd->type)
    {
        case VOICE_CMD_REC_START:
            voice_record(0U != p_cmd->arg);
            break;
        case VOICE_CMD_REC_STOP:
            if (0U == p_cmd->arg)
            {
                voice_record_drop(); /* Dropped in the review. */
            }
            break;
        case VOICE_CMD_REC_SEND:
            voice_record_send();
            break;
        case VOICE_CMD_PLAY:
            voice_play(p_cmd->arg, (uint8_t)p_cmd->id);
            break;
        case VOICE_CMD_DELETE_OWNER:
            voice_delete_owner(p_cmd->arg);
            break;
        case VOICE_CMD_GET:
            slot = voice_find_guest(p_cmd->id);
            if (VOICE_SLOT_NONE != slot)
            {
                (void)voice_upload((uint8_t)slot);
            }
            else
            {
                voice_publish_index(); /* The web has an old list. */
            }
            break;
        case VOICE_CMD_DELETE_GUEST:
            voice_delete_guest(p_cmd->id);
            break;
        case VOICE_CMD_INDEX:
            voice_publish_index();
            break;
        default: /* PLAY_STOP: nothing is playing. */
            break;
    }
}

static void voice_send(uint8_t type, uint8_t arg, uint32_t id)
{
    const voice_cmd_t cmd = {.type = type, .arg = arg, .id = id};

    if ((NULL != s_queue) && (pdTRUE != xQueueSend(s_queue, &cmd, 0)))
    {
        ESP_LOGW(VOICE_TASK_TAG, "Queue full, command %u dropped", type);
    }
}

/* Takes queued commands without waiting. True with a key command, which ends the activity going on; web requests
 * wait in s_later until it is over. */
static bool voice_poll_local(voice_cmd_t *const p_cmd)
{
    voice_cmd_t cmd;

    while (pdTRUE == xQueueReceive(s_queue, &cmd, 0))
    {
        if (cmd.type < VOICE_CMD_GET)
        {
            *p_cmd = cmd;
            return true;
        }
        bool known = false;
        for (uint8_t i = 0U; i < s_later_count; i++)
        {
            known = known || ((s_later[i].type == cmd.type) && (s_later[i].id == cmd.id));
        }
        if (!known && (s_later_count < VOICE_LATER_MAX))
        {
            s_later[s_later_count++] = cmd;
        }
    }
    return false;
}

/***********************************************************************************************************************
 * Records from the microphone to REC.TMP until system_control says stop (or cancel). The file then waits for the
 * review: listened to, recorded again, dropped, or kept with voice_record_send().
 **********************************************************************************************************************/
static void voice_record(bool owner)
{
    voice_file_header_t header;
    voice_cmd_t         cmd;
    uint32_t            bytes = 0U;
    bool                keep  = true;
    FILE               *p_file;

    s_pending = false; /* Recording again: REC.TMP is rewritten. */
    if (!s_ready)
    {
        system_control_voice_result(SYSTEM_NOTICE_MSG_UNAVAILABLE);
        return;
    }
    if (!audio_task_speaker_take(pdMS_TO_TICKS(VOICE_LOCK_WAIT_MS)))
    {
        ESP_LOGW(VOICE_TASK_TAG, "Audio bus busy, recording not started");
        system_control_voice_result(SYSTEM_NOTICE_MSG_FAILED);
        return;
    }

    voice_header_init(&header, SYSTEM_MSG_FROM_DOOR, esp_random() | 1U);
    p_file  = fopen(VOICE_FILE_REC, "wb");
    bool ok = (NULL != p_file) && (1U == fwrite(&header, sizeof(header), 1U, p_file))
              && (ESP_OK == inm441_i2s_mic_start_recording());
    while (ok)
    {
        if (voice_poll_local(&cmd))
        {
            if (VOICE_CMD_REC_STOP == cmd.type)
            {
                keep = (0U != cmd.arg);
            }
            else
            {
                s_next     = cmd; /* Not expected while recording: handled once this one is kept. */
                s_has_next = true;
            }
            break;
        }
        if (bytes >= VOICE_REC_MAX_BYTES)
        {
            break; /* Only if the stop from system_control was lost. */
        }

        size_t got = 0U;
        ok         = (ESP_OK == inm441_i2s_mic_read_smart(s_buf, VOICE_CHUNK_DATA_MAX, &got, VOICE_MIC_TIMEOUT_MS));
        int16_t *const p_samples = (int16_t *)s_buf;
        for (size_t i = 0U; i < (got / sizeof(int16_t)); i++)
        {
            int32_t value = (int32_t)p_samples[i] * VOICE_MIC_GAIN;
            value         = (value > INT16_MAX) ? INT16_MAX : ((value < INT16_MIN) ? INT16_MIN : value);
            p_samples[i]  = (int16_t)value;
        }
        /* 4 KB at a time: one SD write per 128 ms, well inside the I2S DMA buffers. */
        ok    = ok && (got == fwrite(s_buf, 1U, got, p_file));
        bytes = bytes + (uint32_t)got;
    }
    (void)inm441_i2s_mic_stop_recording();
    audio_task_speaker_give();
    if ((NULL != p_file) && (0 != fclose(p_file)))
    {
        ok = false;
    }

    if (!keep)
    {
        (void)remove(VOICE_FILE_REC); /* Cancelled: system_control has said so. */
        return;
    }
    if (!ok)
    {
        ESP_LOGE(VOICE_TASK_TAG, "Recording failed");
        (void)remove(VOICE_FILE_REC);
        system_control_voice_result(SYSTEM_NOTICE_MSG_FAILED);
        return;
    }
    if (bytes < VOICE_MIN_BYTES)
    {
        (void)remove(VOICE_FILE_REC);
        system_control_voice_result(SYSTEM_NOTICE_MSG_TOO_SHORT);
        return;
    }

    s_pending        = true;
    s_pending_owner  = owner;
    s_pending_header = header;
    s_pending_bytes  = bytes;
    system_control_voice_recorded((uint8_t)((bytes + VOICE_BYTES_PER_S - 1U) / VOICE_BYTES_PER_S));
}

/* # in the review (or its timeout): the recording goes into a slot. A guest's then goes to the web; a message for
 * guests goes to the list on Home. */
static void voice_record_send(void)
{
    if (!s_pending)
    {
        system_control_voice_result(SYSTEM_NOTICE_MSG_FAILED);
        return;
    }
    s_pending        = false;
    const bool owner = s_pending_owner;
    const int  slot  = voice_commit(owner, VOICE_FILE_REC, &s_pending_header, s_pending_bytes);
    if (VOICE_SLOT_NONE == slot)
    {
        system_control_voice_result(SYSTEM_NOTICE_MSG_FAILED);
        return;
    }
    ESP_LOGI(VOICE_TASK_TAG,
             "Message %s kept: %lu ms, slot %d",
             owner ? "for guests" : "for the owner",
             (unsigned long)((s_pending_bytes * 1000ULL) / VOICE_BYTES_PER_S),
             slot);
    if (owner)
    {
        voice_publish_owner_list();
        system_control_voice_result(SYSTEM_NOTICE_MSG_SAVED);
        return;
    }
    system_control_voice_result(SYSTEM_NOTICE_MSG_SENT);
    (void)voice_upload((uint8_t)slot); /* Live to an open page; the index lets a closed one fetch it later. */
    voice_publish_index();
}

static void voice_record_drop(void)
{
    if (s_pending)
    {
        s_pending = false;
        (void)remove(VOICE_FILE_REC);
    }
}

/***********************************************************************************************************************
 * Plays a message for guests (or the recording under review) until its end, or until a key stops it. Only the end and a
 *failure are reported: system_control already left the screen when it asked to stop.
 **********************************************************************************************************************/
static void voice_play(uint8_t slot, uint8_t token)
{
    voice_entry_t entry = {0};
    char          path[VOICE_PATH_SIZE];
    voice_cmd_t   cmd;
    FILE         *p_file  = NULL;
    bool          ok      = false;
    bool          stopped = false;

    if (SYSTEM_PLAY_RECORDING == slot)
    {
        entry.used = s_pending;
        (void)snprintf(path, sizeof(path), "%s", VOICE_FILE_REC);
    }
    else
    {
        (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (slot < SYSTEM_MSG_SLOTS)
        {
            entry = s_owner[slot];
        }
        if (entry.used)
        {
            s_playing = slot;
        }
        (void)xSemaphoreGive(s_mutex);
        voice_path(path, true, slot);
    }
    if (entry.used)
    {
        p_file = fopen(path, "rb");
    }
    if ((NULL != p_file) && (0 == fseek(p_file, (long)sizeof(voice_file_header_t), SEEK_SET))
        && audio_task_speaker_take(pdMS_TO_TICKS(VOICE_LOCK_WAIT_MS)))
    {
        ok = (ESP_OK == ns4168_speaker_start_playback());
        while (ok)
        {
            if (voice_poll_local(&cmd))
            {
                if (VOICE_CMD_PLAY_STOP != cmd.type)
                {
                    s_next     = cmd;
                    s_has_next = true;
                }
                stopped = true;
                break;
            }
            /* s_buf: the mono samples read, then the same samples as stereo after them. */
            const int16_t *const p_samples = (const int16_t *)s_buf;
            int16_t *const       p_stereo  = (int16_t *)&s_buf[VOICE_PLAY_SAMPLES * sizeof(int16_t)];
            const size_t         count     = fread(s_buf, sizeof(int16_t), VOICE_PLAY_SAMPLES, p_file);
            if (0U == count)
            {
                break;
            }
            for (size_t i = 0U; i < count; i++)
            {
                p_stereo[2U * i]        = p_samples[i];
                p_stereo[(2U * i) + 1U] = p_samples[i];
            }
            size_t written = 0U;
            ok             = (ESP_OK
                  == ns4168_speaker_write(p_stereo, count * 2U * sizeof(int16_t), &written, VOICE_SPK_TIMEOUT_MS));
        }
        if (ok && !stopped)
        {
            vTaskDelay(pdMS_TO_TICKS(VOICE_TAIL_MS));
        }
        (void)ns4168_speaker_stop_playback();
        audio_task_speaker_give();
    }
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }

    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_playing = VOICE_SLOT_NONE;
    (void)xSemaphoreGive(s_mutex);

    if (!ok)
    {
        ESP_LOGW(VOICE_TASK_TAG, "Could not play slot %u", slot);
    }
    if (!stopped)
    {
        system_control_play_done(token, ok);
    }
}

/***********************************************************************************************************************
 * Sends a guest's message on voice/out in chunks. Each chunk waits until the broker has acknowledged most of the
 * previous ones, so the esp-mqtt outbox stays small. A key at the door stops it: the web can ask again (voice/req).
 **********************************************************************************************************************/
static bool voice_upload(uint8_t slot)
{
    mqtt_conn_instance_ctrl_t *const p_mqtt = network_task_get_mqtt();
    char                             path[VOICE_PATH_SIZE];
    voice_cmd_t                      cmd;
    bool                             ok = true;

    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    const voice_entry_t entry = s_guest[slot];
    (void)xSemaphoreGive(s_mutex);
    if (!entry.used || !mqtt_conn_is_connected(p_mqtt))
    {
        return false;
    }

    voice_path(path, false, slot);
    FILE *const p_file = fopen(path, "rb");
    if ((NULL == p_file) || (0 != fseek(p_file, (long)sizeof(voice_file_header_t), SEEK_SET)))
    {
        if (NULL != p_file)
        {
            (void)fclose(p_file);
        }
        return false;
    }

    const uint16_t total = (uint16_t)((entry.bytes + VOICE_CHUNK_DATA_MAX - 1U) / VOICE_CHUNK_DATA_MAX);
    for (uint16_t seq = 0U; ok && (seq < total); seq++)
    {
        if (voice_poll_local(&cmd))
        {
            s_next     = cmd;
            s_has_next = true;
            ok         = false;
            break;
        }
        const size_t len = fread(&s_buf[VOICE_CHUNK_HEADER_LEN], 1U, VOICE_CHUNK_DATA_MAX, p_file) & ~(size_t)1U;
        if (0U == len)
        {
            ok = false;
            break;
        }
        s_buf[0] = VOICE_CHUNK_MAGIC0;
        s_buf[1] = VOICE_CHUNK_MAGIC1;
        s_buf[2] = VOICE_CHUNK_VERSION;
        s_buf[3] = VOICE_CODEC_PCM16;
        voice_put32(&s_buf[4], entry.id);
        voice_put16(&s_buf[8], seq);
        voice_put16(&s_buf[10], total);
        voice_put32(&s_buf[12], VOICE_RATE);
        ok = voice_wait_outbox(p_mqtt)
             && (APP_SUCCESS
                 == mqtt_conn_publish(p_mqtt, VOICE_TOPIC_OUT, s_buf, VOICE_CHUNK_HEADER_LEN + len, 1U, false));
    }
    (void)fclose(p_file);
    ESP_LOGI(VOICE_TASK_TAG, "Message %08lx %s", (unsigned long)entry.id, ok ? "sent to the web" : "not sent");
    return ok;
}

static bool voice_wait_outbox(mqtt_conn_instance_ctrl_t *const p_mqtt)
{
    for (uint32_t waited = 0U; waited < VOICE_OUTBOX_WAIT_MS; waited += 20U)
    {
        if (!mqtt_conn_is_connected(p_mqtt))
        {
            return false; /* QoS 1 would pile up in the outbox while offline. */
        }
        if (mqtt_conn_outbox_size(p_mqtt) <= VOICE_OUTBOX_MAX)
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

/* Retained {"items":[{"id":..,"ts":..,"ms":..}]}, newest first: the guests' messages the web can fetch. */
static void voice_publish_index(void)
{
    mqtt_conn_instance_ctrl_t *const p_mqtt = network_task_get_mqtt();
    voice_entry_t                    list[VOICE_GUEST_SLOTS];
    bool                             taken[VOICE_GUEST_SLOTS] = {false};
    int                              len;

    if (!mqtt_conn_is_connected(p_mqtt))
    {
        return; /* Sent again on the next connection. */
    }
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(list, s_guest, sizeof(list));
    (void)xSemaphoreGive(s_mutex);

    len = snprintf(s_index, sizeof(s_index), "{\"items\":[");
    for (uint8_t n = 0U; n < VOICE_GUEST_SLOTS; n++)
    {
        int best = VOICE_SLOT_NONE;
        for (uint8_t i = 0U; i < VOICE_GUEST_SLOTS; i++)
        {
            if (list[i].used && !taken[i] && ((VOICE_SLOT_NONE == best) || (list[i].seq > list[best].seq)))
            {
                best = i;
            }
        }
        if (VOICE_SLOT_NONE == best)
        {
            break;
        }
        taken[best] = true;
        len += snprintf(&s_index[len],
                        sizeof(s_index) - (size_t)len,
                        "%s{\"id\":%lu,\"ts\":%lu,\"ms\":%lu}",
                        (0U == n) ? "" : ",",
                        (unsigned long)list[best].id,
                        (unsigned long)list[best].ts,
                        (unsigned long)((list[best].bytes * 1000ULL) / VOICE_BYTES_PER_S));
    }
    len += snprintf(&s_index[len], sizeof(s_index) - (size_t)len, "]}");
    if (APP_SUCCESS != mqtt_conn_publish(p_mqtt, VOICE_TOPIC_INDEX, s_index, (size_t)len, 1U, true))
    {
        ESP_LOGW(VOICE_TASK_TAG, "voice/index not published");
    }
}

/* The messages for guests, newest first, to system_control: Home shows them, hold # plays them. */
static void voice_publish_owner_list(void)
{
    voice_entry_t    owner[SYSTEM_MSG_SLOTS];
    system_message_t list[SYSTEM_MSG_SLOTS];
    bool             taken[SYSTEM_MSG_SLOTS] = {false};
    uint8_t          n                       = 0U;

    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(owner, s_owner, sizeof(owner));
    (void)xSemaphoreGive(s_mutex);

    for (; n < SYSTEM_MSG_SLOTS; n++)
    {
        int best = VOICE_SLOT_NONE;
        for (uint8_t i = 0U; i < SYSTEM_MSG_SLOTS; i++)
        {
            if (owner[i].used && !taken[i] && ((VOICE_SLOT_NONE == best) || (owner[i].seq > owner[best].seq)))
            {
                best = i;
            }
        }
        if (VOICE_SLOT_NONE == best)
        {
            break;
        }
        const uint32_t seconds = (owner[best].bytes + VOICE_BYTES_PER_S - 1U) / VOICE_BYTES_PER_S;
        taken[best]            = true;
        list[n].slot           = (uint8_t)best;
        list[n].source         = owner[best].source;
        list[n].seconds        = (uint8_t)((seconds > UINT8_MAX) ? UINT8_MAX : seconds);
        list[n].ts             = owner[best].ts;
    }
    system_control_set_messages(s_ready, list, n);
}

static void voice_delete_owner(uint8_t slot)
{
    char path[VOICE_PATH_SIZE];

    if (slot >= SYSTEM_MSG_SLOTS)
    {
        return;
    }
    voice_path(path, true, slot);
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    (void)remove(path);
    s_owner[slot].used = false;
    (void)xSemaphoreGive(s_mutex);
    voice_publish_owner_list();
}

/* The owner deleted it on the web. */
static void voice_delete_guest(uint32_t id)
{
    char      path[VOICE_PATH_SIZE];
    const int slot = voice_find_guest(id);

    if (VOICE_SLOT_NONE != slot)
    {
        voice_path(path, false, (uint8_t)slot);
        (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
        (void)remove(path);
        s_guest[slot].used = false;
        (void)xSemaphoreGive(s_mutex);
        ESP_LOGI(VOICE_TASK_TAG, "Message %08lx deleted from the web", (unsigned long)id);
    }
    voice_publish_index();
}

static int voice_find_guest(uint32_t id)
{
    int slot = VOICE_SLOT_NONE;

    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint8_t i = 0U; (i < VOICE_GUEST_SLOTS) && (VOICE_SLOT_NONE == slot); i++)
    {
        if (s_guest[i].used && (id == s_guest[i].id))
        {
            slot = i;
        }
    }
    (void)xSemaphoreGive(s_mutex);
    return slot;
}

/***********************************************************************************************************************
 * Moves a finished file into a free slot, or over the oldest message (never the one being played). Returns the slot,
 * or VOICE_SLOT_NONE; the source file is gone either way.
 **********************************************************************************************************************/
static int voice_commit(bool owner, const char *p_src, const voice_file_header_t *const p_header, uint32_t bytes)
{
    voice_entry_t *const p_table = owner ? s_owner : s_guest;
    const uint8_t        count   = owner ? SYSTEM_MSG_SLOTS : VOICE_GUEST_SLOTS;
    char                 path[VOICE_PATH_SIZE];
    int                  slot = VOICE_SLOT_NONE;

    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint8_t i = 0U; (i < count) && (VOICE_SLOT_NONE == slot); i++)
    {
        if (!p_table[i].used)
        {
            slot = i;
        }
    }
    if (VOICE_SLOT_NONE == slot)
    {
        /* Full: the oldest goes. */
        for (uint8_t i = 0U; i < count; i++)
        {
            if ((owner && (i == s_playing)) || !p_table[i].used)
            {
                continue;
            }
            if ((VOICE_SLOT_NONE == slot) || (p_table[i].seq < p_table[slot].seq))
            {
                slot = i;
            }
        }
    }
    if (VOICE_SLOT_NONE != slot)
    {
        voice_path(path, owner, (uint8_t)slot);
        (void)remove(path); /* FAT does not rename over a file. */
        p_table[slot].used = false;
        if (0 == rename(p_src, path))
        {
            p_table[slot] = (voice_entry_t) {
                .used   = true,
                .source = p_header->source,
                .id     = p_header->id,
                .seq    = p_header->seq,
                .ts     = p_header->ts,
                .bytes  = bytes,
            };
        }
        else
        {
            ESP_LOGE(VOICE_TASK_TAG, "Could not keep %s as %s", p_src, path);
            slot = VOICE_SLOT_NONE;
        }
    }
    (void)xSemaphoreGive(s_mutex);
    if (VOICE_SLOT_NONE == slot)
    {
        (void)remove(p_src);
    }
    return slot;
}

static void voice_header_init(voice_file_header_t *const p_header, uint8_t source, uint32_t id)
{
    const time_t now = time(NULL);

    memset(p_header, 0, sizeof(*p_header));
    p_header->magic[0] = VOICE_FILE_MAGIC0;
    p_header->magic[1] = VOICE_FILE_MAGIC1;
    p_header->version  = VOICE_FILE_VERSION;
    p_header->source   = source;
    p_header->id       = id;
    p_header->ts       = (now > VOICE_TS_VALID) ? (uint32_t)now : 0U;
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    p_header->seq = s_next_seq++;
    (void)xSemaphoreGive(s_mutex);
}

/* At boot, before any other task touches the files. */
static void voice_scan(void)
{
    char     path[VOICE_PATH_SIZE];
    uint32_t last = 0U;

    for (uint8_t i = 0U; i < (SYSTEM_MSG_SLOTS + VOICE_GUEST_SLOTS); i++)
    {
        const bool           owner   = (i < SYSTEM_MSG_SLOTS);
        const uint8_t        slot    = owner ? i : (uint8_t)(i - SYSTEM_MSG_SLOTS);
        voice_entry_t *const p_entry = owner ? &s_owner[slot] : &s_guest[slot];

        voice_path(path, owner, slot);
        if (voice_load(path, p_entry) && (p_entry->seq > last))
        {
            last = p_entry->seq;
        }
    }
    s_next_seq = last + 1U;
}

/* Reads the header and the size of a message file. A damaged one is deleted. */
static bool voice_load(const char *p_path, voice_entry_t *const p_entry)
{
    voice_file_header_t header;
    FILE *const         p_file = fopen(p_path, "rb");
    long                size   = 0;

    memset(p_entry, 0, sizeof(*p_entry));
    if (NULL == p_file)
    {
        return false; /* Free slot. */
    }
    const bool read = (1U == fread(&header, sizeof(header), 1U, p_file)) && (0 == fseek(p_file, 0, SEEK_END));
    size            = read ? ftell(p_file) : 0;
    (void)fclose(p_file);

    if (!read || (VOICE_FILE_MAGIC0 != header.magic[0]) || (VOICE_FILE_MAGIC1 != header.magic[1])
        || (VOICE_FILE_VERSION != header.version) || (size <= (long)sizeof(header)))
    {
        ESP_LOGW(VOICE_TASK_TAG, "%s is damaged, deleted", p_path);
        (void)remove(p_path);
        return false;
    }
    p_entry->used   = true;
    p_entry->source = header.source;
    p_entry->id     = header.id;
    p_entry->seq    = header.seq;
    p_entry->ts     = header.ts;
    p_entry->bytes  = (uint32_t)(size - (long)sizeof(header));
    return true;
}

/* 8.3 names: FATFS long file names are off. */
static void voice_path(char *p_out, bool owner, uint8_t slot)
{
    (void)snprintf(p_out, VOICE_PATH_SIZE, "%s/%c%u.PCM", VOICE_DIR, owner ? 'O' : 'G', slot);
}

/***********************************************************************************************************************
 * One chunk of a message from the owner (voice/in), in the MQTT task. The web sends the chunks in order and waits for
 * each acknowledgement, so they arrive in order: written straight to IN.TMP, kept for guests after the last one.
 **********************************************************************************************************************/
static void voice_in_chunk(const uint8_t *p_data, size_t len)
{
    const TickType_t now = xTaskGetTickCount();

    if ((len <= VOICE_CHUNK_HEADER_LEN) || (len > VOICE_IN_MAX_LEN) || (VOICE_CHUNK_MAGIC0 != p_data[0])
        || (VOICE_CHUNK_MAGIC1 != p_data[1]) || (VOICE_CHUNK_VERSION != p_data[2]) || (VOICE_CODEC_PCM16 != p_data[3]))
    {
        ESP_LOGW(VOICE_TASK_TAG, "Ignored a voice/in message that is not a voice chunk");
        return;
    }
    const uint32_t msg_id   = voice_get32(&p_data[4]);
    const uint16_t seq      = voice_get16(&p_data[8]);
    const uint16_t total    = voice_get16(&p_data[10]);
    const uint32_t rate     = voice_get32(&p_data[12]);
    const size_t   data_len = len - VOICE_CHUNK_HEADER_LEN;

    if ((VOICE_RATE != rate) || (0U == total) || (seq >= total) || (0U != (data_len % 2U))
        || (((uint32_t)total * VOICE_CHUNK_DATA_MAX) > (VOICE_IN_MAX_BYTES + VOICE_CHUNK_DATA_MAX)))
    {
        ESP_LOGW(VOICE_TASK_TAG, "Ignored a voice chunk: %lu Hz, %u / %u", (unsigned long)rate, seq, total);
        return;
    }

    if (0U == seq)
    {
        voice_in_drop(); /* A new message; whatever was half received is lost. */
        voice_header_init(&s_in.header, SYSTEM_MSG_FROM_WEB, msg_id);
        s_in.p_file = fopen(VOICE_FILE_IN, "wb");
        if ((NULL == s_in.p_file) || (1U != fwrite(&s_in.header, sizeof(s_in.header), 1U, s_in.p_file)))
        {
            ESP_LOGE(VOICE_TASK_TAG, "Cannot write %s", VOICE_FILE_IN);
            voice_in_drop();
            return;
        }
        s_in.msg_id   = msg_id;
        s_in.total    = total;
        s_in.next_seq = 0U;
        s_in.bytes    = 0U;
        s_in.start    = now;
    }
    if ((NULL == s_in.p_file) || (msg_id != s_in.msg_id))
    {
        return; /* The rest of a message whose start was missed, or of a second sender. */
    }
    if ((seq != s_in.next_seq) || (total != s_in.total)
        || (((uint32_t)(now - s_in.start) * portTICK_PERIOD_MS) > VOICE_IN_TIMEOUT_MS)
        || (data_len != fwrite(&p_data[VOICE_CHUNK_HEADER_LEN], 1U, data_len, s_in.p_file)))
    {
        ESP_LOGW(VOICE_TASK_TAG, "Message from the web incomplete, dropped");
        voice_in_drop();
        return;
    }
    s_in.bytes += (uint32_t)data_len;
    s_in.next_seq++;
    if (s_in.next_seq < s_in.total)
    {
        return;
    }

    FILE *const p_file = s_in.p_file;
    s_in.p_file        = NULL;
    if (0 != fclose(p_file))
    {
        (void)remove(VOICE_FILE_IN);
        return;
    }
    const int slot = voice_commit(true, VOICE_FILE_IN, &s_in.header, s_in.bytes);
    if (VOICE_SLOT_NONE != slot)
    {
        ESP_LOGI(VOICE_TASK_TAG,
                 "Message for guests from the web: %lu ms, slot %d",
                 (unsigned long)((s_in.bytes * 1000ULL) / VOICE_BYTES_PER_S),
                 slot);
        voice_publish_owner_list();
    }
}

static void voice_in_drop(void)
{
    if (NULL != s_in.p_file)
    {
        (void)fclose(s_in.p_file);
        s_in.p_file = NULL;
        (void)remove(VOICE_FILE_IN);
    }
}

/* {"op":"get"|"del","id":n} in the MQTT task: queued for the voice task. */
static void voice_req(const uint8_t *p_data, size_t len)
{
    cJSON *const p_root = cJSON_ParseWithLength((const char *)p_data, len);
    if (NULL == p_root)
    {
        return;
    }
    const cJSON *const p_op = cJSON_GetObjectItemCaseSensitive(p_root, "op");
    const cJSON *const p_id = cJSON_GetObjectItemCaseSensitive(p_root, "id");
    if (cJSON_IsString(p_op) && cJSON_IsNumber(p_id) && (p_id->valuedouble >= 1.0)
        && (p_id->valuedouble <= (double)UINT32_MAX) && (p_id->valuedouble == (double)(uint32_t)p_id->valuedouble))
    {
        const uint32_t id = (uint32_t)p_id->valuedouble;
        if (0 == strcmp(p_op->valuestring, "get"))
        {
            voice_send(VOICE_CMD_GET, 0U, id);
        }
        else if (0 == strcmp(p_op->valuestring, "del"))
        {
            voice_send(VOICE_CMD_DELETE_GUEST, 0U, id);
        }
    }
    cJSON_Delete(p_root);
}

static uint16_t voice_get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t voice_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void voice_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void voice_put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
