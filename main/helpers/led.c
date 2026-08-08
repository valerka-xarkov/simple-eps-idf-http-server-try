#include "led_strip.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/timers.h"

#define BLINK_GPIO 48
#define MAX_LED_BRIGHTNESS 255
#define LED_COLOR_COUNTS 3
#define LED_BRIGHT_STEP 10

// static const char *TAG = "LED-HELPER";

static led_strip_handle_t led_strip;
static TimerHandle_t toggle_led_timer = NULL;

typedef struct
{
    int red;
    int green;
    int blue;
} r_g_b_descriptor;

static r_g_b_descriptor led_color = {
    .red = 0,
    .green = 0,
    .blue = 0,
};
static int cur_led_bright = MAX_LED_BRIGHTNESS * 0.2;

void initialize_led()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // one LED on board
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
        .clk_src = RMT_CLK_SRC_DEFAULT,
    };

    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);
    led_color.red = cur_led_bright;
}

static int get_next_index(int index)
{
    return index + 1 >= LED_COLOR_COUNTS ? 0 : index + 1;
}

void toggle_led_callback(TimerHandle_t xTimer)
{
    led_strip_set_pixel(led_strip, 0, led_color.red, led_color.green, led_color.blue);
    led_strip_refresh(led_strip);
    int *rgb[LED_COLOR_COUNTS] = {&led_color.red, &led_color.green, &led_color.blue};
    int cur_index = 0;
    for (int i = 0; i < LED_COLOR_COUNTS; i++)
    {
        if ((*rgb[i] != 0 && *rgb[get_next_index(i)] != 0) || (*rgb[i] != 0 && *rgb[get_next_index(i)] == 0 && *rgb[get_next_index(get_next_index(i))] == 0))
        {
            cur_index = i;
            break;
        }
    }
    int next_index = get_next_index(cur_index);
    int cur_led_bright_step = ((float)cur_led_bright / MAX_LED_BRIGHTNESS) * (float)LED_BRIGHT_STEP;

    *rgb[cur_index] = *rgb[cur_index] - cur_led_bright_step < 0 ? 0 : *rgb[cur_index] - cur_led_bright_step;

    *rgb[next_index] = *rgb[cur_index] == 0 ? cur_led_bright : *rgb[next_index] + cur_led_bright_step > cur_led_bright ? cur_led_bright
                                                                                                                       : *rgb[next_index] + cur_led_bright_step;
    // ESP_LOGI(TAG, "index, next index, values %d %d, %d %d %d", cur_index, next_index, led_color.red, led_color.green, led_color.blue);
}

void switch_led_on()
{
    if (toggle_led_timer != NULL && xTimerIsTimerActive(toggle_led_timer))
    {
        return;
    }
    led_color.red = cur_led_bright;
    led_color.green = 0;
    led_color.blue = 0;

    toggle_led_timer = xTimerCreate("toggle-led-timer", pdMS_TO_TICKS(20), pdTRUE, (void *)0, toggle_led_callback);
    xTimerStart(toggle_led_timer, 0);
}

void switch_led_off()
{
    if (toggle_led_timer != NULL && xTimerIsTimerActive(toggle_led_timer))
    {
        xTimerStop(toggle_led_timer, portMAX_DELAY);
        xTimerDelete(toggle_led_timer, portMAX_DELAY);
    }
    toggle_led_timer = NULL;
    /* Clear the memory or set color array elements to 0 to turn off */
    led_strip_clear(led_strip);
    // ESP_LOGI(TAG, "LED Status: OFF");
}