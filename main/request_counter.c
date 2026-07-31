#include "stdatomic.h"
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

char *get_requests_information_char()
{
    if (result_buf[0] != 0)
    {
        return result_buf;
    }

    sprintf(result_buf, "{\"count\": %d, \"items\": [", size);

    char item_string[REQUEST_INFO_ITERM_JSON_LENGTH] = "";
    struct tm *info;
    char formatted_date[30];

    for (int i = 0; i < size; i++)
    {
        int index = size >= QUEUE_SIZE ? (head + i) % 100 : i;

        time_t time = requests_information[index].time;
        int requests_quantity = requests_information[index].requests_quantity;
        info = localtime(&time);

        strftime(formatted_date, sizeof(formatted_date), "%Y-%m-%d %H:%M:%S", info);
        char *comma = (i + 1) == size ? "" : ",";
        sprintf(item_string, "{\"time\": %lld, \"requestsQuantity\": %d, \"dateTime\": \"%s\"}%s", time, requests_quantity, formatted_date, comma);
        strcat(result_buf, item_string);
    }
    strcat(result_buf, "]}\n");

    return result_buf;
}
