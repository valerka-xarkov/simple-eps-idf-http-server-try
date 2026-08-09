#include "stdatomic.h"
#include "cJSON.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "esp_log.h"
#include "request_counter.h"
#define QUEUE_SIZE 100

static const char *TAG = "requests-count";

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
    }
    else if (current_time == 0)
    {
        current_time = new_time;
    }
    last_second_requests_count++;
}

int get_size()
{
    return size;
}

void get_requests_quantity_information(struct requests_per_second res[])
{

    for (int i = 0; i < size; i++)
    {
        int index = size >= QUEUE_SIZE ? (head + i) % 100 : i;
        res[i] = requests_information[index];
    }
}
