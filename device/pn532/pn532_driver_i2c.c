#include <string.h>
#include "pn532_driver.h"
#include "pn532_driver_i2c.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char TAG[] = "pn532_driver_i2c";

#define PN532_USE_LEGACY_I2C 0
#define PN532_I2C_RAW_ADDRESS (0x24)

typedef struct
{
    gpio_num_t sda;
    gpio_num_t scl;
    i2c_port_num_t i2c_port_number;
    bool bus_created; // LUÔN CÓ FIELD NÀY
#if !PN532_USE_LEGACY_I2C
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_dev_handle_t i2c_dev_handle;
#endif
    uint8_t frame_buffer[512];
} pn532_i2c_driver_config;

static esp_err_t pn532_init_io(pn532_io_handle_t io_handle);
static void pn532_release_driver(pn532_io_handle_t io_handle);
static void pn532_release_io(pn532_io_handle_t io_handle);
static esp_err_t pn532_read(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms);
static esp_err_t pn532_write(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms);
static esp_err_t pn532_is_ready(pn532_io_handle_t io_handle);

static inline TickType_t ms2tick(int ms)
{
    return (ms > 0) ? pdMS_TO_TICKS(ms) : portMAX_DELAY;
}

esp_err_t pn532_new_driver_i2c(gpio_num_t sda,
                               gpio_num_t scl,
                               gpio_num_t reset,
                               gpio_num_t irq,
                               i2c_port_num_t i2c_port_number,
                               pn532_io_handle_t io_handle)
{
    if (io_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    if (i2c_port_number < 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *dev_config = heap_caps_calloc(1, sizeof(pn532_i2c_driver_config), MALLOC_CAP_DEFAULT);
    if (dev_config == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    io_handle->reset = reset;
    io_handle->irq = irq;

    dev_config->i2c_port_number = i2c_port_number;
    dev_config->scl = scl;
    dev_config->sda = sda;
    dev_config->bus_created = false;
    io_handle->driver_data = dev_config;

    io_handle->pn532_init_io = pn532_init_io;
    io_handle->pn532_release_io = pn532_release_io;
    io_handle->pn532_release_driver = pn532_release_driver;
    io_handle->pn532_read = pn532_read;
    io_handle->pn532_write = pn532_write;
    io_handle->pn532_init_extra = NULL;
    io_handle->pn532_is_ready = pn532_is_ready;

#ifdef CONFIG_ENABLE_IRQ_ISR
    io_handle->IRQQueue = NULL;
#endif

    return ESP_OK;
}

void pn532_release_driver(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL || io_handle->driver_data == NULL)
        return;

    pn532_release_io(io_handle);

    free(io_handle->driver_data);
    io_handle->driver_data = NULL;
}

esp_err_t pn532_init_io(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL || io_handle->driver_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *cfg = (pn532_i2c_driver_config *)io_handle->driver_data;

#if PN532_USE_LEGACY_I2C
    // LEGACY MODE: Sử dụng legacy I2C API
    cfg->bus_created = false;

    if (cfg->scl != GPIO_NUM_NC && cfg->sda != GPIO_NUM_NC)
    {
        // Tự tạo I2C driver
        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = cfg->sda,
            .scl_io_num = cfg->scl,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = 100000,
        };

        esp_err_t err = i2c_param_config(cfg->i2c_port_number, &conf);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
            return err;
        }

        err = i2c_driver_install(cfg->i2c_port_number, conf.mode, 0, 0, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
            return err;
        }

        cfg->bus_created = true;
    }
    // Nếu GPIO_NUM_NC thì giả định I2C đã được init ở nơi khác

    return ESP_OK;

#else
    // API MỚI
    if (cfg->i2c_bus_handle != NULL && cfg->bus_created)
    {
        pn532_release_io(io_handle);
    }

    cfg->bus_created = false;

    if (cfg->scl != GPIO_NUM_NC && cfg->sda != GPIO_NUM_NC)
    {
        // Tự tạo bus mới
        i2c_master_bus_config_t conf = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = cfg->i2c_port_number,
            .sda_io_num = cfg->sda,
            .scl_io_num = cfg->scl,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&conf, &cfg->i2c_bus_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_new_master_bus() failed (%s)", esp_err_to_name(err));
            return err;
        }
        cfg->bus_created = true;
    }
    else
    {
        // Lấy handle bus đã tạo bằng API mới
        esp_err_t err = i2c_master_get_bus_handle(cfg->i2c_port_number, &cfg->i2c_bus_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_master_get_bus_handle() failed (%s)", esp_err_to_name(err));
            return err;
        }
    }

    // Add PN532 như một device trên bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_I2C_RAW_ADDRESS,
        .scl_speed_hz = 100000,
        .scl_wait_us = 200000,
    };
    esp_err_t err = i2c_master_bus_add_device(cfg->i2c_bus_handle, &dev_cfg, &cfg->i2c_dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_master_bus_add_device() failed (%s)", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
#endif
}

void pn532_release_io(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL || io_handle->driver_data == NULL)
    {
        return;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

#if PN532_USE_LEGACY_I2C
    // LEGACY MODE: Chỉ uninstall driver nếu chúng ta đã tạo nó
    if (driver_config->bus_created)
    {
        ESP_LOGD(TAG, "uninstall legacy i2c driver...");
        i2c_driver_delete(driver_config->i2c_port_number);
        driver_config->bus_created = false;
    }
#else
    // API MỚI
    if (driver_config->i2c_dev_handle != NULL)
    {
        ESP_LOGD(TAG, "remove i2c device ...");
        i2c_master_bus_rm_device(driver_config->i2c_dev_handle);
        driver_config->i2c_dev_handle = NULL;
    }

    if (driver_config->i2c_bus_handle != NULL)
    {
        if (driver_config->bus_created)
        {
            ESP_LOGD(TAG, "delete i2c bus ...");
            i2c_del_master_bus(driver_config->i2c_bus_handle);
            driver_config->bus_created = false;
        }
        driver_config->i2c_bus_handle = NULL;
    }
#endif
}

static esp_err_t pn532_is_ready(pn532_io_handle_t io_handle)
{
    if (!io_handle || !io_handle->driver_data)
        return ESP_ERR_INVALID_ARG;
    pn532_i2c_driver_config *cfg = (pn532_i2c_driver_config *)io_handle->driver_data;

#if PN532_USE_LEGACY_I2C
    const uint8_t addr_7bit = 0x24;
    uint8_t status = 0;

    esp_err_t err = i2c_master_read_from_device(cfg->i2c_port_number, addr_7bit,
                                                &status, 1, ms2tick(10));
    if (err != ESP_OK)
        return err;
    return (status == 0x01) ? ESP_OK : ESP_FAIL;
#else
    uint8_t status;
    esp_err_t result = i2c_master_receive(cfg->i2c_dev_handle, &status, 1, 10);
    if (result != ESP_OK)
        return result;
    return (status == 0x01) ? ESP_OK : ESP_FAIL;
#endif
}

esp_err_t pn532_read(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms)
{
    if (!io_handle || !io_handle->driver_data || !read_buffer || read_size == 0 || read_size > 255)
        return ESP_ERR_INVALID_ARG;

    pn532_i2c_driver_config *cfg = (pn532_i2c_driver_config *)io_handle->driver_data;

#if PN532_USE_LEGACY_I2C
    uint8_t rx[512];
    TickType_t start = xTaskGetTickCount();
    TickType_t to_all = (xfer_timeout_ms > 0) ? pdMS_TO_TICKS(xfer_timeout_ms) : portMAX_DELAY;
    TickType_t elapsed = 0;
    const int per_read_ms = (xfer_timeout_ms > 0) ? xfer_timeout_ms : 100;
    TickType_t per_to = pdMS_TO_TICKS(per_read_ms);
    const uint8_t addr_7bit = 0x24;

    esp_err_t ret = ESP_FAIL;
    bool ready = false;
    while (!ready && elapsed < to_all)
    {
        // PN532: byte đầu tiên trả về là status, phải = 0x01 mới "ready"
        ret = i2c_master_read_from_device(cfg->i2c_port_number,
                                          addr_7bit,
                                          rx, read_size + 1,
                                          per_to);
        if (ret == ESP_OK && rx[0] == 0x01)
        {
            ready = true;
        }
        elapsed = xTaskGetTickCount() - start;
    }
    if (ret != ESP_OK)
        return ret;
    if (rx[0] != 0x01)
        return ESP_ERR_TIMEOUT;

    memcpy(read_buffer, rx + 1, read_size);
    return ESP_OK;
#else
    uint8_t rx_buffer[512];
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = (xfer_timeout_ms > 0) ? pdMS_TO_TICKS(xfer_timeout_ms) : portMAX_DELAY;
    TickType_t elapsed_ticks = 0;
    int read_timeout = (xfer_timeout_ms > 0) ? xfer_timeout_ms : 100;

    esp_err_t result = ESP_FAIL;
    bool is_ready = false;
    while (!is_ready && elapsed_ticks < timeout_ticks)
    {
        result = i2c_master_receive(cfg->i2c_dev_handle, rx_buffer, read_size + 1, read_timeout);
        if (result == ESP_OK && rx_buffer[0] == 0x01)
        {
            is_ready = true;
        }
        elapsed_ticks = xTaskGetTickCount() - start_ticks;
    }

    if (result != ESP_OK)
        return result;
    if (rx_buffer[0] != 0x01)
        return ESP_ERR_TIMEOUT;

    memcpy(read_buffer, rx_buffer + 1, read_size);
    return result;
#endif
}

esp_err_t pn532_write(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms)
{
    if (!io_handle || !io_handle->driver_data)
        return ESP_ERR_INVALID_ARG;
    if (write_size > 254)
        return ESP_ERR_INVALID_SIZE;

    pn532_i2c_driver_config *cfg = (pn532_i2c_driver_config *)io_handle->driver_data;

#if PN532_USE_LEGACY_I2C
    // PN532 over I2C yêu cầu 1 byte 0x00 trước frame và 1 byte 0x00 sau frame
    cfg->frame_buffer[0] = 0x00;
    memcpy(cfg->frame_buffer + 1, write_buffer, write_size);
    cfg->frame_buffer[write_size + 1] = 0x00;

    TickType_t to = (xfer_timeout_ms > 0) ? pdMS_TO_TICKS(xfer_timeout_ms) : portMAX_DELAY;
    const uint8_t addr_7bit = 0x24;

    return i2c_master_write_to_device(cfg->i2c_port_number,
                                      addr_7bit,
                                      cfg->frame_buffer,
                                      write_size + 2,
                                      to);
#else
    cfg->frame_buffer[0] = 0;
    memcpy(cfg->frame_buffer + 1, write_buffer, write_size);
    cfg->frame_buffer[write_size + 1] = 0;

    return i2c_master_transmit(cfg->i2c_dev_handle, cfg->frame_buffer, write_size + 2, xfer_timeout_ms);
#endif
}
