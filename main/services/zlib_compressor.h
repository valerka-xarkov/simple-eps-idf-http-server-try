#include "esp_err.h"

typedef size_t (*data_provider_cb)(char *buf, size_t max_len, void *user_context);
typedef size_t (*compress_data_cb)(uint8_t *buf, size_t buf_len, void *context);

esp_err_t init_global_zlib(void);
esp_err_t send_compressed_stream_cached(data_provider_cb provide_data, void *dp_context, compress_data_cb compress_cb, void *cc_context);

esp_err_t compress_string_to_buffer(const char *src, uint8_t *dest, size_t dest_max, size_t *out_len);
esp_err_t compress_stream_to_file(FILE *dest_file, data_provider_cb provide_data, void *user_context);

#define COMPRESSOR_BUFFER_SIZE 32768
