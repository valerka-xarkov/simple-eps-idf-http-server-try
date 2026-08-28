#include "esp_err.h"
#include "stdio.h"
#include "string.h"
#include "template_helpers.h"
#include "esp_heap_caps.h"
#include "../../services/zlib_compressor.h"
#include "esp_log.h"

static const int mem_alloc_step = 1024;
static const char *TAG = "TEMPLATE-HELPERS";

static void template_callback(template_callback_context_t *context, char *template_part)
{
    if (context->buf == NULL)
    {
        context->buf_size = mem_alloc_step;
        context->content_size = 0;
        context->buf = (char *)heap_caps_malloc(mem_alloc_step, MALLOC_CAP_SPIRAM);
    }
    int len = strlen(template_part);
    int new_len = context->content_size + len;
    if (context->buf_size < new_len)
    {
        int new_capacity = new_len - context->buf_size < mem_alloc_step ? new_len + mem_alloc_step : new_len;
        uint8_t *new_dest = heap_caps_realloc(context->buf, new_capacity, MALLOC_CAP_SPIRAM);
        context->buf = (char *)new_dest;
    }
    memcpy(context->buf + context->content_size, template_part, len);
    context->content_size = new_len;
}

void get_compressed_template(page_templage_t template, void *template_context, uint8_t **main_page_compressed, size_t *main_page_compressed_len)
{
    template_callback_context_t tem_cal_context = {
        .buf = (char *)heap_caps_malloc(mem_alloc_step, MALLOC_CAP_SPIRAM),
        .buf_size = mem_alloc_step,
        .content_size = 0,
    };
    template(template_context, &tem_cal_context, template_callback);

    compress_string_to_buffer(tem_cal_context.buf, tem_cal_context.content_size, main_page_compressed, (size_t *)main_page_compressed_len);
    heap_caps_free(tem_cal_context.buf);
}