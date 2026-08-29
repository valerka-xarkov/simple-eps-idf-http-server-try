#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "../../services/request_counter.h"
#include <time.h>

#define REQUEST_INFO_ITERM_JSON_LENGTH 65
#define QUEUE_SIZE 100
#define ZLIB_RES_BUF_SIZE 1000

static time_t current_time = 0;
static char *result_buf = NULL;

static char *generate_response(struct requests_per_second requests_information[], int quantity)
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
    cJSON_Delete(root);
    return res;
}

esp_err_t get_requests_quantity_handler(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);

    time_t new_time = time(NULL);
    if (new_time != current_time)
    {
        if (result_buf != NULL)
        {
            free(result_buf);
        }
        const int size = get_size();
        struct requests_per_second requests_information[size];
        get_requests_quantity_information(requests_information);
        result_buf = generate_response(requests_information, size);
        current_time = new_time;
    }
    esp_err_t result = httpd_resp_sendstr(req, result_buf);
    return result;
}