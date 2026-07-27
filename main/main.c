#include "esp_wifi.h"
#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <lwip/sockets.h>

#define OUTPUT_LED 22
#define MAX_POST_SIZE 220
#define LED_ON 0
#define LED_OFF 1
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

SemaphoreHandle_t blinking_mutex;
static const char *TAG = "main";
int led_status = LED_OFF;
TaskHandle_t blink_task_handle = NULL;
static EventGroupHandle_t s_wifi_event_group;

struct BlinkTaskParams
{
    int times;
    int intervalMs;
};

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    const char *resp_str = "<h1>Hello World</h1>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t led_toggle_handler(httpd_req_t *req)
{
    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {

        if (blink_task_handle != NULL)
        {
            ESP_LOGI(TAG, "Task %d Deleted", blink_task_handle);
            vTaskDelete(blink_task_handle);
        }
        xSemaphoreGive(blinking_mutex);
    }
    bool isLedOn = LED_ON == led_status;
    led_status = isLedOn ? LED_OFF : LED_ON;
    gpio_set_level(OUTPUT_LED, led_status);
    char resp_str[20];
    sprintf(resp_str, "{\"led\": \"%s\"}", isLedOn ? "off" : "on");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void blink_led_task_implementation(void *pvParameters)
{
    struct BlinkTaskParams *task_parameters = pvParameters;
    const int times = task_parameters->times;
    const int intervalMs = task_parameters->intervalMs;
    free(task_parameters);
    ESP_LOGI(TAG, "BlinkTask running on core %d", xPortGetCoreID());
    TickType_t ticks = pdMS_TO_TICKS(intervalMs);
    ESP_LOGI(TAG, "Values: times = %d, intervalMs = %d, ticks = %d", times, intervalMs, ticks);

    for (int i = 0; i <= times; i++)
    {
        gpio_set_level(OUTPUT_LED, LED_OFF);
        vTaskDelay(ticks);
        gpio_set_level(OUTPUT_LED, LED_ON);
        vTaskDelay(ticks);
    }
    ESP_LOGI(TAG, "Blink finished");
    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {

        ESP_LOGI(TAG, "Stack watermark %d", uxTaskGetStackHighWaterMark(blink_task_handle));
        blink_task_handle = NULL;
        xSemaphoreGive(blinking_mutex);
    }
    else
    {
        ESP_LOGI(TAG, "Error happen while removing blinking task handle");
    }
    vTaskDelete(NULL);
}

static esp_err_t led_blink_handler(httpd_req_t *req)
{

    const int total_len = req->content_len;
    if (total_len > MAX_POST_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Content too long\"}");
        return ESP_FAIL;
    }

    if (total_len == 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Content too short\"}");
        return ESP_FAIL;
    }

    char buf[total_len];
    if (httpd_req_recv(req, buf, total_len) <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Failed to recieve POST body\"}");
        return ESP_FAIL;
    }
    buf[total_len] = '\0';

    cJSON *const root = cJSON_Parse(buf);

    const cJSON *const timesNode = cJSON_GetObjectItem(root, "times");
    const cJSON *const intervalMsNode = cJSON_GetObjectItem(root, "intervalMs");

    if (!timesNode || !intervalMsNode)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Error parsing JSON data\"}");
        return ESP_FAIL;
    }
    const int times = timesNode->valueint;
    const int intervalMs = intervalMsNode->valueint;
    ESP_LOGI(TAG, "Received and parsed values: times = %d, intervalMs = %d", times, intervalMs);
    cJSON_Delete(root);

    if (times <= 0 || times > 1000 || intervalMs < 50 || intervalMs > 3000)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Wrong values are provided\"}");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {
        if (blink_task_handle != NULL)
        {
            ESP_LOGI(TAG, "Task %d Deleted", blink_task_handle);
            vTaskDelete(blink_task_handle);
        }
        struct BlinkTaskParams *task_parameters = malloc(sizeof(struct BlinkTaskParams));
        task_parameters->intervalMs = intervalMs;
        task_parameters->times = times;
        ESP_LOGI(TAG, "Values sent to the handle: times = %d, intervalMs = %d", task_parameters->times, task_parameters->intervalMs);

        xTaskCreate(blink_led_task_implementation, "blink_led_task", 2048, (void *)task_parameters, 1, &blink_task_handle);
        xSemaphoreGive(blinking_mutex);
    }
    else
    {
        ESP_LOGI(TAG, "Error happen while runnning while creating blinking task");
    }

    ESP_LOGI(TAG, "blink_task_handle value %d", (int)&blink_task_handle);

    httpd_resp_sendstr(req, "{\"success\": true}");
    return ESP_OK;
}

static const httpd_uri_t hello_world_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = hello_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t led_toggle = {
    .uri = "/led/toggle",
    .method = HTTP_GET,
    .handler = led_toggle_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t led_blink = {
    .uri = "/led/blink",
    .method = HTTP_POST,
    .handler = led_blink_handler,
    .user_ctx = NULL,
};

static void configure_led(void)
{
    gpio_reset_pin(OUTPUT_LED);
    /* Set the GPIO as push/pull output */
    gpio_set_direction(OUTPUT_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT_LED, 1);
    ESP_LOGI(TAG, "LED Configured!\n");
}
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WIFI disconected");

        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK)
    {
        ESP_LOGI("WIFI", "Power save disabled successfully.");
    }
    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}
esp_err_t open_fn(httpd_handle_t hd, int sockfd)
{
    int val = 1;
    // Disable Nagle's algorithm for instant packet delivery
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    return ESP_OK;
}
httpd_handle_t start_webserver()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_open_sockets = 7;
    config.open_fn = open_fn; // THIS LINE IS SPEEDING UP esp_http_server RESPONSE FROM 65ms TO 10ms

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "Server started successfully, registering URI handlers...");
        return server;
    }

    ESP_LOGE(TAG, "Failed to start server");
    return NULL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Memory Usage Initial Free Heap: %u bytes", xPortGetFreeHeapSize());

    configure_led();
    wifi_init_sta();

    httpd_handle_t server = start_webserver();

    httpd_register_uri_handler(server, &hello_world_uri);
    httpd_register_uri_handler(server, &led_toggle);
    httpd_register_uri_handler(server, &led_blink);

    blinking_mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Memory Usage after initialization: %u bytes", xPortGetFreeHeapSize());
}