#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "helpers/time_set_up.h"
#include "wifi_config.h"
#include "http_server_handling.h"
#include "cpu_temperature.h"
#include "helpers/fs_operations.h"
#include "services/sys_information.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Memory Usage Initial Free Heap: %u bytes", xPortGetFreeHeapSize());

    wifi_init_sta();

    set_up_time();

    mount_littlefs();

    install_cpu_temperature();

    start_webserver();

    get_interesting_system_info();
    ESP_LOGI(TAG, "Free memory left: %u bytes", xPortGetFreeHeapSize());

    // esp_log_level_set("*", ESP_LOG_NONE);
}