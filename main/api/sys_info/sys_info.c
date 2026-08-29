#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "../../services/sys_information.h"
#include "esp_log.h"
#include "../../services/request_counter.h"
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"

// static const char *TAG = "sys-info-api";
static time_t cur_time = 0;
static char *result_buf;

static void generate_response()
{
    time_t new_time = time(NULL);
    if (cur_time >= new_time)
    {
        return;
    }
    cur_time = new_time;
    struct interesting_system_information int_sys_info = get_sys_int_info();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "cpuFrequency", int_sys_info.cpu_freq);
    cJSON_AddNumberToObject(root, "cores", int_sys_info.cores);
    cJSON_AddStringToObject(root, "model", int_sys_info.model);
    cJSON_AddNumberToObject(root, "flashSize", int_sys_info.flash_size);
    cJSON_AddNumberToObject(root, "totalSram", int_sys_info.total_sram);
    cJSON_AddNumberToObject(root, "freeSram", int_sys_info.free_ram);
    cJSON_AddNumberToObject(root, "totalPsram", int_sys_info.total_psram);
    cJSON_AddNumberToObject(root, "freePsram", int_sys_info.free_psram);
    cJSON_AddNumberToObject(root, "wifiSignal", int_sys_info.wifi_signal);
    cJSON_AddNumberToObject(root, "cpuTemperature", int_sys_info.cpu_temperature);

    result_buf = cJSON_PrintUnformatted(root);
    // ESP_LOGE(TAG, "result_buf after %s", result_buf);
    cJSON_Delete(root);
}

esp_err_t get_int_sys_info_handler(httpd_req_t *req)
{
    http_info_request_happen();
    generate_response();
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    esp_err_t result = httpd_resp_sendstr(req, (char *)result_buf);
    free(result_buf);
    return result;
}