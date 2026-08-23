#include "stdio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"

static const char *TAG = "LITTLEFS_INIT";

esp_err_t mount_littlefs()
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",       // Your base VFS mount point
        .partition_label = "storage",   // Matches the name in partitions.csv
        .format_if_mount_failed = true, // Formats empty space automatically if needed
        .dont_mount = false,
    };

    // Use the ESP-IDF VFS registration system
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    // Optional: Log diagnostic space info
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Partition initialized successfully. Size: total: %d bytes, used: %d bytes", total, used);
    }

    return ESP_OK;
}