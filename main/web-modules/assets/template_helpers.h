typedef struct template_callback_context
{
    int buf_size;
    int content_size;
    char *buf;
} template_callback_context_t;

typedef void (*template_callback_t)(template_callback_context_t *context, char *template_part);
typedef void (*page_templage_t)(void *data, template_callback_context_t *cb_context, template_callback_t cb);

void get_compressed_template(page_templage_t template, void *template_context, uint8_t **main_page_compressed, size_t *main_page_compressed_len);
