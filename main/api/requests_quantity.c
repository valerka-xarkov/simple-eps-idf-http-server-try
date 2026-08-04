#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "../services/request_counter.h"
#include "../services/zlib_compressor.h"
#include <time.h>

#define REQUEST_INFO_ITERM_JSON_LENGTH 65
#define QUEUE_SIZE 100
#define ZLIB_RES_BUF_SIZE 1000

// static char *result_buf[REQUEST_INFO_ITERM_JSON_LENGTH * QUEUE_SIZE];
static uint8_t *zlib_res_buf[ZLIB_RES_BUF_SIZE];
static size_t zlib_res_buf_length = 0;
static time_t current_time = 0;
static char *result_buf[REQUEST_INFO_ITERM_JSON_LENGTH * QUEUE_SIZE];

typedef struct
{
    const char *ptr;
    size_t remaining;
} string_source_t;

// size_t string_provider(char *buf, size_t max_len, void *ctx)
// {
//     string_source_t *source = (string_source_t *)ctx;
//     if (source->remaining == 0)
//         return 0;

//     size_t to_write = (source->remaining > max_len) ? max_len : source->remaining;
//     memcpy(buf, source->ptr, to_write);

//     source->ptr += to_write;
//     source->remaining -= to_write;
//     return to_write;
// }

// static size_t simple_compress_cb(uint8_t *buf, size_t buf_len, void *context)
// {
//     httpd_req_t *req = (httpd_req_t *)context;
//     return httpd_resp_send_chunk(req, (char *)buf, buf_len);
// }

static void generate_response(char *resp_buf, struct requests_per_second requests_information[], int quantity)
{

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "count", quantity);
    cJSON *items = cJSON_AddArrayToObject(root, "items");

    for (int i = 0; i < quantity; i++)
    {
        time_t time = requests_information[i].time;
        int requests_quantity = requests_information[i].requests_quantity;
        cJSON *statistic_item = cJSON_CreateObject();
        cJSON_AddNumberToObject(statistic_item, "time", time);
        cJSON_AddNumberToObject(statistic_item, "requestsQuantity", requests_quantity);
        cJSON_AddItemToArray(items, statistic_item);
    }
    char *res = cJSON_PrintUnformatted(root);
    strcpy((char *)resp_buf, res);
    free(res);
    cJSON_Delete(root);
}

esp_err_t get_requests_quantity_handler(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    time_t new_time = time(NULL);
    if (new_time != current_time)
    {
        const int size = get_size();
        struct requests_per_second requests_information[size];
        get_requests_quantity_information(requests_information);
        generate_response((char *)result_buf, (struct requests_per_second *)requests_information, size);

        compress_string_to_buffer((char *)result_buf, (uint8_t *)zlib_res_buf, ZLIB_RES_BUF_SIZE, &zlib_res_buf_length);
        current_time = new_time;
    }
    httpd_resp_send_chunk(req, (char *)zlib_res_buf, zlib_res_buf_length);

    return httpd_resp_send_chunk(req, NULL, 0);
}