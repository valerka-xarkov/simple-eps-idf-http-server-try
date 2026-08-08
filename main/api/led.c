#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "../services/request_counter.h"
#include "../helpers/led.h"

static bool is_led_on = false;

static char *generate_toggle_led_response(bool led_status)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "led", led_status ? "on" : "off");
    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return response;
}

esp_err_t toggle_led_handler(httpd_req_t *req)
{
    http_info_request_happen();
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);

    is_led_on = !is_led_on;
    if (is_led_on)
        switch_led_on();
    else
        switch_led_off();
    char *response = generate_toggle_led_response(is_led_on);
    esp_err_t result = httpd_resp_send(req, (char *)response, HTTPD_RESP_USE_STRLEN);
    free(response);

    return result;
}