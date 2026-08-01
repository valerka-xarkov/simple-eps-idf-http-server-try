#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "zlib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "request_counter.h"
#include "esp_timer.h"

#define OUTPUT_LED 22
#define MAX_BLINK_POST_SIZE 220
#define LED_ON 0
#define LED_OFF 1
#define BUFFER_SIZE 1024

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

typedef size_t (*data_provider_cb)(char *buf, size_t max_len, void *user_context);
static z_stream g_strm;

typedef struct
{
    const char *ptr;
    size_t remaining;
} string_source_t;

size_t string_provider(char *buf, size_t max_len, void *ctx)
{
    string_source_t *source = (string_source_t *)ctx;
    if (source->remaining == 0)
        return 0;

    size_t to_write = (source->remaining > max_len) ? max_len : source->remaining;
    memcpy(buf, source->ptr, to_write);

    source->ptr += to_write;
    source->remaining -= to_write;
    return to_write;
}

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
    httpd_resp_set_type(req, "application/json");
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
    ESP_LOGI(TAG, "Blink finished");

    if (xSemaphoreTake(blinking_mutex, pdMS_TO_TICKS(50)))
    {

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
    if (total_len > MAX_BLINK_POST_SIZE)
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

esp_err_t send_compressed_stream_cached(httpd_req_t *req, data_provider_cb provide_data, void *user_context)
{
    int ret = deflateReset(&g_strm);
    if (ret != Z_OK)
    {
        ESP_LOGE(TAG, "Deflate engine reset failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    uint8_t out_buf[BUFFER_SIZE];
    char in_buf[BUFFER_SIZE];
    esp_err_t err = ESP_OK;

    while (true)
    {
        size_t read_bytes = provide_data(in_buf, sizeof(in_buf), user_context);
        if (read_bytes == 0)
            break; // Source function empty

        g_strm.next_in = (uint8_t *)in_buf;
        g_strm.avail_in = read_bytes;

        while (g_strm.avail_in > 0)
        {
            g_strm.next_out = out_buf;
            g_strm.avail_out = BUFFER_SIZE;

            deflate(&g_strm, Z_NO_FLUSH);

            size_t compressed_len = BUFFER_SIZE - g_strm.avail_out;
            if (compressed_len > 0)
            {
                err = httpd_resp_send_chunk(req, (char *)out_buf, compressed_len);
                if (err != ESP_OK)
                    return err;
            }
        }
    }

    // Finalize GZIP stream tail blocks
    bool finished = false;
    while (!finished)
    {
        g_strm.next_out = out_buf;
        g_strm.avail_out = BUFFER_SIZE;

        ret = deflate(&g_strm, Z_FINISH);
        if (ret == Z_STREAM_END)
        {
            finished = true;
        }

        size_t compressed_len = BUFFER_SIZE - g_strm.avail_out;
        if (compressed_len > 0)
        {
            err = httpd_resp_send_chunk(req, (char *)out_buf, compressed_len);
            if (err != ESP_OK)
                return err;
        }
    }

    // Signal end of chunked session to client
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

size_t file_provider(char *buf, size_t max_len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fread(buf, 1, max_len, f); // Returns 0 automatically on EOF
}

esp_err_t file_stream_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/data.json", "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    esp_err_t result = send_compressed_stream(req, file_provider, f);
    fclose(f);
    return result;
}

static esp_err_t get_requests_quantity_handler(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_type(req, "application/json");

    const char *resp_str = get_requests_information_http();
    string_source_t state = {.ptr = resp_str, .remaining = strlen(resp_str)};

    return send_compressed_stream_cached(req, string_provider, &state);
    // httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    // return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, HTTPD_404);
    httpd_resp_send(req, "404 error happen, please check URL", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t init_global_zlib(void)
{
    g_strm.zalloc = Z_NULL;
    g_strm.zfree = Z_NULL;
    g_strm.opaque = Z_NULL;
    // 12 + 16 windowBits configures it for memory-friendly GZIP framing
    deflateInit2(&g_strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 12 + 16, 4, Z_DEFAULT_STRATEGY);
    ESP_LOGI(TAG, "Cached single-threaded zlib engine initialized");
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

    const httpd_uri_t hello_world_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = hello_get_handler,
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
        .uri = "/requests-quantity",
        .method = HTTP_GET,
        .handler = get_requests_quantity_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &hello_world_uri);
    httpd_register_uri_handler(server, &led_toggle);
    httpd_register_uri_handler(server, &led_blink);
    httpd_register_uri_handler(server, &get_requests_quantity);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
}
void start_webserver()
{
    configure_led();
    init_global_zlib();
    init_http_info_requests_counter();

    blinking_mutex = xSemaphoreCreateMutex();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = false;
    config.max_open_sockets = 7;
    config.open_fn = open_fn; // THIS LINE IS SPEEDING UP esp_http_server RESPONSE FROM 65ms TO 10ms
    config.core_id = 1;       // Improves performance on "Hello world" page from 320/s to 350/s

    if (httpd_start(&server, &config) == ESP_OK)
    {
        register_http_handlers();
        ESP_LOGI(TAG, "Server started successfully, registering URI handlers...");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start server");
    }
}