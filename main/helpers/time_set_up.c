#include "time_set_up.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "time_set_up";

static void sync_cb(struct timeval *tv)
{
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);
    time_t sec = current_time.tv_sec;
    struct tm *tm_local = localtime(&sec);

    char formatted_date[30];
    strftime(formatted_date, sizeof(formatted_date), "%Y-%m-%d %H:%M:%S", tm_local);

    ESP_LOGI(TAG, "Current local time : %s", formatted_date);
    ESP_LOGI(TAG, "seconds and nanoseconds : %lld . %d", current_time.tv_sec, current_time.tv_nsec);
}

void set_up_time()
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.sync_cb = sync_cb;
    esp_netif_sntp_init(&config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK)
    {
        ESP_LOGI(TAG, "Time synchronized successfully");
    }
    else
    {
        ESP_LOGI(TAG, "Failed to update system time within 10s timeout");
    }
}