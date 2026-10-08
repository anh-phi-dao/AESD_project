#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "pn532_driver.h"

extern void i2c_bus_lock(void);
extern void i2c_bus_unlock(void);
extern SemaphoreHandle_t i2c_bus_get_mutex(void);

__attribute__((weak)) void pn532_platform_recover_i2c(void) {}

static const char TAG[] = "pn532_driver";

#ifndef CONFIG_ENABLE_IRQ_ISR
static bool pn532_is_ready();
#endif

#define PN532_COMMAND_BUFFER_LEN 64

#ifdef CONFIG_ENABLE_IRQ_ISR
/**
 *  @brief  ISR for PN532 to indicate data is available.
 *  @param  arg PN532 io handle
 */

static void IRAM_ATTR pn532_irq_handler(void *arg)
{
    pn532_io_handle_t io = (pn532_io_handle_t)arg;
    gpio_intr_disable(io->irq);

#if CONFIG_ENABLE_IRQ_ISR
    if (io->IRQQueue)
    {
        BaseType_t hp = pdFALSE;
        xQueueOverwriteFromISR(io->IRQQueue, &io->irq, &hp);
        if (hp)
            portYIELD_FROM_ISR();
    }
#endif
}
#endif

// === PN532 timeout recovery config ===
#define PN532_TIMEOUT_STREAK_MAX 5   // quá 5 lần timeout liên tiếp thì phục hồi
#define PN532_RECOVER_COOLDOWN_MS 20 // nghỉ một nhịp ngắn trước khi init lại
#define PN532_RETRY_AFTER_RECOVER 1  // 1 = thử gửi lại đúng lệnh vừa fail

static int s_timeout_streak = 0;                 // đếm chuỗi timeout liên tiếp
static SemaphoreHandle_t s_recover_mutex = NULL; // tránh 2 task phục hồi cùng lúc

// static esp_err_t pn532_try_recover(pn532_io_handle_t io)
// {
//     if (s_recover_mutex == NULL) {
//         s_recover_mutex = xSemaphoreCreateMutex();
//         if (s_recover_mutex == NULL) return ESP_FAIL;
//     }

//     // Nếu đang ở trong ISR thì KHÔNG phục hồi ở đây
//     if (xPortInIsrContext()) {
//         return ESP_ERR_INVALID_STATE;
//     }

//     esp_err_t ret = ESP_OK;

//     if (xSemaphoreTake(s_recover_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
//         ESP_LOGW("pn532_driver", "⚠️  Timeout streak >= %d → trying to recover PN532 ...",
//                  PN532_TIMEOUT_STREAK_MAX);
//         // Thả thiết bị (KHÔNG xoá I2C driver nếu đang dùng shared bus)
//         pn532_release(io);

//         vTaskDelay(pdMS_TO_TICKS(PN532_RECOVER_COOLDOWN_MS));

//         ret = pn532_init(io);
//         if (ret == ESP_OK) {
//             ESP_LOGW("pn532_driver", "✅ PN532 recovered successfully");
//             s_timeout_streak = 0;  // reset chuỗi timeout
//         } else {
//             ESP_LOGE("pn532_driver", "❌ PN532 recover failed: %s", esp_err_to_name(ret));
//         }
//         xSemaphoreGive(s_recover_mutex);
//     } else {
//         // Có task khác đang phục hồi
//         ret = ESP_ERR_TIMEOUT;
//     }
//     return ret;
// }

static esp_err_t pn532_try_recover(pn532_io_handle_t io)
{
    // Lấy mutex (nếu có). Dùng recursive nên không sợ deadlock từ cùng task.
    if (i2c_bus_get_mutex())
        i2c_bus_lock();

    if (s_recover_mutex == NULL)
    {
        s_recover_mutex = xSemaphoreCreateMutex();
        if (s_recover_mutex == NULL)
        {
            if (i2c_bus_get_mutex())
                i2c_bus_unlock();
            return ESP_FAIL;
        }
    }

    esp_err_t ret = ESP_OK;

    if (xSemaphoreTake(s_recover_mutex, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        ESP_LOGW("pn532_driver", "⚠️  Timeout streak >= %d → recovering PN532 ...",
                 PN532_TIMEOUT_STREAK_MAX);

        // 1) Thả PN532 layer (không gỡ driver shared nếu bus_created=false)
        pn532_release(io);
        vTaskDelay(pdMS_TO_TICKS(PN532_RECOVER_COOLDOWN_MS));

        // 2) (Khuyến nghị) Recover bus vật lý nếu bạn đã cài hook (uninstall→pulse SCL→reinstall)
        pn532_platform_recover_i2c();

        // 3) Init lại PN532 (SAM_config, v.v.)
        ret = pn532_init(io);
        if (ret == ESP_OK)
        {
            ESP_LOGW("pn532_driver", "✅ PN532 recovered successfully");
            s_timeout_streak = 0;
        }
        else
        {
            ESP_LOGE("pn532_driver", "❌ PN532 recover failed: %s", esp_err_to_name(ret));
        }

        xSemaphoreGive(s_recover_mutex);
    }
    else
    {
        ret = ESP_ERR_TIMEOUT; // Có thread khác đang recover
    }

    // MỞ KHÓA BUS dù thành công hay thất bại
    if (i2c_bus_get_mutex())
        i2c_bus_unlock();
    return ret;
}

esp_err_t pn532_init(pn532_io_handle_t io_handle)
{
    gpio_config_t io_conf;

    if (io_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    if (io_handle->reset != GPIO_NUM_NC)
    {
        // configure Reset pin
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = ((1ULL) << io_handle->reset);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 1;
        if (gpio_config(&io_conf) != ESP_OK)
            return ESP_FAIL;
    }

#ifdef CONFIG_ENABLE_IRQ_ISR
    if (io_handle->irq != GPIO_NUM_NC)
    {
        if (io_handle->IRQQueue != NULL)
            vQueueDelete(io_handle->IRQQueue);
        // create a queue to handle gpio event from isr
        io_handle->IRQQueue = xQueueCreate(1, sizeof(gpio_num_t));

        // install IRQ handler
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        gpio_isr_handler_add(io_handle->irq, pn532_irq_handler, (void *)io_handle);
        ESP_LOGD(TAG, "pn532_init(): using IRQ line with ISR.");
    }
#else
    ESP_LOGI(TAG, "pn532_init(): using IRQ line in polling mode.");
#endif

    if (io_handle->irq != GPIO_NUM_NC)
    {
        // configure IRQ pin
#ifdef CONFIG_ENABLE_IRQ_ISR
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
#else
        io_conf.intr_type = GPIO_INTR_DISABLE;
#endif
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = ((1ULL) << io_handle->irq);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 1;
        if (gpio_config(&io_conf) != ESP_OK)
            return ESP_FAIL;
    }

    // Reset the PN532
    if (io_handle->reset != GPIO_NUM_NC)
    {
        ESP_LOGD(TAG, "reset PN532 ...");
        pn532_reset(io_handle);
        ESP_LOGD(TAG, "reset done");
    }

    ESP_LOGI(TAG, "pn532_init(): call pn532_init_io() ...");
    io_handle->isSAMConfigDone = false;
    esp_err_t err = io_handle->pn532_init_io(io_handle);
    if (err != ESP_OK)
        return err;

#ifdef CONFIG_ENABLE_IRQ_ISR
    // clear queue after successful initialization
    if (io_handle->irq >= 0)
        xQueueReset(io_handle->IRQQueue);
#endif

    ESP_LOGI(TAG, "pn532_init(): call pn532_SAM_config() ...");
    err = pn532_SAM_config(io_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "pn532_init(): failed ...");
        return err;
    }

    io_handle->isSAMConfigDone = true;

    ESP_LOGI(TAG, "pn532_init(): call pn532_init_extra() ...");
    if (io_handle->pn532_init_extra != NULL)
    {
        err = io_handle->pn532_init_extra(io_handle);
        if (err != ESP_OK)
            return err;
    }

    ESP_LOGI(TAG, "init ok");
    return err;
}

void pn532_release(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL)
        return;

    ESP_LOGD(TAG, "call pn532_release_io() ...");
    if (io_handle->pn532_release_io != NULL)
    {
        io_handle->pn532_release_io(io_handle);
    }

#ifdef CONFIG_ENABLE_IRQ_ISR
    if (io_handle->irq != GPIO_NUM_NC)
    {
        ESP_LOGD(TAG, "remove irq handler");
        gpio_isr_handler_remove(io_handle->irq);
        gpio_uninstall_isr_service();

        if (io_handle->IRQQueue != NULL)
        {
            ESP_LOGD(TAG, "delete IRQ queue");
            vQueueDelete(io_handle->IRQQueue);
            io_handle->IRQQueue = NULL;
        }
    }
#endif

    if (io_handle->reset != GPIO_NUM_NC)
    {
        ESP_LOGD(TAG, "release reset pin");
        gpio_reset_pin(io_handle->reset);
    }

    if (io_handle->irq != GPIO_NUM_NC)
    {
        ESP_LOGD(TAG, "release irq pin");
        gpio_reset_pin(io_handle->irq);
    }
}

void pn532_delete_driver(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL)
        return;

    if (io_handle->pn532_release_driver != NULL)
        io_handle->pn532_release_driver(io_handle);
}

void pn532_reset(pn532_io_handle_t io_handle)
{
    if (io_handle->reset == GPIO_NUM_NC)
        return;

    gpio_set_level(io_handle->reset, 0);
    vTaskDelay(400 / portTICK_PERIOD_MS);
    gpio_set_level(io_handle->reset, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    io_handle->isSAMConfigDone = false;
}

// esp_err_t pn532_write_command(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmdlen, int timeout)
// {
//     static uint8_t command[256];

//     uint8_t checksum = PN532_HOST_TO_PN532;

//     int idx = 0;
//     command[idx++] = PN532_STARTCODE1;
//     command[idx++] = PN532_STARTCODE2;
//     command[idx++] = (cmdlen + 1);
//     command[idx++] = 0x100 - (cmdlen + 1);
//     command[idx++] = PN532_HOST_TO_PN532;

//     uint8_t i = 0;
//     for (i = 0; i < cmdlen; i++) {
//         command[idx++] = cmd[i];
//         checksum += cmd[i];
//     }
//     command[idx++] = ~checksum + 1;

// #ifdef CONFIG_PN532DEBUG
//     ESP_LOGD(TAG, "%s Sending :", __func__);
//     esp_log_buffer_hex(TAG, command, idx);
// #endif

// //    vTaskDelay(pdMS_TO_TICKS(100));

// //    ESP_LOGI(TAG, "going to send command ...");
//     esp_err_t result = io_handle->pn532_write(io_handle, command, idx, timeout);

//     if (result != ESP_OK) {
//         char *resultText = NULL;
//         switch (result) {
//             case ESP_ERR_INVALID_ARG:
//                 resultText = "parameter error";
//                 break;
//             case ESP_FAIL:
//                 resultText = "send command failed";
//                 break;
//             case ESP_ERR_INVALID_STATE:
//                 resultText = "invalid state";
//                 break;
//             case ESP_ERR_TIMEOUT:
//                 resultText = "timeout occurred";
//                 break;
//             default:
//                 resultText = "unknown error";

//         }
//         ESP_LOGW(TAG, "%s write failed: %s!", __func__, resultText);
//     }
//     return result;
// }

esp_err_t pn532_write_command(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmdlen, int timeout)
{
    static uint8_t command[256];
    uint8_t checksum = PN532_HOST_TO_PN532;

    int idx = 0;
    command[idx++] = PN532_STARTCODE1;
    command[idx++] = PN532_STARTCODE2;
    command[idx++] = (cmdlen + 1);
    command[idx++] = 0x100 - (cmdlen + 1);
    command[idx++] = PN532_HOST_TO_PN532;

    uint8_t i = 0;
    for (i = 0; i < cmdlen; i++)
    {
        command[idx++] = cmd[i];
        checksum += cmd[i];
    }
    command[idx++] = ~checksum + 1;

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "%s Sending :", __func__);
    esp_log_buffer_hex(TAG, command, idx);
#endif

    // ===== Added recovery state tracking =====
    static int s_timeout_streak = 0;
    static SemaphoreHandle_t s_recover_mutex = NULL;

    if (s_recover_mutex == NULL)
    {
        s_recover_mutex = xSemaphoreCreateMutex();
    }

    esp_err_t result = io_handle->pn532_write(io_handle, command, idx, timeout);

    if (result == ESP_OK)
    {
        s_timeout_streak = 0; // reset streak on success
        return ESP_OK;
    }

    // ===== Error handling (kept your original style) =====
    char *resultText = NULL;
    switch (result)
    {
    case ESP_ERR_INVALID_ARG:
        resultText = "parameter error";
        break;
    case ESP_FAIL:
        resultText = "send command failed";
        break;
    case ESP_ERR_INVALID_STATE:
        resultText = "invalid state";
        break;
    case ESP_ERR_TIMEOUT:
        resultText = "timeout occurred";
        break;
    default:
        resultText = "unknown error";
    }

    ESP_LOGW(TAG, "%s write failed: %s!", __func__, resultText);

    // ===== Added timeout recovery logic =====
    if (result == ESP_ERR_TIMEOUT)
    {
        s_timeout_streak++;

        if (s_timeout_streak >= 5)
        { // sau 5 lần timeout liên tiếp thì phục hồi
            if (xSemaphoreTake(s_recover_mutex, pdMS_TO_TICKS(200)) == pdTRUE)
            {
                ESP_LOGW(TAG, "⚠️ PN532 timeout streak >=5 → reinitializing module...");
                pn532_release(io_handle);
                vTaskDelay(pdMS_TO_TICKS(20)); // nghỉ một nhịp
                esp_err_t r = pn532_init(io_handle);
                if (r == ESP_OK)
                {
                    ESP_LOGW(TAG, "✅ PN532 recovered successfully");
                    s_timeout_streak = 0;
                }
                else
                {
                    ESP_LOGE(TAG, "❌ PN532 recover failed: %s", esp_err_to_name(r));
                }
                xSemaphoreGive(s_recover_mutex);
            }
        }
    }
    else
    {
        s_timeout_streak = 0; // lỗi khác timeout → reset chuỗi
    }

    return result;
}

esp_err_t pn532_read_data(pn532_io_handle_t io_handle, uint8_t *buffer, uint8_t length, int32_t timeout)
{
    static uint8_t local_buffer[512];

    bzero(local_buffer, sizeof(local_buffer));

    if (timeout == 0)
    {
        timeout = -1;
    }

    esp_err_t res = io_handle->pn532_read(io_handle, local_buffer, length, timeout);
    if (res != ESP_OK)
    {
        return res;
    }

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "Reading: ");
    esp_log_buffer_hex(TAG, local_buffer, length);
#endif

    // copy data without preamble
    memcpy(buffer, local_buffer, length);
    return ESP_OK;
}

#ifndef CONFIG_ENABLE_IRQ_ISR
/**
 * Check if data is ready
 * @param io_handle PN532 io handle
 * @return true, if ready
 */
bool pn532_is_ready(pn532_io_handle_t io_handle)
{
    // check if status is ready by IRQ line being pulled low.
    uint8_t x = gpio_get_level(io_handle->irq);
#ifdef CONFIG_IRQDEBUG
    ESP_LOGI(TAG, "IRQ: %d", x);
#endif
    return (x == 0);
}
#endif
static esp_err_t pn532_poll_ready(pn532_io_handle_t io_handle, int32_t timeout)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = (timeout > 0) ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
    TickType_t elapsed_ticks = 0;

    esp_rom_delay_us(1000);

    bool is_ready = false;
    while (!is_ready && elapsed_ticks <= timeout_ticks)
    {
        is_ready = ESP_OK == io_handle->pn532_is_ready(io_handle);
        if (!is_ready)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            elapsed_ticks = xTaskGetTickCount() - start_ticks;
        }
    }

    return is_ready ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t pn532_wait_ready(pn532_io_handle_t io_handle, int32_t timeout)
{
    if (io_handle->irq == GPIO_NUM_NC)
    {
        esp_err_t err = ESP_OK;
        if (io_handle->pn532_is_ready != NULL)
        {
            err = pn532_poll_ready(io_handle, timeout);
        }
        return err;
    }

#ifdef CONFIG_ENABLE_IRQ_ISR
    uint32_t io_num = 0;
    TickType_t delay = 0;

    if (timeout == 0)
        delay = portMAX_DELAY;
    else
        delay = timeout / portTICK_PERIOD_MS;

    BaseType_t res = xQueueReceive(io_handle->IRQQueue, &io_num, delay);

    if (res)
    {
        return ESP_OK;
    }

    return ESP_FAIL;
#else
    uint16_t timer = 0;
    while (!pn532_is_ready(io_handle))
    {
        if (timeout != 0)
        {
            timer += 10;
            if (timer > timeout)
            {
#ifdef CONFIG_PN532DEBUG
                ESP_LOGE(TAG, "Wait ready TIMEOUT after %d ms!", (int)timeout);
#endif
                return ESP_ERR_TIMEOUT;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
#endif
}

// esp_err_t pn532_SAM_config(pn532_io_handle_t io_handle)
// {
//     esp_err_t result;
//     uint8_t response_buffer[16];

//     if (io_handle == NULL || io_handle->driver_data == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     static const uint8_t sam_config_frame[] = { 0x14, 0x01, 0x00, 0x01 }; // normal mode and use IRQ pin

//     result = pn532_send_command_wait_ack(io_handle, sam_config_frame, sizeof(sam_config_frame), 1000);
//     if (ESP_OK != result)
//         return result;

// #ifdef CONFIG_PN532DEBUG
//     ESP_LOGD(TAG, "pn532_SAM_config(): Waiting for IRQ/ready");
// #endif
//     result = pn532_wait_ready(io_handle, 100);
//     if (ESP_OK != result) {
// #ifdef CONFIG_PN532DEBUG
//         ESP_LOGD(TAG, "pn532_SAM_config(): Timeout occurred");
// #endif
//         return result;
//     }

//     // read data packet
//     result = pn532_read_data(io_handle, response_buffer, 10, PN532_READ_TIMEOUT);
//     if (ESP_OK != result)
//         return result;

//     if (response_buffer[6] != 0x15) {
//         return ESP_FAIL;
//     }
//     return ESP_OK;
// }

esp_err_t pn532_SAM_config(pn532_io_handle_t io_handle)
{
    esp_err_t result;
    uint8_t response_buffer[16];

    if (io_handle == NULL || io_handle->driver_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    /*if use irq */
    const uint8_t use_irq = (io_handle->irq == GPIO_NUM_NC) ? 0x00 : 0x01;
    uint8_t sam_config_frame[] = {0x14, 0x01, 0x00, use_irq};

    result = pn532_send_command_wait_ack(io_handle, sam_config_frame, sizeof(sam_config_frame), 1000);
    if (ESP_OK != result)
        return result;

#ifdef CONFIG_PN532DEBUG
    ESP_LOGI(TAG, "pn532_SAM_config(): Waiting for IRQ/ready");
#endif

    result = pn532_wait_ready(io_handle, 1000);
    if (ESP_OK != result)
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGI(TAG, "pn532_SAM_config(): Timeout occurred");
#endif
        return result;
    }

    // read data packet
    result = pn532_read_data(io_handle, response_buffer, 10, PN532_READ_TIMEOUT);
    if (ESP_OK != result)
        return result;

    if (response_buffer[6] != 0x15)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t pn532_send_command_wait_ack(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmd_length, int32_t timeout)
{
    esp_err_t result;

    if (io_handle == NULL || io_handle->driver_data == NULL || cmd == NULL || cmd_length == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // write the command
    result = pn532_write_command(io_handle, cmd, cmd_length, timeout);
    if (result != ESP_OK)
    {
        return result;
    }

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "pn532_send_command_wait_ack(): Waiting for PN532 IRQ/ready");
#endif
    result = pn532_wait_ready(io_handle, timeout);
    if (result != ESP_OK)
    {
#ifdef CONFIG_PN532DEBUG
        if (result == ESP_ERR_TIMEOUT)
            ESP_LOGE(TAG, "pn532_send_command_wait_ack(): Timeout occurred!");
#endif
        ESP_LOGD(TAG, "pn532_wait_ready() failed with 0x%X", result);
        return result;
    }

    // read acknowledgement

    result = pn532_read_ack(io_handle);
    if (result != ESP_OK)
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "pn532_send_command_wait_ack(): No ACK frame received!");
#endif
    }

    return result;
}

esp_err_t pn532_read_ack(pn532_io_handle_t io_handle)
{
    uint8_t ack_buffer[6];
    esp_err_t result;

    if (io_handle == NULL || io_handle->driver_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    result = pn532_read_data(io_handle, ack_buffer, sizeof(ACK_FRAME), PN532_READ_TIMEOUT);
    if (result != ESP_OK)
        return result;

    if (0 != memcmp(ack_buffer, ACK_FRAME, sizeof(ACK_FRAME)))
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}
