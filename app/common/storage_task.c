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
#define STORAGE_VERSION     1U
#define STORAGE_BUFFER_SIZE 2048U /* 20 users with a PIN, a card and a finger take about 1.2 KB. */

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static esp_err_t storage_load(void);
static esp_err_t storage_read(const char *p_name);
static bool      storage_parse(char *p_text, system_credentials_t *const p_cred);
static bool      storage_parse_line(char *p_line, system_credentials_t *const p_cred);
static uint8_t   storage_parse_user(const char *p_text);
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

static bool     s_ready     = false; /* Card mounted and credentials loaded: changes may be saved. */
static uint32_t s_saved_seq = 0U;    /* system_control credentials_seq of what is on the card. */

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
        return;
    }
    s_ready = true;
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
        cards += (0U != s_cred.card_len[i]) ? 1U : 0U;
        fingers += s_cred.fingers[i] ? 1U : 0U;
    }
    ESP_LOGI(STORAGE_TASK_TAG, "Loaded %u PINs, %u cards, %u fingers from %s", pins, cards, fingers, p_name);
    return ESP_OK;
}

/***********************************************************************************************************************
 * One entry per line, '#' starts a comment:
 *   VERSION,1
 *   PIN,<user>,<digits>
 *   CARD,<user>,<UID in hex>
 *   FINGER,<user>         (the template is in the sensor, at page <user>)
 * A bad line is skipped; a file of another version is refused, so it is not overwritten in a format it does not have.
 **********************************************************************************************************************/
static bool storage_parse(char *p_text, system_credentials_t *const p_cred)
{
    bool  version_ok = false;
    char *p_save     = NULL;

    for (char *p_line = strtok_r(p_text, "\n", &p_save); NULL != p_line; p_line = strtok_r(NULL, "\n", &p_save))
    {
        p_line[strcspn(p_line, "\r#")] = '\0';
        if ('\0' == p_line[0])
        {
            continue;
        }
        if (0 == strncmp(p_line, "VERSION,", 8))
        {
            version_ok = (STORAGE_VERSION == strtoul(&p_line[8], NULL, 10));
            if (!version_ok)
            {
                ESP_LOGE(STORAGE_TASK_TAG, "Unknown file version: %s", &p_line[8]);
                return false;
            }
            continue;
        }
        if (!storage_parse_line(p_line, p_cred))
        {
            /* The line itself is not logged: it may hold a PIN. */
            ESP_LOGW(STORAGE_TASK_TAG, "Bad line in the credentials file skipped");
        }
    }

    if (!version_ok)
    {
        ESP_LOGE(STORAGE_TASK_TAG, "Credentials file has no VERSION line");
    }
    return version_ok;
}

static bool storage_parse_line(char *p_line, system_credentials_t *const p_cred)
{
    char       *p_save  = NULL;
    const char *p_kind  = strtok_r(p_line, ",", &p_save);
    const char *p_user  = strtok_r(NULL, ",", &p_save);
    const char *p_value = strtok_r(NULL, ",", &p_save);
    const uint8_t user  = (NULL != p_user) ? storage_parse_user(p_user) : 0U;

    if ((NULL == p_kind) || (0U == user))
    {
        return false;
    }
    const uint8_t i = (uint8_t)(user - 1U);

    if (0 == strcmp(p_kind, "PIN"))
    {
        /* Digits and length are checked by system_control_set_credentials(). */
        if ((NULL == p_value) || (strlen(p_value) > SYSTEM_ENTRY_MAX))
        {
            return false;
        }
        (void)snprintf(p_cred->pins[i], sizeof(p_cred->pins[i]), "%s", p_value);
        return true;
    }
    if (0 == strcmp(p_kind, "CARD"))
    {
        const uint8_t len = (NULL != p_value) ? storage_parse_hex(p_value, p_cred->cards[i], SYSTEM_UID_MAX) : 0U;
        p_cred->card_len[i] = len;
        return 0U != len;
    }
    if (0 == strcmp(p_kind, "FINGER"))
    {
        p_cred->fingers[i] = true;
        return true;
    }
    return false;
}

/* 1..SYSTEM_USER_COUNT, or 0. */
static uint8_t storage_parse_user(const char *p_text)
{
    char               *p_end = NULL;
    const unsigned long value = strtoul(p_text, &p_end, 10);

    if ((p_end == p_text) || ('\0' != *p_end) || (value < 1UL) || (value > SYSTEM_USER_COUNT))
    {
        return 0U;
    }
    return (uint8_t)value;
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
    int used = snprintf(p_out,
                        out_size,
                        "# Smart lock credentials, written by the lock. PINs are in clear: keep this card in the lock.\n"
                        "VERSION,%u\n",
                        STORAGE_VERSION);

    for (uint8_t i = 0U; (i < SYSTEM_USER_COUNT) && (used >= 0) && ((size_t)used < out_size); i++)
    {
        const unsigned user = i + 1U;

        if ('\0' != p_cred->pins[i][0])
        {
            used += snprintf(&p_out[used], out_size - (size_t)used, "PIN,%u,%s\n", user, p_cred->pins[i]);
        }
        if ((0U != p_cred->card_len[i]) && ((size_t)used < out_size))
        {
            used += snprintf(&p_out[used], out_size - (size_t)used, "CARD,%u,", user);
            for (uint8_t b = 0U; (b < p_cred->card_len[i]) && ((size_t)used < out_size); b++)
            {
                used += snprintf(&p_out[used], out_size - (size_t)used, "%02X", p_cred->cards[i][b]);
            }
            if ((size_t)used < out_size)
            {
                used += snprintf(&p_out[used], out_size - (size_t)used, "\n");
            }
        }
        if (p_cred->fingers[i] && ((size_t)used < out_size))
        {
            used += snprintf(&p_out[used], out_size - (size_t)used, "FINGER,%u\n", user);
        }
    }
    return (used >= 0) && ((size_t)used < out_size);
}

static void storage_wipe(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    memset(&s_cred, 0, sizeof(s_cred));
}
