#include "esp_http_server.h"
#include "esp_log.h"
#include "main_page_template.h"
#include "../../services/zlib_compressor.h"
#define MEM_ALLOC_STEP 1024

static const char *TAG = "MAIN-PAGE";
static uint8_t *main_page_compressed = NULL;
static size_t main_page_compressed_len;

static char *main_page_content;
static int main_page_buffer_size = MEM_ALLOC_STEP;
static int cur_content_size = 0;

static void callback(void *context, char *template_part)
{
    int len = strlen(template_part);
    int new_len = cur_content_size + len;
    if (main_page_buffer_size < new_len)
    {
        int new_capacity = new_len - main_page_buffer_size < MEM_ALLOC_STEP ? new_len + MEM_ALLOC_STEP : new_len;
        uint8_t *new_dest = heap_caps_realloc(main_page_content, new_capacity, MALLOC_CAP_SPIRAM);
        main_page_content = (char *)new_dest;
    }
    memcpy(main_page_content + cur_content_size, template_part, len);
    cur_content_size = new_len;
}
void initialize_main_page()
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
    main_page_content = (char *)heap_caps_malloc(MEM_ALLOC_STEP, MALLOC_CAP_SPIRAM),

    get_main_template(&template_context, NULL, callback);

    compress_string_to_buffer(main_page_content, cur_content_size, &main_page_compressed, (size_t *)&main_page_compressed_len);
    heap_caps_free(main_page_content);
}

esp_err_t get_main_page(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (char *)main_page_compressed, main_page_compressed_len);
}