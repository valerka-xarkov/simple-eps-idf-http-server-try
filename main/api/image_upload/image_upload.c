#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "../error_handlers/error_handlers.h"
#include "../../services/request_counter.h"
#include <string.h>

static const char *TAG = "IMAGE-UPLOAD";
static const int max_img_size = 1024 * 1024 * 10;

static char *get_response(int post_size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "postSize", post_size);
    cJSON_AddStringToObject(root, "status", "ok");
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    return res;
}

char *get_boundary(char *header)
{
    // multipart/form-data; boundary=----WebKitFormBoundary8os0uwtZPrAtftT2
    return header;
}
esp_err_t upload_image_handler(httpd_req_t *req)
{
    http_info_request_happen();

    if (req->content_len <= 0)
        return http_400_error_handler(req, "Content-Length is not provided in request");
    if (req->content_len > max_img_size)
        return http_413_error_handler(req, max_img_size, req->content_len);

    char content_type[200];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK)
        return http_400_error_handler(req, "Content-Type is not provided in request");

    ESP_LOGI(TAG, "content-length %d, content-type %s", req->content_len, content_type);

    int total_received = 0;
    int remaining = req->content_len;
    int ret;
    int buf_size = 1024 * 1024;
    char *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    // 4. Loop httpd_req_recv until all content bytes are retrieved
    while (remaining > 0)
    {
        // Read into the buffer, offsetting the pointer by bytes already received
        ret = httpd_req_recv(req, buf + total_received, buf_size - 1);

        if (ret <= 0)
        {
            // Check if timeout occurred
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGW(TAG, "Socket timeout, retrying...");
                continue; // Retry the read operation
            }

            // Handle fatal socket error or unexpected disconnection
            ESP_LOGE(TAG, "Socket error or closure during read: %d", ret);
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Socket error");
            return ESP_FAIL;
        }
        total_received += ret;
        remaining -= ret;
    }
    buf[total_received] = 0;
    ESP_LOGI(TAG, "Received %d bytes, remaining: %d", total_received, remaining);
    ESP_LOGI(TAG, "Received content: \n%s", (char *)buf);

    free(buf);

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    char *response = get_response(req->content_len);
    esp_err_t result = httpd_resp_send(req, (char *)response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}
