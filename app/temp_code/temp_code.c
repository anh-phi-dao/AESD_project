/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "temp_code.h"

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "mbedtls/md.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define TEMP_CODE_LABEL     "LOCKTMP1" /* Ahead of the slot in the HMAC message, so the key serves nothing else. */
#define TEMP_CODE_LABEL_LEN 8U
#define TEMP_CODE_MODULO    100000000UL /* 10^TEMP_CODE_DIGITS */

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static int  temp_code_hex_digit(char c);
static bool temp_code_parse(const char *const p_code, uint32_t *const p_value);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static uint8_t      s_key[TEMP_CODE_SECRET_LEN];
static bool         s_has_key = false;
static portMUX_TYPE s_key_mux = portMUX_INITIALIZER_UNLOCKED;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

app_err_t temp_code_set_secret(const char *const p_hex)
{
    uint8_t key[TEMP_CODE_SECRET_LEN];

    if ((NULL == p_hex) || ('\0' == p_hex[0]))
    {
        taskENTER_CRITICAL(&s_key_mux);
        memset(s_key, 0, sizeof(s_key));
        s_has_key = false;
        taskEXIT_CRITICAL(&s_key_mux);
        return APP_SUCCESS;
    }
    if (TEMP_CODE_SECRET_HEX != strlen(p_hex))
    {
        return APP_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0U; i < TEMP_CODE_SECRET_LEN; i++)
    {
        const int hi = temp_code_hex_digit(p_hex[2U * i]);
        const int lo = temp_code_hex_digit(p_hex[(2U * i) + 1U]);
        if ((hi < 0) || (lo < 0))
        {
            return APP_ERR_INVALID_ARGUMENT;
        }
        key[i] = (uint8_t)((hi << 4) | lo);
    }

    taskENTER_CRITICAL(&s_key_mux);
    memcpy(s_key, key, sizeof(s_key));
    s_has_key = true;
    taskEXIT_CRITICAL(&s_key_mux);
    memset(key, 0, sizeof(key));
    return APP_SUCCESS;
}

temp_code_result_t temp_code_check(const char *const p_code)
{
    uint8_t  key[TEMP_CODE_SECRET_LEN];
    uint32_t entered = 0U;
    bool     has_key;

    taskENTER_CRITICAL(&s_key_mux);
    has_key = s_has_key;
    memcpy(key, s_key, sizeof(key));
    taskEXIT_CRITICAL(&s_key_mux);

    if (!has_key || !temp_code_parse(p_code, &entered))
    {
        memset(key, 0, sizeof(key));
        return TEMP_CODE_WRONG;
    }

    const time_t now = time(NULL);
    if (now < (time_t)TEMP_CODE_TIME_MIN)
    {
        memset(key, 0, sizeof(key));
        return TEMP_CODE_NO_TIME;
    }

    /* Every slot of the window is computed, whatever matches first, so the time taken says nothing about the code. */
    const uint32_t now_slot = (uint32_t)(now / 60);
    bool           match    = false;
    for (uint32_t back = 0U; back < (TEMP_CODE_VALID_MIN + TEMP_CODE_AHEAD_MIN); back++)
    {
        uint32_t code = 0U;
        if ((APP_SUCCESS == temp_code_at(key, sizeof(key), (now_slot + TEMP_CODE_AHEAD_MIN) - back, &code))
            && (code == entered))
        {
            match = true;
        }
    }
    memset(key, 0, sizeof(key));
    return match ? TEMP_CODE_OK : TEMP_CODE_WRONG;
}

app_err_t temp_code_at(const uint8_t *const p_key, size_t key_len, uint32_t slot, uint32_t *const p_code)
{
    const mbedtls_md_info_t *const p_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t                        message[TEMP_CODE_LABEL_LEN + 4U];
    uint8_t                        mac[32];

    if ((NULL == p_key) || (NULL == p_code) || (NULL == p_info))
    {
        return APP_ERR_INVALID_POINTER;
    }
    memcpy(message, TEMP_CODE_LABEL, TEMP_CODE_LABEL_LEN);
    message[TEMP_CODE_LABEL_LEN]      = (uint8_t)(slot >> 24);
    message[TEMP_CODE_LABEL_LEN + 1U] = (uint8_t)(slot >> 16);
    message[TEMP_CODE_LABEL_LEN + 2U] = (uint8_t)(slot >> 8);
    message[TEMP_CODE_LABEL_LEN + 3U] = (uint8_t)slot;
    if (0 != mbedtls_md_hmac(p_info, p_key, key_len, message, sizeof(message), mac))
    {
        return APP_FAIL;
    }

    /* Dynamic truncation of HOTP (RFC 4226), on SHA-256. */
    const uint8_t  offset = mac[sizeof(mac) - 1U] & 0x0FU;
    const uint32_t value  = (((uint32_t)mac[offset] & 0x7FU) << 24) | ((uint32_t)mac[offset + 1U] << 16)
                           | ((uint32_t)mac[offset + 2U] << 8) | (uint32_t)mac[offset + 3U];
    *p_code = value % TEMP_CODE_MODULO;
    return APP_SUCCESS;
}

bool temp_code_self_test(void)
{
    /* Key 00 01 .. 0f, from scripts/temp_code.py (web/js/temp-code.js has the same ones). */
    static const struct
    {
        uint32_t slot;
        uint32_t code;
    } s_vectors[] = {{0U, 80970255U}, {29833333U, 55184857U}, {29833334U, 74724184U}, {0xFFFFFFFFU, 32480546U}};
    uint8_t key[TEMP_CODE_SECRET_LEN];

    for (uint8_t i = 0U; i < TEMP_CODE_SECRET_LEN; i++)
    {
        key[i] = i;
    }
    for (size_t i = 0U; i < (sizeof(s_vectors) / sizeof(s_vectors[0])); i++)
    {
        uint32_t code = 0U;
        if ((APP_SUCCESS != temp_code_at(key, sizeof(key), s_vectors[i].slot, &code)) || (code != s_vectors[i].code))
        {
            return false;
        }
    }
    return true;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

static int temp_code_hex_digit(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return (c - 'a') + 10;
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return (c - 'A') + 10;
    }
    return -1;
}

/* Exactly TEMP_CODE_DIGITS digits, leading zeros included. */
static bool temp_code_parse(const char *const p_code, uint32_t *const p_value)
{
    uint32_t value = 0U;

    if ((NULL == p_code) || (TEMP_CODE_DIGITS != strlen(p_code)))
    {
        return false;
    }
    for (size_t i = 0U; i < TEMP_CODE_DIGITS; i++)
    {
        if ((p_code[i] < '0') || (p_code[i] > '9'))
        {
            return false;
        }
        value = (value * 10U) + (uint32_t)(p_code[i] - '0');
    }
    *p_value = value;
    return true;
}
