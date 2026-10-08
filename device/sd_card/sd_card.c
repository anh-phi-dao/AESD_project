/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "sd_card.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* ESP32-S3-WROOM-1 SDMMC GPIO pin mapping from hardware schematic. */
#define SD_PIN_CLK   21
#define SD_PIN_CMD   42
#define SD_PIN_D0    47
#define SD_PIN_D1    48
#define SD_PIN_D2    1
#define SD_PIN_D3    2
#define SD_PIN_CD    20

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void sd_card_build_full_path(const char *filename, char *full_path, size_t max_len);

/***********************************************************************************************************************
 * Global Variables
 **********************************************************************************************************************/

static const char   *TAG          = "SD_CARD";
static sdmmc_card_t *g_card       = NULL;
static bool          g_sd_mounted = false;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Initializes the SDMMC peripheral, mounts the FAT filesystem (trying 4-bit high-speed mode first and falling back to
 * 1-bit default mode if needed), prints card information, and runs a read/write speed benchmark.
 *
 * @retval ESP_OK   SD card was initialized and mounted successfully, or is already mounted.
 * @return          An ESP-IDF error code if mounting fails in both 4-bit and 1-bit modes.
 **********************************************************************************************************************/
esp_err_t sd_card_init(void)
{
    if (g_sd_mounted)
    {
        ESP_LOGW(TAG, "SD card already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card via SDMMC...");

    /* SDMMC host configuration */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    /* Slot configuration with hardware schematic pins */
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0  = SD_PIN_D0;
    slot_config.d1  = SD_PIN_D1;
    slot_config.d2  = SD_PIN_D2;
    slot_config.d3  = SD_PIN_D3;

    /* Enable internal pull-ups for stability */
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /* Mount configuration optimized for performance */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed   = false,
        .max_files                = SD_CARD_MAX_FILES,
        .allocation_unit_size     = SD_CARD_ALLOCATION_UNIT_SIZE,
        .disk_status_check_enable = false
    };

    /* Try 4-bit mode first, fallback to 1-bit if needed */
    ESP_LOGI(TAG, "Attempting 4-bit SDMMC mode...");

    host.flags        = SDMMC_HOST_FLAG_4BIT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    slot_config.width = 4;

    int64_t   start_time = esp_timer_get_time();
    esp_err_t ret        = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT, &host, &slot_config, &mount_config, &g_card);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "4-bit mode failed (%s), trying 1-bit mode...", esp_err_to_name(ret));

        /* Fallback to 1-bit mode */
        host.flags        = SDMMC_HOST_FLAG_1BIT;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        slot_config.width = 1;

        ret = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT, &host, &slot_config, &mount_config, &g_card);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to mount SDMMC in 1-bit mode: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGW(TAG, "Mounted in 1-bit mode (reduced performance)");
    }
    else
    {
        ESP_LOGI(TAG, "Mounted in 4-bit mode (high performance)");
    }

    int64_t mount_time = (esp_timer_get_time() - start_time) / 1000;

    g_sd_mounted = true;
    ESP_LOGI(TAG, "SDMMC mounted successfully in %lld ms", mount_time);

    /* Print detailed card information */
    ESP_LOGI(TAG, "=== SD CARD INFO ===");

    const char *card_type = "Unknown";
    if (g_card->is_mmc)
    {
        card_type = "MMC";
    }
    else
    {
        uint64_t capacity_bytes = (uint64_t)g_card->csd.capacity * 512;
        if (capacity_bytes > 2ULL * 1024 * 1024 * 1024)
        {
            card_type = "SDHC/SDXC";
        }
        else
        {
            card_type = "SDSC";
        }
    }

    ESP_LOGI(TAG, "Type: %s", card_type);
    ESP_LOGI(TAG, "Name: %s", g_card->cid.name);
    ESP_LOGI(TAG, "Capacity: %.2f GB", (float)(g_card->csd.capacity) / (1024.0f * 1024.0f * 2.0f));
    ESP_LOGI(TAG, "Current Freq: %" PRIu32 " kHz", (uint32_t)g_card->real_freq_khz);
    ESP_LOGI(TAG, "Max Freq: %" PRIu32 " kHz", (uint32_t)g_card->max_freq_khz);
    ESP_LOGI(TAG, "Bus Width: %d-bit", (g_card->log_bus_width == 2) ? 4 : 1);

    /* Calculate theoretical speed */
    uint32_t freq_khz          = g_card->real_freq_khz;
    int      bus_width         = (g_card->log_bus_width == 2) ? 4 : 1;
    float    theoretical_speed = (float)(freq_khz * bus_width) / 8000.0f;
    ESP_LOGI(TAG, "Theoretical Speed: %.2f MB/s", theoretical_speed);

    /* Performance test */
    ESP_LOGI(TAG, "Testing read performance...");
    start_time = esp_timer_get_time();

    FILE *test_file = fopen(SD_CARD_MOUNT_POINT "/speed_test.tmp", "w");
    if (test_file)
    {
        uint8_t *test_data = (uint8_t *)malloc(1024);
        if (test_data)
        {
            memset(test_data, 0xAA, 1024);
            for (int i = 0; i < 1024; i++)
            {
                fwrite(test_data, 1, 1024, test_file);
            }
            free(test_data);
        }
        fclose(test_file);

        test_file = fopen(SD_CARD_MOUNT_POINT "/speed_test.tmp", "r");
        if (test_file)
        {
            uint8_t *read_buffer = (uint8_t *)malloc(SD_CARD_FILE_BUF_SIZE);
            if (read_buffer)
            {
                size_t total_read = 0;
                size_t bytes_read;
                while ((bytes_read = fread(read_buffer, 1, SD_CARD_FILE_BUF_SIZE, test_file)) > 0)
                {
                    total_read += bytes_read;
                }
                free(read_buffer);

                int64_t read_time = (esp_timer_get_time() - start_time) / 1000;
                if (read_time > 0)
                {
                    float speed_mbps = (float)(total_read / 1024) / (read_time / 1000.0f);
                    ESP_LOGI(TAG, "Actual Read Speed: %.2f MB/s", speed_mbps);
                }
            }
            fclose(test_file);
        }

        unlink(SD_CARD_MOUNT_POINT "/speed_test.tmp");
    }

    return ESP_OK;
}

/***********************************************************************************************************************
 * Unmounts the FAT filesystem and deinitializes the SDMMC host peripheral.
 *
 * @retval ESP_OK   Deinitialization succeeded or the card was not mounted.
 * @return          An ESP-IDF error code if unmounting the filesystem fails.
 **********************************************************************************************************************/
esp_err_t sd_card_deinit(void)
{
    if (!g_sd_mounted)
    {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing SDMMC...");

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, g_card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to unmount filesystem: %s", esp_err_to_name(ret));
    }

    g_sd_mounted = false;
    g_card       = NULL;

    ESP_LOGI(TAG, "SDMMC deinitialized");
    return ret;
}

/***********************************************************************************************************************
 * Configures the SDMMC host in 1-bit mode and mounts the FAT filesystem at SD_CARD_MOUNT_POINT.
 *
 * @retval ESP_OK   The SD card was mounted successfully or is already mounted.
 * @return          An ESP-IDF error code if the mount operation fails.
 **********************************************************************************************************************/
esp_err_t sd_card_mount(void)
{
    if (g_sd_mounted)
    {
        ESP_LOGW(TAG, "SD card is already mounted");
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files              = SD_CARD_MAX_FILES,
        .allocation_unit_size   = SD_CARD_ALLOCATION_UNIT_SIZE,
    };

    sdmmc_host_t        host        = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0  = SD_PIN_D0;

    /* Use 1-bit SDMMC mode. */
    slot_config.width = 1;

    /* Enable internal pull-ups of the ESP32-S3. */
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting SD card...");

    esp_err_t error = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT,
                                              &host,
                                              &slot_config,
                                              &mount_config,
                                              &g_card);

    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(error));
        return error;
    }

    g_sd_mounted = true;

    ESP_LOGI(TAG, "SD card mounted at %s", SD_CARD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, g_card);

    return ESP_OK;
}

/***********************************************************************************************************************
 * Unmounts the FAT filesystem and resets the internal SD card state.
 **********************************************************************************************************************/
void sd_card_unmount(void)
{
    (void)sd_card_deinit();
}

/***********************************************************************************************************************
 * Checks whether the SD card filesystem is currently mounted.
 *
 * @retval true     The SD card is mounted.
 * @retval false    The SD card is not mounted.
 **********************************************************************************************************************/
bool sd_card_is_mounted(void)
{
    return g_sd_mounted;
}

/***********************************************************************************************************************
 * Retrieves a pointer to the internal SDMMC card descriptor structure.
 *
 * @param[out] card  Pointer that receives the sdmmc_card_t address.
 *
 * @retval ESP_OK                   Card descriptor pointer was returned.
 * @retval ESP_ERR_INVALID_ARG      The output pointer is NULL.
 * @retval ESP_ERR_INVALID_STATE    The SD card is not mounted.
 **********************************************************************************************************************/
esp_err_t sd_card_get_info(sdmmc_card_t **card)
{
    if (!card)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_sd_mounted || !g_card)
    {
        return ESP_ERR_INVALID_STATE;
    }

    *card = g_card;
    return ESP_OK;
}

/***********************************************************************************************************************
 * Logs current SDMMC bus parameters, card capacity, and theoretical maximum transfer speed.
 **********************************************************************************************************************/
void sd_card_print_performance_info(void)
{
    if (!g_sd_mounted || !g_card)
    {
        ESP_LOGW(TAG, "SD card not mounted");
        return;
    }

    ESP_LOGI(TAG, "=== SDMMC PERFORMANCE INFO ===");
    ESP_LOGI(TAG, "Current Frequency: %lu kHz", (unsigned long)g_card->real_freq_khz);
    ESP_LOGI(TAG, "Max Frequency: %lu kHz", (unsigned long)g_card->max_freq_khz);
    ESP_LOGI(TAG, "Bus Width: %d-bit", (g_card->log_bus_width == 2) ? 4 : 1);
    ESP_LOGI(TAG, "High Speed: %s", (g_card->is_ddr) ? "DDR" : "SDR");
    ESP_LOGI(TAG, "Capacity: %.2f GB", (float)(g_card->csd.capacity) / (1024.0f * 1024.0f * 2.0f));

    /* Calculate theoretical max speed */
    uint32_t freq_khz          = (uint32_t)g_card->real_freq_khz;
    int      bus_width         = (g_card->log_bus_width == 2) ? 4 : 1;
    float    theoretical_speed = (float)(freq_khz * bus_width) / 8000.0f;
    ESP_LOGI(TAG, "Theoretical Max Speed: %.2f MB/s", theoretical_speed);
    ESP_LOGI(TAG, "Speed Class: Class 10+");
    ESP_LOGI(TAG, "UHS Speed Grade: U1+");
}

/***********************************************************************************************************************
 * Writes or appends text content to a .txt file on the SD card using an 8 KB stream buffer for optimal throughput.
 *
 * @param[in] filename  Relative file name (e.g., "hello.txt") or full path starting with SD_CARD_MOUNT_POINT.
 * @param[in] content   Null-terminated text string to write into the file.
 * @param[in] append    True to append to an existing file ("a"), false to overwrite or create a new file ("w").
 *
 * @retval ESP_OK                   Text was written to the file successfully.
 * @retval ESP_ERR_INVALID_ARG      filename or content is NULL.
 * @retval ESP_ERR_INVALID_STATE    The SD card is not mounted.
 * @retval ESP_FAIL                 Failed to open or write to the file.
 **********************************************************************************************************************/
esp_err_t sd_card_write_txt_file(const char *filename, const char *content, bool append)
{
    if (filename == NULL || content == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_sd_mounted)
    {
        ESP_LOGE(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[SD_CARD_PATH_MAX_LEN];
    sd_card_build_full_path(filename, full_path, sizeof(full_path));

    int64_t start_time = esp_timer_get_time();
    FILE   *f          = fopen(full_path, append ? "a" : "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
        return ESP_FAIL;
    }

    /* Set larger buffer for better SDMMC write performance */
    setvbuf(f, NULL, _IOFBF, SD_CARD_FILE_BUF_SIZE);

    size_t content_len = strlen(content);
    size_t written     = fwrite(content, 1, content_len, f);
    fclose(f);

    if (written != content_len)
    {
        ESP_LOGE(TAG, "Incomplete write to file: %s", full_path);
        return ESP_FAIL;
    }

    int64_t write_time = (esp_timer_get_time() - start_time) / 1000;
    ESP_LOGI(TAG, "Text file written (%s) in %lld ms: %s", append ? "append" : "overwrite", write_time, full_path);

    return ESP_OK;
}

/***********************************************************************************************************************
 * Opens and reads a .txt file from the SD card, logs its contents line-by-line to the terminal, and optionally copies
 * the text into a caller-provided buffer.
 *
 * @param[in]  filename     Relative file name (e.g., "hello.txt") or full path starting with SD_CARD_MOUNT_POINT.
 * @param[out] out_buffer   Optional buffer to store the file content (pass NULL if only terminal logging is needed).
 * @param[in]  buffer_size  Size of out_buffer in bytes.
 *
 * @retval ESP_OK                   File was opened and read successfully.
 * @retval ESP_ERR_INVALID_ARG      filename is NULL.
 * @retval ESP_ERR_INVALID_STATE    The SD card is not mounted.
 * @retval ESP_ERR_NOT_FOUND        The specified file could not be opened.
 **********************************************************************************************************************/
esp_err_t sd_card_read_txt_file(const char *filename, char *out_buffer, size_t buffer_size)
{
    if (filename == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_sd_mounted)
    {
        ESP_LOGE(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[SD_CARD_PATH_MAX_LEN];
    sd_card_build_full_path(filename, full_path, sizeof(full_path));

    int64_t start_time = esp_timer_get_time();
    FILE   *f          = fopen(full_path, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open text file for reading: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Set larger buffer for better SDMMC read performance */
    setvbuf(f, NULL, _IOFBF, SD_CARD_FILE_BUF_SIZE);

    if (out_buffer != NULL && buffer_size > 0)
    {
        out_buffer[0] = '\0';
    }

    size_t offset = 0;
    char   line[128];

    ESP_LOGI(TAG, "Reading file: %s", full_path);
    while (fgets(line, sizeof(line), f) != NULL)
    {
        /* Copy to caller buffer if provided */
        if (out_buffer != NULL && buffer_size > 1 && offset < (buffer_size - 1))
        {
            size_t line_len  = strlen(line);
            size_t copy_size = (offset + line_len < buffer_size - 1) ? line_len : (buffer_size - 1 - offset);
            memcpy(&out_buffer[offset], line, copy_size);
            offset += copy_size;
            out_buffer[offset] = '\0';
        }

        /* Strip trailing newline characters for clean terminal output */
        line[strcspn(line, "\r\n")] = '\0';
        ESP_LOGI(TAG, "Read: %s", line);
    }

    fclose(f);

    int64_t read_time = (esp_timer_get_time() - start_time) / 1000;
    ESP_LOGI(TAG, "Text file read in %lld ms: %s", read_time, full_path);

    return ESP_OK;
}

/***********************************************************************************************************************
 * Recursively traverses and logs the directory tree up to SD_MAX_DIRECTORY_DEPTH levels deep. Uses heap allocation
 * for path buffers to prevent stack overflow.
 *
 * @param[in] directory  Path to the directory to list.
 * @param[in] level      Current recursion depth level used for indentation.
 **********************************************************************************************************************/
void sd_card_list_files(const char *directory, int level)
{
    if (directory == NULL)
    {
        ESP_LOGE(TAG, "Directory path is NULL");
        return;
    }

    if (!g_sd_mounted)
    {
        ESP_LOGE(TAG, "SD card is not mounted");
        return;
    }

    if (level > SD_MAX_DIRECTORY_DEPTH)
    {
        ESP_LOGW(TAG, "Maximum directory depth reached");
        return;
    }

    DIR *dir = opendir(directory);
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Cannot open directory: %s", directory);
        return;
    }

    char *full_path = (char *)malloc(SD_CARD_PATH_MAX_LEN);
    if (full_path == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for path buffer");
        closedir(dir);
        return;
    }

    struct dirent *entry;
    struct stat    entry_stat;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        int written = snprintf(full_path, SD_CARD_PATH_MAX_LEN, "%s/%s", directory, entry->d_name);
        if (written < 0 || written >= (int)SD_CARD_PATH_MAX_LEN)
        {
            ESP_LOGW(TAG, "Path too long: %s/%s", directory, entry->d_name);
            continue;
        }

        if (stat(full_path, &entry_stat) != 0)
        {
            ESP_LOGW(TAG, "Cannot stat: %s", full_path);
            continue;
        }

        char   indent[64]    = "";
        size_t indent_length = 0;

        for (int i = 0; i < level && indent_length + 4 < sizeof(indent); i++)
        {
            memcpy(&indent[indent_length], "   |", 4);
            indent_length += 4;
            indent[indent_length] = '\0';
        }

        if (S_ISDIR(entry_stat.st_mode))
        {
            ESP_LOGI(TAG, "%s[Directory] %s", indent, full_path);
            sd_card_list_files(full_path, level + 1);
        }
        else
        {
            ESP_LOGI(TAG, "%s[File] %s", indent, full_path);
        }
    }

    free(full_path);
    closedir(dir);
}

/***********************************************************************************************************************
 * Recursively deletes all files and subdirectories within the specified directory while keeping the root directory
 * itself intact.
 *
 * @param[in] directory  Path to the directory whose contents will be deleted.
 *
 * @retval ESP_OK                   All contents were deleted successfully.
 * @retval ESP_ERR_INVALID_ARG      The directory pointer is NULL.
 * @retval ESP_ERR_INVALID_STATE    The SD card is not mounted.
 * @retval ESP_ERR_NO_MEM           Failed to allocate memory for the path buffer.
 * @retval ESP_FAIL                 Failed to open directory or remove one or more items.
 **********************************************************************************************************************/
esp_err_t sd_card_delete_all(const char *directory)
{
    if (directory == NULL)
    {
        ESP_LOGE(TAG, "Directory path is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_sd_mounted)
    {
        ESP_LOGE(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    DIR *dir = opendir(directory);
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Cannot open directory: %s", directory);
        return ESP_FAIL;
    }

    char *full_path = (char *)malloc(SD_CARD_PATH_MAX_LEN);
    if (full_path == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for path buffer");
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t      ret = ESP_OK;
    struct dirent *entry;
    struct stat    entry_stat;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        int written = snprintf(full_path, SD_CARD_PATH_MAX_LEN, "%s/%s", directory, entry->d_name);
        if (written < 0 || written >= (int)SD_CARD_PATH_MAX_LEN)
        {
            ESP_LOGW(TAG, "Path too long: %s/%s", directory, entry->d_name);
            ret = ESP_FAIL;
            continue;
        }

        if (stat(full_path, &entry_stat) != 0)
        {
            ESP_LOGW(TAG, "Cannot stat: %s", full_path);
            ret = ESP_FAIL;
            continue;
        }

        if (S_ISDIR(entry_stat.st_mode))
        {
            if (sd_card_delete_all(full_path) != ESP_OK)
            {
                ret = ESP_FAIL;
            }

            if (rmdir(full_path) == 0)
            {
                ESP_LOGI(TAG, "Deleted directory: %s", full_path);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to delete directory: %s", full_path);
                ret = ESP_FAIL;
            }
        }
        else
        {
            if (unlink(full_path) == 0)
            {
                ESP_LOGI(TAG, "Deleted file: %s", full_path);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to delete file: %s", full_path);
                ret = ESP_FAIL;
            }
        }
    }

    free(full_path);
    closedir(dir);

    return ret;
}

/***********************************************************************************************************************
 * Formats the FAT filesystem on the currently mounted SD card, erasing all files and directories immediately.
 *
 * @retval ESP_OK                   The SD card was formatted successfully.
 * @retval ESP_ERR_INVALID_STATE    The SD card is not mounted.
 * @return                          An ESP-IDF error code if the format operation fails.
 **********************************************************************************************************************/
esp_err_t sd_card_format(void)
{
    if (!g_sd_mounted || g_card == NULL)
    {
        ESP_LOGE(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Formatting SD card...");
    ESP_LOGI(TAG, "PLEASE WAIT!!!!!!!");

    esp_err_t ret = esp_vfs_fat_sdcard_format(SD_CARD_MOUNT_POINT, g_card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to format SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card formatted successfully");
    return ESP_OK;
}

/***********************************************************************************************************************
 * Private function
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Builds a complete virtual filesystem path by prepending SD_CARD_MOUNT_POINT if not already present.
 **********************************************************************************************************************/
static void sd_card_build_full_path(const char *filename, char *full_path, size_t max_len)
{
    if (strncmp(filename, SD_CARD_MOUNT_POINT, strlen(SD_CARD_MOUNT_POINT)) == 0)
    {
        snprintf(full_path, max_len, "%s", filename);
    }
    else if (filename[0] == '/')
    {
        snprintf(full_path, max_len, "%s%s", SD_CARD_MOUNT_POINT, filename);
    }
    else
    {
        snprintf(full_path, max_len, "%s/%s", SD_CARD_MOUNT_POINT, filename);
    }
}