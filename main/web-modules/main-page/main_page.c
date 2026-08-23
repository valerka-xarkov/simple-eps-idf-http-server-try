#include "esp_http_server.h"
#include "esp_log.h"
#include "main_page_template.h"

static const char *TAG = "MAIN-PAGE";

void callback(void *context, char *template_part)
{
    httpd_req_t *req = (httpd_req_t *)context;
    httpd_resp_send_chunk(req, template_part, HTTPD_RESP_USE_STRLEN);
}

esp_err_t get_main_page(httpd_req_t *req)
{
    main_page_template_post_t posts[] = {
        {
            .title = "First-title",
            .date = "01-02-2020",
        },
        {
            .title = "Second-title",
            .date = "07-08-2921",
        },
    };
    main_page_template_context_t template_context = {
        .posts_count = 2,
        .age = 192838,
        .name = "My-First_name",
        .surname = "My-First-Surname",
        .test1 = "some-test-1",
        .test2 = "some-test-2",
        .posts = posts,
    };
    get_main_template((main_page_template_context_t *)&template_context, (void *)req, callback);
    return httpd_resp_send_chunk(req, NULL, 0);
}