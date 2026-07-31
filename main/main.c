#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "zlib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "time_set_up.h"
#include "request_counter.h"
#include "wifi_config.h"

#define CHUNK_SIZE 4096
#define OUTPUT_LED 22
#define MAX_POST_SIZE 220
#define LED_ON 0
#define LED_OFF 1

SemaphoreHandle_t blinking_mutex;
static const char *TAG = "main";
int led_status = LED_OFF;
TaskHandle_t blink_task_handle = NULL;

struct blink_task_params
{
    int times;
    int intervalMs;
};

// Define a structure to hold your persistent buffers and stream context
typedef struct
{
    z_stream strm;
    uint8_t *in_buf;
    uint8_t *out_buf;
    bool is_initialized;
} global_zlib_ctx_t;

// Instantiate the global workspace variable
static global_zlib_ctx_t g_zlib = {.is_initialized = false};

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
        ESP_LOGI(TAG, "Semaphore taken in toggle handler");
        if (blink_task_handle != NULL)
        {
            vTaskDelete(blink_task_handle);
            blink_task_handle = NULL;
        }
        xSemaphoreGive(blinking_mutex);
        ESP_LOGI(TAG, "Semaphore given in toggle handler");
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
    ESP_LOGI(TAG, "BlinkImplementation started on core 0");
    struct blink_task_params *task_parameters = pvParameters;
    const int times = task_parameters->times;
    const int intervalMs = task_parameters->intervalMs;
    free(task_parameters);
    ESP_LOGI(TAG, "Memory has beed freed, values in BlinkImplementation: times = %d, intervalMs = %d", times, intervalMs);

    TickType_t ticks = pdMS_TO_TICKS(intervalMs);

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

        ESP_LOGI(TAG, "Semaphore taken in blinking implementation");
        blink_task_handle = NULL;
        xSemaphoreGive(blinking_mutex);
        ESP_LOGI(TAG, "Semaphore given in blinking implementation");
    }
    else
    {
        ESP_LOGI(TAG, "Error happen while removing blinking task handle");
    }
    vTaskDelete(NULL);
}

static esp_err_t led_blink_handler(httpd_req_t *req)
{

    httpd_resp_set_type(req, "application/json");
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Failed to receive POST body\"}");
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

        ESP_LOGI(TAG, "Semaphore taken in http-handler");

        if (blink_task_handle != NULL)
        {
            vTaskDelete(blink_task_handle);
            ESP_LOGI(TAG, "Deleted Task %d", blink_task_handle);
            blink_task_handle = NULL;
        }
        struct blink_task_params *task_parameters = malloc(sizeof(struct blink_task_params));
        task_parameters->intervalMs = intervalMs;
        task_parameters->times = times;
        const int priority = uxTaskPriorityGet(NULL);
        int xReturned = xTaskCreatePinnedToCore(blink_led_task_implementation, "blink_led_task", 8192, (void *)task_parameters, priority, &blink_task_handle, 0);
        if (xReturned == pdFAIL)
        {
            ESP_LOGI(TAG, "xTaskCreate Failed");
        }
        xSemaphoreGive(blinking_mutex);
        ESP_LOGI(TAG, "Semaphore given in http-handler");
    }
    else
    {
        ESP_LOGI(TAG, "Error happen while runnning while creating blinking task");
    }

    ESP_LOGI(TAG, "blink_task_handle value %d", (int)&blink_task_handle);

    httpd_resp_sendstr(req, "{\"success\": true}");
    return ESP_OK;
}

esp_err_t gzip_minify_handler(httpd_req_t *req, char *generated_content)
{
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    z_stream *p_strm = &g_zlib.strm;
    uint8_t *in_buf = g_zlib.in_buf;
    uint8_t *out_buf = g_zlib.out_buf;

    size_t content_bytes_left = strlen(generated_content);
    const char *content_ptr = generated_content;
    bool flush_flag = false;

    do
    {
        size_t bytes_to_read = (content_bytes_left > CHUNK_SIZE) ? CHUNK_SIZE : content_bytes_left;
        memcpy(in_buf, content_ptr, bytes_to_read);

        content_ptr += bytes_to_read;
        content_bytes_left -= bytes_to_read;

        p_strm->next_in = in_buf;
        p_strm->avail_in = bytes_to_read;

        if (content_bytes_left == 0)
        {
            flush_flag = true;
        }
        do
        {
            p_strm->next_out = out_buf;
            p_strm->avail_out = CHUNK_SIZE;

            // Execute compression using persistent buffers
            int ret = deflate(p_strm, flush_flag ? Z_FINISH : Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR)
            {
                ESP_LOGE(TAG, "Zlib engine execution error");
                deflateReset(p_strm); // Clean up context state even on failure
                return ESP_FAIL;
            }

            size_t compressed_bytes = CHUNK_SIZE - p_strm->avail_out;
            if (compressed_bytes > 0)
            {
                esp_err_t err = httpd_resp_send_chunk(req, (const char *)out_buf, compressed_bytes);
                if (err != ESP_OK)
                {
                    deflateReset(p_strm); // Clean up if browser abruptly closes the socket
                    return ESP_FAIL;
                }
            }
        } while (p_strm->avail_out == 0);

    } while (!flush_flag);

    deflateReset(p_strm);

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t get_requests_quantity_handler(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_type(req, "application/json");
    char *resp_str = get_requests_information_char();
    // httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    gzip_minify_handler(req, resp_str);
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

static const httpd_uri_t get_requests_quantity = {
    .uri = "/requests-quantity",
    .method = HTTP_GET,
    .handler = get_requests_quantity_handler,
    .user_ctx = NULL,
};

esp_err_t init_global_zlib(void)
{
    g_zlib.in_buf = malloc(CHUNK_SIZE);
    g_zlib.out_buf = malloc(CHUNK_SIZE);

    g_zlib.strm.zalloc = Z_NULL;
    g_zlib.strm.zfree = Z_NULL;
    g_zlib.strm.opaque = Z_NULL;
    // windowBits = 13 + 16 (Gzip output wrapper) = 29 memLevel = 6
    int ret = deflateInit2(&g_zlib.strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 29, 6, Z_DEFAULT_STRATEGY);
    if (ret == Z_OK)
    {
        ESP_LOGI(TAG, "Global Zlib initialized successfully. 64KB allocated permanently.");
    }
    return ESP_OK;
}

static void configure_led(void)
{
    gpio_reset_pin(OUTPUT_LED);
    gpio_set_direction(OUTPUT_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT_LED, 1);
    ESP_LOGI(TAG, "LED Configured!\n");
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
    config.lru_purge_enable = false;
    config.max_open_sockets = 7;
    config.open_fn = open_fn; // THIS LINE IS SPEEDING UP esp_http_server RESPONSE FROM 65ms TO 10ms
    config.core_id = 1;       // Improves performance on "Hello world" page from 320/s to 350/s
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
    blinking_mutex = xSemaphoreCreateMutex();

    configure_led();
    wifi_init_sta();

    set_up_time();
    init_global_zlib();

    httpd_handle_t server = start_webserver();

    httpd_register_uri_handler(server, &hello_world_uri);
    httpd_register_uri_handler(server, &led_toggle);
    httpd_register_uri_handler(server, &led_blink);
    httpd_register_uri_handler(server, &get_requests_quantity);

    init_http_info_requests_counter();

    ESP_LOGI(TAG, "Free memory left: %u bytes", xPortGetFreeHeapSize());
    // esp_log_level_set("*", ESP_LOG_NONE);
}