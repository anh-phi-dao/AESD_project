/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "as608.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define AS608_TAG              "AS608"
#define AS608_HEADER_HIGH      0xEFU
#define AS608_HEADER_LOW       0x01U
#define AS608_PID_COMMAND      0x01U
#define AS608_PID_ACK          0x07U
#define AS608_ADDRESS_LEN      4U
#define AS608_CHECKSUM_LEN     2U
#define AS608_RX_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
#define AS608_HEADER_SCAN_MAX  16U /* bytes skipped while looking for a header before giving up */
#define AS608_DATA_MAX         8U  /* largest command payload used by this driver */
#define AS608_ACK_MAX          32U /* confirmation code + data + checksum of a response */

#define AS608_INSTR_GEN_IMG       0x01U
#define AS608_INSTR_IMG2TZ        0x02U
#define AS608_INSTR_SEARCH        0x04U
#define AS608_INSTR_REG_MODEL     0x05U
#define AS608_INSTR_STORE         0x06U
#define AS608_INSTR_DELETE_CHAR   0x0CU
#define AS608_INSTR_TEMPLATE_NUM  0x1DU

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Reads one byte from the UART, returns false on timeout.
 **********************************************************************************************************************/
static bool as608_read_byte(uart_port_num_t e_uart_port, uint8_t *p_byte)
{
    return BSP_uartReadByte(e_uart_port, p_byte, AS608_RX_TIMEOUT_TICKS) > 0;
}

/***********************************************************************************************************************
 * Builds a command packet (header, address, length, instruction, data, checksum) and sends it.
 **********************************************************************************************************************/
static void as608_send_command(uart_port_num_t e_uart_port,
                               const uint8_t  *p_address,
                               uint8_t         instruction,
                               const uint8_t  *p_data,
                               uint8_t         data_len)
{
    uint8_t  packet[2U + AS608_ADDRESS_LEN + 3U + 1U + AS608_DATA_MAX + AS608_CHECKSUM_LEN];
    uint8_t  index    = 0U;
    uint16_t length   = (uint16_t)(1U + data_len + AS608_CHECKSUM_LEN); /* instruction + data + checksum */
    uint16_t checksum = 0U;

    packet[index++] = AS608_HEADER_HIGH;
    packet[index++] = AS608_HEADER_LOW;
    memcpy(&packet[index], p_address, AS608_ADDRESS_LEN);
    index += AS608_ADDRESS_LEN;
    packet[index++] = AS608_PID_COMMAND;
    packet[index++] = (uint8_t)(length >> 8);
    packet[index++] = (uint8_t)(length & 0xFFU);
    packet[index++] = instruction;
    if (data_len > 0U)
    {
        memcpy(&packet[index], p_data, data_len);
        index += data_len;
    }

    /* The checksum covers everything after the address, up to the end of the data. */
    for (uint8_t i = 2U + AS608_ADDRESS_LEN; i < index; i++)
    {
        checksum = (uint16_t)(checksum + packet[i]);
    }
    packet[index++] = (uint8_t)(checksum >> 8);
    packet[index++] = (uint8_t)(checksum & 0xFFU);

    (void)BSP_resetBuffer(e_uart_port); /* drop stale bytes of an earlier response */
    (void)BSP_uartSendData(e_uart_port, packet, index);
}

/***********************************************************************************************************************
 * Sends a command and returns the confirmation code of its response.
 **********************************************************************************************************************/
static uint8_t as608_transact(uart_port_num_t e_uart_port,
                              const uint8_t  *p_address,
                              uint8_t         instruction,
                              const uint8_t  *p_data,
                              uint8_t         data_len,
                              uint16_t       *p_value)
{
    as608_send_command(e_uart_port, p_address, instruction, p_data, data_len);
    return DEV_AS608_Reponse(e_uart_port, instruction, p_value);
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

uint8_t DEV_AS608_Reponse(uart_port_num_t e_uart_port, uint8_t instruction_code, uint16_t *p_value)
{
    uint8_t  byte   = 0U;
    uint8_t  ack[AS608_ACK_MAX];
    uint8_t  pid    = 0U;
    uint16_t length = 0U;
    bool     synced = false;

    /* Find the 0xEF 0x01 header. */
    for (uint8_t i = 0U; (i < AS608_HEADER_SCAN_MAX) && !synced; i++)
    {
        if (!as608_read_byte(e_uart_port, &byte))
        {
            return AS608_ERR_COMM;
        }
        if (AS608_HEADER_HIGH == byte)
        {
            synced = as608_read_byte(e_uart_port, &byte) && (AS608_HEADER_LOW == byte);
        }
    }
    if (!synced)
    {
        return AS608_ERR_COMM;
    }

    /* Skip the module address, then read the packet identifier and length. */
    for (uint8_t i = 0U; i < AS608_ADDRESS_LEN; i++)
    {
        if (!as608_read_byte(e_uart_port, &byte))
        {
            return AS608_ERR_COMM;
        }
    }
    uint8_t length_bytes[2];
    if (!as608_read_byte(e_uart_port, &pid) || !as608_read_byte(e_uart_port, &length_bytes[0]) ||
        !as608_read_byte(e_uart_port, &length_bytes[1]))
    {
        return AS608_ERR_COMM;
    }
    length = (uint16_t)(((uint16_t)length_bytes[0] << 8) | length_bytes[1]);

    if ((AS608_PID_ACK != pid) || (length < (1U + AS608_CHECKSUM_LEN)) || (length > AS608_ACK_MAX))
    {
        (void)BSP_resetBuffer(e_uart_port);
        return AS608_ERR_COMM;
    }

    for (uint16_t i = 0U; i < length; i++)
    {
        if (!as608_read_byte(e_uart_port, &ack[i]))
        {
            return AS608_ERR_COMM;
        }
    }

    uint16_t checksum = (uint16_t)(pid + length_bytes[0] + length_bytes[1]);
    for (uint16_t i = 0U; i < (length - AS608_CHECKSUM_LEN); i++)
    {
        checksum = (uint16_t)(checksum + ack[i]);
    }
    if (checksum != (uint16_t)(((uint16_t)ack[length - 2U] << 8) | ack[length - 1U]))
    {
        ESP_LOGW(AS608_TAG, "Instruction 0x%02X: response checksum mismatch", instruction_code);
        return AS608_ERR_COMM;
    }

    /* ack[0] is the confirmation code, ack[1..2] the first 16-bit value (template count / matched page id). */
    if ((NULL != p_value) && (length >= (1U + 2U + AS608_CHECKSUM_LEN)))
    {
        *p_value = (uint16_t)(((uint16_t)ack[1] << 8) | ack[2]);
    }

    ESP_LOGD(AS608_TAG, "Instruction 0x%02X: confirmation code 0x%02X", instruction_code, ack[0]);
    return ack[0];
}

uint8_t DEV_AS608_GenImg(uart_port_num_t e_uart_port, uint8_t *p_AS608_address)
{
    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_GEN_IMG, NULL, 0U, NULL);
}

uint8_t DEV_AS608_Img2Tz(uart_port_num_t e_uart_port, uint8_t *p_AS608_address, uint8_t *p_buffer_id)
{
    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_IMG2TZ, p_buffer_id, 1U, NULL);
}

uint8_t DEV_AS608_Search(uart_port_num_t e_uart_port,
                         uint8_t        *p_AS608_address,
                         uint8_t        *p_buffer_id,
                         uint8_t        *p_start_page,
                         uint8_t        *p_page_number,
                         uint16_t       *p_matched_page)
{
    const uint8_t data[5] = {p_buffer_id[0], p_start_page[0], p_start_page[1], p_page_number[0], p_page_number[1]};

    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_SEARCH, data, sizeof(data), p_matched_page);
}

uint8_t DEV_AS608_RegModel(uart_port_num_t e_uart_port, uint8_t *p_AS608_address)
{
    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_REG_MODEL, NULL, 0U, NULL);
}

uint8_t DEV_AS608_Store(uart_port_num_t e_uart_port,
                        uint8_t        *p_AS608_address,
                        uint8_t        *p_buffer_id,
                        uint8_t        *p_page_id)
{
    const uint8_t data[3] = {p_buffer_id[0], p_page_id[0], p_page_id[1]};

    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_STORE, data, sizeof(data), NULL);
}

uint8_t DEV_AS608_TempleteNum(uart_port_num_t e_uart_port, uint8_t *p_AS608_address, uint16_t *p_template_count)
{
    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_TEMPLATE_NUM, NULL, 0U, p_template_count);
}

uint8_t DEV_AS608_DeleteChar(uart_port_num_t e_uart_port,
                             uint8_t        *p_AS608_address,
                             uint8_t        *p_page_id,
                             uint8_t        *p_number_of_templates)
{
    const uint8_t data[4] = {p_page_id[0], p_page_id[1], p_number_of_templates[0], p_number_of_templates[1]};

    return as608_transact(e_uart_port, p_AS608_address, AS608_INSTR_DELETE_CHAR, data, sizeof(data), NULL);
}
