#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "services/request_counter.h"
#include "esp_timer.h"
#include "services/zlib_compressor.h"
#include "api/requests_quantity.h"
#include "api/sys_info.h"
#include "helpers/page_cache_generator.h"
#include "api/led.h"
#include "helpers/led.h"
#include "helpers/touch_events_helper.h"
#include "web-modules/main-page/main_page.h"
#include "api/performance-testing.h"

// #define OUTPUT_LED 22 // use this for esp32 lite
#define OUTPUT_LED 38
#define LED_ON 0
#define LED_OFF 1

httpd_handle_t server = NULL;
SemaphoreHandle_t blinking_mutex;
static const char *TAG = "http-server";
int led_status = LED_OFF;
TaskHandle_t blink_task_handle = NULL;

struct blink_task_params
{
    int times;
    int intervalMs;
};

const char *get_mime_type(const char *filename)
{
    if (filename == NULL)
        return "application/octet-stream";

    const char *ext = strrchr(filename, '.');
    if (!ext)
        return "text/plain"; // No extension found

    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
        return HTTPD_TYPE_TEXT;
    if (strcasecmp(ext, ".css") == 0)
        return "text/css";
    if (strcasecmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(ext, ".json") == 0)
        return HTTPD_TYPE_JSON;
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".ico") == 0)
        return "image/x-icon";
    if (strcasecmp(ext, ".svg") == 0)
        return "image/svg+xml";

    return HTTPD_TYPE_OCTET; // Default fallback
}

static const char *hello_world_message = "<h1>Hello World</h1>"; // 20 symbols

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    http_info_request_happen();
    // httpd_resp_send(req, hello_world_message, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, hello_world_message, 20);
    return ESP_OK;
}

static char *hello_optimized_message = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 20\r\n\r\n<h1>Hello World</h1>";

static esp_err_t hello_optimized_get_handler(httpd_req_t *req)
{
    http_info_request_happen();
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return ESP_FAIL;

    httpd_send(req, hello_optimized_message, 84);
    return ESP_OK;
}

static esp_err_t led_toggle_handler(httpd_req_t *req)
{
    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {
        if (blink_task_handle != NULL)
        {
            vTaskDelete(blink_task_handle);
            blink_task_handle = NULL;
        }
        xSemaphoreGive(blinking_mutex);
    }
    bool isLedOn = LED_ON == led_status;
    led_status = isLedOn ? LED_OFF : LED_ON;
    gpio_set_level(OUTPUT_LED, led_status);
    char resp_str[20];
    sprintf(resp_str, "{\"led\": \"%s\"}", isLedOn ? "off" : "on");
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void blink_led_task_implementation(void *pvParameters)
{
    struct blink_task_params *task_parameters = pvParameters;
    const int times = task_parameters->times;
    const int intervalMs = task_parameters->intervalMs;
    free(task_parameters);

    TickType_t ticks = pdMS_TO_TICKS(intervalMs);

    for (int i = 0; i <= times; i++)
    {
        gpio_set_level(OUTPUT_LED, LED_OFF);
        vTaskDelay(ticks);
        gpio_set_level(OUTPUT_LED, LED_ON);
        vTaskDelay(ticks);
    }
    // ESP_LOGI(TAG, "Blink finished");

    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {
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

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    const int total_len = req->content_len;
    const int max_blink_data_size = 35;
    if (total_len > max_blink_data_size)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"You are hacker! Don't break this piece of rubbish with 520kb RAM only. Content too long.\"}");
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
    // ESP_LOGI(TAG, "Received and parsed values: times = %d, intervalMs = %d", times, intervalMs);
    cJSON_Delete(root);

    if (times <= 0 || times > 1000 || intervalMs < 50 || intervalMs > 3000)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\": \"Wrong values are provided\"}");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {

        // ESP_LOGI(TAG, "Semaphore taken in http-handler");

        if (blink_task_handle != NULL)
        {
            vTaskDelete(blink_task_handle);
            // ESP_LOGI(TAG, "Deleted Task %d", blink_task_handle);
            blink_task_handle = NULL;
        }
        struct blink_task_params *task_parameters = malloc(sizeof(struct blink_task_params));
        task_parameters->intervalMs = intervalMs;
        task_parameters->times = times;
        const int priority = uxTaskPriorityGet(NULL);
        int xReturned = xTaskCreatePinnedToCore(blink_led_task_implementation, "blink_led_task", 4096, (void *)task_parameters, priority, &blink_task_handle, 0);
        if (xReturned == pdFAIL)
        {
            ESP_LOGI(TAG, "xTaskCreate Failed");
        }
        xSemaphoreGive(blinking_mutex);
        // ESP_LOGI(TAG, "Semaphore given in http-handler");
    }
    else
    {
        ESP_LOGI(TAG, "Error happen while runnning while creating blinking task");
    }

    // ESP_LOGI(TAG, "blink_task_handle value %d", (int)&blink_task_handle);

    httpd_resp_sendstr(req, "{\"success\": true}");
    return ESP_OK;
}

static size_t file_provider(char *buf, size_t max_len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fread(buf, 1, max_len, f); // Returns 0 automatically on EOF
}

static size_t simple_compress_cb(uint8_t *buf, size_t buf_len, void *context)
{
    httpd_req_t *req = (httpd_req_t *)context;
    return httpd_resp_send_chunk(req, (char *)buf, buf_len);
}

static esp_err_t file_stream_handler(httpd_req_t *req)
{
    http_info_request_happen();
    char *file_path = "/littlefs/static/index.html";

    const char *mime_type = get_mime_type(file_path);
    httpd_resp_set_type(req, mime_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    FILE *f = fopen(file_path, "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    esp_err_t result = send_compressed_stream_cached(file_provider, f, simple_compress_cb, req);
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return result;
}

static esp_err_t file_stream_handler_cached(httpd_req_t *req)
{
    http_info_request_happen();
    char *file_path = "/littlefs/cache/index.html.gz";

    const char *mime_type = get_mime_type("index.html");
    httpd_resp_set_type(req, mime_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    FILE *f = fopen(file_path, "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    const int buf_len = 512;
    uint8_t *buf = malloc(buf_len);
    int read_len = 0;
    do
    {
        read_len = fread(buf, 1, buf_len, f);
        if (read_len > 0)
        {
            httpd_resp_send_chunk(req, (char *)buf, read_len);
        }
    } while (read_len > 0);
    esp_err_t result = httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    free(buf);
    return result;
}

static esp_err_t file_stream_handler_cached_in_mem(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (char *)buffered_file, buffered_file_size);
}
static esp_err_t file_stream_handler_cached_in_mem_optimized(httpd_req_t *req)
{
    http_info_request_happen();
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return ESP_FAIL;
    char *content_length_header[140];
    sprintf((char *)content_length_header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: gzip\r\nContent-Length: %d\r\n\r\n", buffered_file_size);

    // 1. Send headers directly to the socket
    httpd_send(req, (char *)content_length_header, strlen((char *)content_length_header));

    // 2. Send the raw 32 KB memory pointer with zero copying
    httpd_send(req, (const char *)buffered_file, buffered_file_size);
    return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, HTTPD_404);
    httpd_resp_send(req, "404 error happen, please check URL", HTTPD_RESP_USE_STRLEN);
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

void register_http_handlers()
{

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_main_page,
        .user_ctx = NULL,
    };
    const httpd_uri_t index_uri_cached = {
        .uri = "/cached",
        .method = HTTP_GET,
        .handler = file_stream_handler_cached,
        .user_ctx = NULL,
    };
    const httpd_uri_t index_uri_cached_in_mem = {
        .uri = "/cached-in-mem",
        .method = HTTP_GET,
        .handler = file_stream_handler_cached_in_mem,
        .user_ctx = NULL,
    };
    const httpd_uri_t index_uri_cached_in_mem_optimized = {
        .uri = "/cached-in-mem-optimized",
        .method = HTTP_GET,
        .handler = file_stream_handler_cached_in_mem_optimized,
        .user_ctx = NULL,
    };
    const httpd_uri_t index_uri_hello_world = {
        .uri = "/hello-world",
        .method = HTTP_GET,
        .handler = hello_get_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t index_uri_hello_world_optimized = {
        .uri = "/hello-world-optimized",
        .method = HTTP_GET,
        .handler = hello_optimized_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t led_toggle = {
        .uri = "/led/toggle",
        .method = HTTP_GET,
        .handler = led_toggle_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t led_blink = {
        .uri = "/led/blink",
        .method = HTTP_POST,
        .handler = led_blink_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t get_requests_quantity = {
        .uri = "/api/requests-quantity",
        .method = HTTP_GET,
        .handler = get_requests_quantity_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t get_int_sys_info = {
        .uri = "/api/int-sys-info",
        .method = HTTP_GET,
        .handler = get_int_sys_info_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_toggle_led_handled = {
        .uri = "/api/led/toggle",
        .method = HTTP_GET,
        .handler = toggle_led_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_performance_testing_handled = {
        .uri = "/api/performance-testing",
        .method = HTTP_GET,
        .handler = performance_testing_api,
        .user_ctx = NULL,
    };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &index_uri_cached);
    httpd_register_uri_handler(server, &index_uri_cached_in_mem);
    httpd_register_uri_handler(server, &index_uri_cached_in_mem_optimized);
    httpd_register_uri_handler(server, &index_uri_hello_world);
    httpd_register_uri_handler(server, &index_uri_hello_world_optimized);
    httpd_register_uri_handler(server, &led_toggle);
    httpd_register_uri_handler(server, &led_blink);
    httpd_register_uri_handler(server, &get_requests_quantity);
    httpd_register_uri_handler(server, &get_int_sys_info);
    httpd_register_uri_handler(server, &api_toggle_led_handled);
    httpd_register_uri_handler(server, &api_performance_testing_handled);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
}

void start_webserver()
{
    configure_led();
    init_global_zlib();
    init_http_info_requests_counter();
    initialize_touch_events();
    initialize_led();

    initialize_main_page();
    initialize_performance_testing_api();

    blinking_mutex = xSemaphoreCreateMutex();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Looks like connections are not always closed, if false it can cause potential memory leak and stop accept connections at all
    config.lru_purge_enable = true; // must be true because of issues after too many connections if false it will stop accept new connections.
    config.max_uri_handlers = 20;
    config.max_open_sockets = 7;
    config.open_fn = open_fn; // THIS LINE IS SPEEDING UP esp_http_server RESPONSE FROM 65ms TO 10ms
    config.core_id = 1;       // Improves performance on "Hello world" page from 220/s to 350/s
    config.task_priority = 23;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    // config.stack_size = 1024 * 16;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        register_http_handlers();
        ESP_LOGI(TAG, "Server started successfully, registering URI handlers...");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start server");
    }
    generate_static_cache();
}