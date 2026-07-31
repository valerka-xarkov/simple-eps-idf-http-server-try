#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "time_set_up.h"
#include "wifi_config.h"
#include "http_server_handling.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Memory Usage Initial Free Heap: %u bytes", xPortGetFreeHeapSize());

    wifi_init_sta();

    set_up_time();

    start_webserver();

    ESP_LOGI(TAG, "Free memory left: %u bytes", xPortGetFreeHeapSize());
    // esp_log_level_set("*", ESP_LOG_NONE);
}