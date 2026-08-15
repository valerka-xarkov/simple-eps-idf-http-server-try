#include "esp_log.h"
#include "touch_events_helper.h"

static const char *TAG = "LED-TOUCH";

// static int top_button_chanels[] = {4, 5, 6, 7};
static int top_button_chanels[] = {4};
// static int bottom_button_chanels[] = {9, 10, 11, 12, 13, 14};
static int bottom_button_chanels[] = {11};

static char *get_event_name(touch_click_events_helper_t event)
{
    switch (event)
    {
    case TOUCH_CLICK:
        return "TOUCH_CLICK";
    case TOUCH_LONG_TOUCH_START:
        return "TOUCH_LONG_TOUCH_START";
    case TOUCH_LONG_TOUCH_END:
        return "TOUCH_LONG_TOUCH_END";
    };
    return "UNKNOWN_EVENT";
}

static void top_click_handler(touch_click_events_helper_t event)
{
    char *event_name = get_event_name(event);
    ESP_LOGI(TAG, "Up handler %s", event_name);
}

static void bottom_click_handler(touch_click_events_helper_t event)
{
    char *event_name = get_event_name(event);
    ESP_LOGI(TAG, "Down handler %s", event_name);
}

void initialize_led_touch()
{
    add_touch_chanel(top_button_chanels, sizeof(top_button_chanels) / sizeof(int), (touch_event_cb)&top_click_handler);
    add_touch_chanel(bottom_button_chanels, sizeof(bottom_button_chanels) / sizeof(int), bottom_click_handler);
    ESP_LOGI(TAG, "LED touch initialized on pin 4(up click) and 11(down click)");
}
