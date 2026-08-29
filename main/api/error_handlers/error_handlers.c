#include "esp_http_server.h"

esp_err_t http_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (err == HTTPD_404_NOT_FOUND)
    {
        httpd_resp_set_status(req, HTTPD_404);
        return httpd_resp_send(req, "404 error happen, please check URL", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "Something is wrong. No idea what happen", HTTPD_RESP_USE_STRLEN);
}