

#include "esp_http_server.h"
#include "esp_err.h"
#include "esp_log.h"
#include "../../services/request_counter.h"

static char *testing_content = NULL;
static size_t testing_content_size = 0;

static void read_file_to_buffer(const char *filename, char **content, size_t *out_size)
{
    FILE *file = fopen(filename, "rb");
    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    char *buffer = (char *)heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM);
    size_t bytes_read = fread(buffer, 1, file_size, file);

    fclose(file);

    buffer[bytes_read] = '\0';
    *out_size = bytes_read;
    *content = buffer;
}

void initialize_performance_testing_api()
{
    char *source_file_path = "/littlefs/static/index.html";
    read_file_to_buffer(source_file_path, &testing_content, &testing_content_size);
}

esp_err_t performance_testing_api(httpd_req_t *req)
{
    http_info_request_happen();

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    int responseSize = testing_content_size;
    if (buf_len > 1)
    {
        char buf[buf_len];
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            int param_value_size = 7;
            char param_value[param_value_size];
            int value;
            if (httpd_query_key_value(buf, "responseSize", param_value, param_value_size) == ESP_OK && sscanf(param_value, "%d", &value) == 1)
            {
                responseSize = value > responseSize ? responseSize : value < 1 ? 1
                                                                               : value;
            }
        }
    }

    return httpd_resp_send(req, (char *)testing_content, responseSize);
}
