#include "stdatomic.h"
#include "cJSON.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "esp_log.h"

#define QUEUE_SIZE 100
#define REQUEST_INFO_ITERM_JSON_LENGTH 100

struct requests_per_second
{
    time_t time;
    int requests_quantity;
};

static const char *TAG = "requests-count";
static char result_buf[REQUEST_INFO_ITERM_JSON_LENGTH * QUEUE_SIZE] = "";

static int size = 0;
static int head = 0;
static struct requests_per_second requests_information[QUEUE_SIZE];

static time_t current_time = 0;

static int last_second_requests_count = 0;

void init_http_info_requests_counter()
{
    for (int i = 0; i < QUEUE_SIZE; i++)
    {
        struct requests_per_second e = {
            .time = 0,
            .requests_quantity = 0,
        };
        requests_information[i] = e;
    }
    ESP_LOGI(TAG, "Requests counter initialized");
}

void http_info_request_happen()
{
    time_t new_time = time(NULL);

    if (new_time > current_time && current_time != 0)
    {
        requests_information[head].requests_quantity = last_second_requests_count;
        requests_information[head].time = current_time;

        int new_head = head + 1;

        head = new_head >= QUEUE_SIZE ? 0 : new_head;
        size = size >= QUEUE_SIZE ? QUEUE_SIZE : new_head;

        current_time = new_time;
        last_second_requests_count = 0;
        result_buf[0] = 0;
    }
    else if (current_time == 0)
    {
        current_time = new_time;
    }
    last_second_requests_count++;
}

char *get_requests_information_http()
{
    if (result_buf[0] != 0)
    {
        return result_buf;
    }

    struct tm *info;
    char formatted_date[30];
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "count", size);
    cJSON *items = cJSON_AddArrayToObject(root, "items");

    for (int i = 0; i < size; i++)
    {
        int index = size >= QUEUE_SIZE ? (head + i) % 100 : i;
        time_t time = requests_information[index].time;
        int requests_quantity = requests_information[index].requests_quantity;
        info = localtime(&time);
        strftime(formatted_date, sizeof(formatted_date), "%Y-%m-%d %H:%M:%S", info);

        cJSON *statistic_item = cJSON_CreateObject();
        cJSON_AddNumberToObject(statistic_item, "time", time);
        cJSON_AddNumberToObject(statistic_item, "requestsQuantity", requests_quantity);
        cJSON_AddStringToObject(statistic_item, "dateTime", formatted_date);
        cJSON_AddItemToArray(items, statistic_item);
    }

    char *res = cJSON_PrintUnformatted(root);
    strcpy(result_buf, res);
    free(res);
    cJSON_Delete(root);
    return result_buf;
}
