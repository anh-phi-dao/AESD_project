#include "storage_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_card.h"
#include "system_control.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* 8.3 names: FATFS is built without long file names (CONFIG_FATFS_LFN_NONE). */
#define STORAGE_FILE        "CREDS.TXT"
#define STORAGE_FILE_TMP    "CREDS.TMP" /* Written first, then renamed: a power cut never leaves half a file. */
#define STORAGE_FILE_BAK    "CREDS.BAK" /* The previous CREDS.TXT, read when CREDS.TXT is missing. */
#define STORAGE_VERSION     2U          /* 1: one card and one finger per user, by user ID. Still read. */
#define STORAGE_BUFFER_SIZE 3072U       /* Full: 20 PINs, 20 cards, 40 fingers take about 2 KB. */

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static esp_err_t storage_load(void);
static esp_err_t storage_read(const char *p_name);
static bool      storage_parse(char *p_text, system_credentials_t *const p_cred);
static bool      storage_parse_line(char *p_line, unsigned version, system_credentials_t *const p_cred);
static int       storage_parse_num(const char *p_text, unsigned max);
static uint8_t   storage_parse_hex(const char *p_text, uint8_t *p_out, uint8_t out_max);
static esp_err_t storage_save(const system_credentials_t *const p_cred);
static bool      storage_format(const system_credentials_t *const p_cred, char *p_out, size_t out_size);
static void      storage_wipe(void);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* Used by boot_storage() and then by the storage task only. Both hold PINs in clear: wiped after every use. */
static char                 s_buffer[STORAGE_BUFFER_SIZE];
static system_credentials_t s_cred;

static bool     s_ready      = false; /* Card mounted and credentials loaded: changes may be saved. */
static bool     s_unreadable = false; /* A credentials file is there but could not be read. */
static uint32_t s_saved_seq  = 0U;    /* system_control credentials_seq of what is on the card. */

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

void boot_storage(void)
{
    if (ESP_OK != sd_card_init())
    {
        ESP_LOGW(STORAGE_TASK_TAG, "No SD card, credentials are kept in RAM only");
        return;
    }

    const esp_err_t ret = storage_load();
    if (ESP_ERR_NOT_FOUND == ret)
    {
        /* First boot with this card: write what system_control starts with, so the file exists from now on. */
        uint32_t seq = 0U;
        if (system_control_get_credentials(&s_cred, &seq) && (ESP_OK == storage_save(&s_cred)))
        {
            s_saved_seq = seq;
            ESP_LOGI(STORAGE_TASK_TAG, "No credentials file, created " STORAGE_FILE " with the defaults");
        }
        else
        {
            s_saved_seq = seq - 1U; /* Not on the card yet: the task tries again. */
        }
        storage_wipe();
    }
    else if (ESP_OK != ret)
    {
        /* Do not save over a file that is there but could not be read: fixing the card must bring the users back. */
        ESP_LOGE(STORAGE_TASK_TAG, "Credentials file unreadable (%s), changes will not be saved", esp_err_to_name(ret));
        s_unreadable = true;
        return;
    }
    s_ready = true;
}

bool storage_is_unreadable(void)
{
    return s_unreadable;
}

void storage_task(void *pvParameter)
{
    (void)pvParameter;

    if (!s_ready)
    {
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(STORAGE_TASK_POLL_MS));
        if (system_control_credentials_seq() == s_saved_seq)
        {
            continue;
        }

        uint32_t seq = 0U;
        if (!system_control_get_credentials(&s_cred, &seq))
        {
            continue;
        }
        const esp_err_t ret = storage_save(&s_cred);
        storage_wipe();
        if (ESP_OK == ret)
        {
            s_saved_seq = seq;
            ESP_LOGI(STORAGE_TASK_TAG, "Credentials saved to the SD card");
        }
        else
        {
            ESP_LOGE(STORAGE_TASK_TAG, "Saving the credentials failed (%s), retrying", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(STORAGE_TASK_RETRY_MS));
        }
    }
}

/***********************************************************************************************************************
 * Private APIs
 **********************************************************************************************************************/

/* CREDS.TXT, or CREDS.BAK when a power cut came between the two renames of storage_save(). ESP_ERR_NOT_FOUND: no
 * file at all. */
static esp_err_t storage_load(void)
{
    esp_err_t ret = storage_read(STORAGE_FILE);
    if (ESP_ERR_NOT_FOUND == ret)
    {
        ret = storage_read(STORAGE_FILE_BAK);
        if (ESP_OK == ret)
        {
            ESP_LOGW(STORAGE_TASK_TAG, STORAGE_FILE " missing, loaded " STORAGE_FILE_BAK);
        }
    }
    storage_wipe();
    return ret;
}

static esp_err_t storage_read(const char *p_name)
{
    esp_err_t ret = sd_card_read_file(p_name, s_buffer, sizeof(s_buffer), NULL);
    if (ESP_OK != ret)
    {
        return ret;
    }

    memset(&s_cred, 0, sizeof(s_cred));
    if (!storage_parse(s_buffer, &s_cred))
    {
        return ESP_ERR_INVALID_VERSION;
    }
    /* Read back: system_control drops entries that break its rules, and its seq says what is now on the card. */
    if ((APP_SUCCESS != system_control_set_credentials(&s_cred))
        || !system_control_get_credentials(&s_cred, &s_saved_seq))
    {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t pins    = 0U;
    uint8_t cards   = 0U;
    uint8_t fingers = 0U;
    for (uint8_t i = 0U; i < SYSTEM_USER_COUNT; i++)
    {
        pins += ('\0' != s_cred.pins[i][0]) ? 1U : 0U;
    }
    for (uint8_t i = 0U; i < SYSTEM_CARD_SLOTS; i++)
    {
        cards += (0U != s_cred.card_user[i]) ? 1U : 0U;
    }
    for (uint8_t i = 0U; i < SYSTEM_FINGER_SLOTS; i++)
    {
        fingers += (0U != s_cred.finger_user[i]) ? 1U : 0U;
    }
    ESP_LOGI(STORAGE_TASK_TAG, "Loaded %u PINs, %u cards, %u fingers from %s", pins, cards, fingers, p_name);
    return ESP_OK;
}

/***********************************************************************************************************************
 * One entry per line, '#' starts a comment:
 *   VERSION,2
 *   PIN,<user>,<digits>
 *   CARD,<slot>,<user>,<UID in hex>
 *   FINGER,<slot>,<user>,<finger 0..10>   (the template is in the sensor, at page <slot> + 1)
 * Version 1 had CARD,<user>,<hex> and FINGER,<user>: read into slot <user> - 1, which is the same sensor page, with
 * the finger unknown. The next save writes version 2. A bad line is skipped; an unknown version is refused, so it is
 * not overwritten in a format it does not have.
 **********************************************************************************************************************/
static bool storage_parse(char *p_text, system_credentials_t *const p_cred)
{
    unsigned version = 0U;
    char    *p_save  = NULL;

    for (char *p_line = strtok_r(p_text, "\n", &p_save); NULL != p_line; p_line = strtok_r(NULL, "\n", &p_save))
    {
        p_line[strcspn(p_line, "\r#")] = '\0';
        if ('\0' == p_line[0])
        {
            continue;
        }
        if (0 == strncmp(p_line, "VERSION,", 8))
        {
            version = (unsigned)strtoul(&p_line[8], NULL, 10);
            if ((1U != version) && (STORAGE_VERSION != version))
            {
                ESP_LOGE(STORAGE_TASK_TAG, "Unknown file version: %s", &p_line[8]);
                return false;
            }
            continue;
        }
        if ((0U == version) || !storage_parse_line(p_line, version, p_cred))
        {
            /* The line itself is not logged: it may hold a PIN. */
            ESP_LOGW(STORAGE_TASK_TAG, "Bad line in the credentials file skipped");
        }
    }

    if (0U == version)
    {
        ESP_LOGE(STORAGE_TASK_TAG, "Credentials file has no VERSION line");
    }
    return 0U != version;
}

static bool storage_parse_line(char *p_line, unsigned version, system_credentials_t *const p_cred)
{
    char       *p_save      = NULL;
    const char *p_fields[4] = {NULL, NULL, NULL, NULL};

    p_fields[0] = strtok_r(p_line, ",", &p_save);
    for (uint8_t i = 1U; (i < 4U) && (NULL != p_fields[i - 1U]); i++)
    {
        p_fields[i] = strtok_r(NULL, ",", &p_save);
    }
    if ((NULL == p_fields[0]) || (NULL == p_fields[1]))
    {
        return false;
    }

    if (0 == strcmp(p_fields[0], "PIN"))
    {
        const int user = storage_parse_num(p_fields[1], SYSTEM_USER_COUNT);
        /* Digits and length are checked by system_control_set_credentials(). */
        if ((user < 1) || (NULL == p_fields[2]) || (strlen(p_fields[2]) > SYSTEM_ENTRY_MAX))
        {
            return false;
        }
        (void)snprintf(p_cred->pins[user - 1U], sizeof(p_cred->pins[0]), "%s", p_fields[2]);
        return true;
    }

    /* Version 1: <user> only, slot = user - 1 (the same sensor page). Version 2: <slot>,<user>. */
    const bool  v1      = (1U == version);
    const int   user    = storage_parse_num(v1 ? p_fields[1] : p_fields[2], SYSTEM_USER_COUNT);
    const int   slot    = v1 ? (user - 1) : storage_parse_num(p_fields[1], SYSTEM_FINGER_SLOTS - 1U);
    const char *p_value = v1 ? p_fields[2] : p_fields[3];

    if ((user < 1) || (slot < 0))
    {
        return false;
    }

    if (0 == strcmp(p_fields[0], "CARD"))
    {
        if ((slot >= (int)SYSTEM_CARD_SLOTS) || (NULL == p_value))
        {
            return false;
        }
        const uint8_t len = storage_parse_hex(p_value, p_cred->cards[slot], SYSTEM_UID_MAX);
        if (0U == len)
        {
            return false;
        }
        p_cred->card_user[slot] = (uint8_t)user;
        p_cred->card_len[slot]  = len;
        return true;
    }
    if (0 == strcmp(p_fields[0], "FINGER"))
    {
        const unsigned long finger
            = v1 ? SYSTEM_FINGER_UNKNOWN
                 : ((NULL != p_value) ? strtoul(p_value, NULL, 10) : (SYSTEM_FINGER_UNKNOWN + 1UL));
        if (finger > SYSTEM_FINGER_UNKNOWN)
        {
            return false;
        }
        p_cred->finger_user[slot] = (uint8_t)user;
        p_cred->finger_id[slot]   = (uint8_t)finger;
        return true;
    }
    return false;
}

/* 0..max, or -1 when the text is not such a number. */
static int storage_parse_num(const char *p_text, unsigned max)
{
    char         *p_end = NULL;
    unsigned long value = 0UL;

    if ((NULL == p_text) || ('\0' == p_text[0]))
    {
        return -1;
    }
    value = strtoul(p_text, &p_end, 10);
    return (('\0' != *p_end) || (value > max)) ? -1 : (int)value;
}

/* Bytes decoded, or 0 for an odd length, a non-hex character or more than out_max bytes. */
static uint8_t storage_parse_hex(const char *p_text, uint8_t *p_out, uint8_t out_max)
{
    const size_t len = strlen(p_text);

    if ((0U == len) || (0U != (len % 2U)) || ((len / 2U) > out_max))
    {
        return 0U;
    }
    for (size_t i = 0U; i < len; i += 2U)
    {
        const char    pair[3] = {p_text[i], p_text[i + 1U], '\0'};
        char         *p_end   = NULL;
        unsigned long value   = strtoul(pair, &p_end, 16);
        if ('\0' != *p_end)
        {
            return 0U;
        }
        p_out[i / 2U] = (uint8_t)value;
    }
    return (uint8_t)(len / 2U);
}

/* CREDS.TMP is written in full, then CREDS.TXT becomes CREDS.BAK and CREDS.TMP becomes CREDS.TXT. */
static esp_err_t storage_save(const system_credentials_t *const p_cred)
{
    if (!storage_format(p_cred, s_buffer, sizeof(s_buffer)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = sd_card_write_txt_file(STORAGE_FILE_TMP, s_buffer, false);
    if (ESP_OK != ret)
    {
        return ret;
    }
    ret = sd_card_rename_file(STORAGE_FILE, STORAGE_FILE_BAK);
    if ((ESP_OK != ret) && (ESP_ERR_NOT_FOUND != ret))
    {
        /* CREDS.TXT is there but could not be moved away: keep it rather than lose both copies. */
        return ret;
    }
    return sd_card_rename_file(STORAGE_FILE_TMP, STORAGE_FILE);
}

static bool storage_format(const system_credentials_t *const p_cred, char *p_out, size_t out_size)
{
    int used
        = snprintf(p_out,
                   out_size,
                   "# Smart lock credentials, written by the lock. PINs are in clear: keep this card in the lock.\n"
                   "VERSION,%u\n",
                   STORAGE_VERSION);

    for (uint8_t i = 0U; (i < SYSTEM_USER_COUNT) && (used >= 0) && ((size_t)used < out_size); i++)
    {
        if ('\0' != p_cred->pins[i][0])
        {
            used += snprintf(&p_out[used], out_size - (size_t)used, "PIN,%u,%s\n", i + 1U, p_cred->pins[i]);
        }
    }
    for (uint8_t i = 0U; (i < SYSTEM_CARD_SLOTS) && (used >= 0) && ((size_t)used < out_size); i++)
    {
        if (0U == p_cred->card_user[i])
        {
            continue;
        }
        used += snprintf(&p_out[used], out_size - (size_t)used, "CARD,%u,%u,", i, p_cred->card_user[i]);
        for (uint8_t b = 0U; (b < p_cred->card_len[i]) && (used >= 0) && ((size_t)used < out_size); b++)
        {
            used += snprintf(&p_out[used], out_size - (size_t)used, "%02X", p_cred->cards[i][b]);
        }
        if ((used >= 0) && ((size_t)used < out_size))
        {
            used += snprintf(&p_out[used], out_size - (size_t)used, "\n");
        }
    }
    for (uint8_t i = 0U; (i < SYSTEM_FINGER_SLOTS) && (used >= 0) && ((size_t)used < out_size); i++)
    {
        if (0U != p_cred->finger_user[i])
        {
            used += snprintf(&p_out[used],
                             out_size - (size_t)used,
                             "FINGER,%u,%u,%u\n",
                             i,
                             p_cred->finger_user[i],
                             p_cred->finger_id[i]);
        }
    }
    return (used >= 0) && ((size_t)used < out_size);
}

static void storage_wipe(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    memset(&s_cred, 0, sizeof(s_cred));
}
