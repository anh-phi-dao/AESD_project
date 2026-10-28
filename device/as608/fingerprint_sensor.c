/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "fingerprint_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "as608.h"
#include "esp_log.h"
#include "fingerprint_sensor_config.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define FP_TAG "FINGERPRINT"

/***********************************************************************************************************************
 * Private variables
 **********************************************************************************************************************/

static uint8_t s_address[4] = FINGERPRINT_MODULE_ADDRESS;

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Waits until a finger image is captured, or the timeout expires.
 **********************************************************************************************************************/
static esp_err_t fingerprint_wait_for_finger(void)
{
    for (uint32_t waited = 0U; waited < FINGERPRINT_WAIT_TIMEOUT_MS; waited += FINGERPRINT_POLL_PERIOD_MS)
    {
        uint8_t code = DEV_AS608_GenImg(FINGERPRINT_UART_PORT, s_address);
        if (AS608_OK == code)
        {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(FINGERPRINT_POLL_PERIOD_MS));
    }
    return ESP_ERR_TIMEOUT;
}

/***********************************************************************************************************************
 * Waits until the finger is lifted from the sensor.
 **********************************************************************************************************************/
static esp_err_t fingerprint_wait_for_release(void)
{
    for (uint32_t waited = 0U; waited < FINGERPRINT_WAIT_TIMEOUT_MS; waited += FINGERPRINT_POLL_PERIOD_MS)
    {
        if (AS608_ERR_NO_FINGER == DEV_AS608_GenImg(FINGERPRINT_UART_PORT, s_address))
        {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(FINGERPRINT_POLL_PERIOD_MS));
    }
    return ESP_ERR_TIMEOUT;
}

/***********************************************************************************************************************
 * Converts the captured image to a character file in the given buffer.
 **********************************************************************************************************************/
static esp_err_t fingerprint_image_to_buffer(uint8_t buffer_id)
{
    return (AS608_OK == DEV_AS608_Img2Tz(FINGERPRINT_UART_PORT, s_address, &buffer_id)) ? ESP_OK : ESP_FAIL;
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

esp_err_t fingerprint_sensor_init(void)
{
    BSP_uartConfigBaudrate(FINGERPRINT_UART_BAUDRATE);
    BSP_uartConfigDataLen(UART_DATA_8_BITS);
    BSP_uartConfigParity(UART_PARITY_DISABLE);
    BSP_uartConfigStopBits(UART_STOP_BITS_1);
    BSP_uartConfigHWFlowCTRL(UART_HW_FLOWCTRL_DISABLE);
    BSP_uartDriverInit(FINGERPRINT_UART_PORT);
    BSP_uartConfigIO(FINGERPRINT_UART_PORT, FINGERPRINT_UART_TX_PIN, FINGERPRINT_UART_RX_PIN);

    uint16_t  count = 0U;
    esp_err_t ret   = fingerprint_sensor_get_count(&count);
    if (ESP_OK != ret)
    {
        ESP_LOGE(FP_TAG, "AS608 not responding (check wiring / baudrate)");
        return ret;
    }

    ESP_LOGI(FP_TAG, "AS608 ready, %u template(s) stored", count);
    return ESP_OK;
}

esp_err_t fingerprint_sensor_get_count(uint16_t *p_count)
{
    (void)BSP_resetBuffer(FINGERPRINT_UART_PORT);
    uint8_t code = DEV_AS608_TempleteNum(FINGERPRINT_UART_PORT, s_address, p_count);
    return (AS608_OK == code) ? ESP_OK : ESP_FAIL;
}

esp_err_t fingerprint_sensor_enroll(uint16_t page_id)
{
    esp_err_t ret;
    uint8_t   page[2] = {(uint8_t)(page_id >> 8), (uint8_t)(page_id & 0xFFU)};
    uint8_t   buffer_id;

    for (buffer_id = 1U; buffer_id <= 2U; buffer_id++)
    {
        ESP_LOGI(FP_TAG, "Place finger (scan %u/2)...", buffer_id);
        ret = fingerprint_wait_for_finger();
        if (ESP_OK != ret)
        {
            return ret;
        }
        ret = fingerprint_image_to_buffer(buffer_id);
        if (ESP_OK != ret)
        {
            return ret;
        }
        ESP_LOGI(FP_TAG, "Remove finger");
        ret = fingerprint_wait_for_release();
        if (ESP_OK != ret)
        {
            return ret;
        }
    }

    if (AS608_OK != DEV_AS608_RegModel(FINGERPRINT_UART_PORT, s_address))
    {
        return ESP_FAIL; /* the two scans do not match */
    }

    buffer_id = 1U;
    return (AS608_OK == DEV_AS608_Store(FINGERPRINT_UART_PORT, s_address, &buffer_id, page)) ? ESP_OK : ESP_FAIL;
}

esp_err_t fingerprint_sensor_identify(uint16_t *p_page_id)
{
    uint8_t start_page[2] = {0U, 0U};
    uint8_t page_count[2] = {(uint8_t)(FINGERPRINT_LIBRARY_SIZE >> 8), (uint8_t)(FINGERPRINT_LIBRARY_SIZE & 0xFFU)};
    uint8_t buffer_id     = 1U;

    esp_err_t ret = fingerprint_wait_for_finger();
    if (ESP_OK != ret)
    {
        return ret;
    }
    ret = fingerprint_image_to_buffer(buffer_id);
    if (ESP_OK != ret)
    {
        return ret;
    }

    uint8_t code = DEV_AS608_Search(FINGERPRINT_UART_PORT, s_address, &buffer_id, start_page, page_count, p_page_id);
    if (AS608_OK == code)
    {
        return ESP_OK;
    }
    return (AS608_ERR_NO_MATCH == code) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

esp_err_t fingerprint_sensor_delete(uint16_t page_id, uint16_t count)
{
    uint8_t page[2] = {(uint8_t)(page_id >> 8), (uint8_t)(page_id & 0xFFU)};
    uint8_t num[2]  = {(uint8_t)(count >> 8), (uint8_t)(count & 0xFFU)};

    return (AS608_OK == DEV_AS608_DeleteChar(FINGERPRINT_UART_PORT, s_address, page, num)) ? ESP_OK : ESP_FAIL;
}
