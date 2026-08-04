#include "../services/zlib_compressor.h"
#include <sys/stat.h>
#include "esp_log.h"
#include "page_cache_generator.h"

static const char *TAG = "cache-generator";
char *buffered_file = NULL;
size_t buffered_file_size = 0;

static size_t file_provider(char *buf, size_t max_len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fread(buf, 1, max_len, f); // Returns 0 automatically on EOF
}
static char *read_file_to_buffer(const char *filename, size_t *out_size)
{
    FILE *file = fopen(filename, "rb");
    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(file_size + 1);

    size_t bytes_read = fread(buffer, 1, file_size, file);

    fclose(file);

    // Null-terminate the buffer for C-string function compatibility
    buffer[bytes_read] = '\0';
    *out_size = bytes_read;

    return buffer;
}
void generate_static_cache()
{
    const char *dir_path = "/littlefs/cache";
    mkdir(dir_path, 0777);
    char *source_file_path = "/littlefs/static/index.html";

    char *dest_file_path = "/littlefs/cache/index.html.gz";

    FILE *source_f = fopen(source_file_path, "r");
    FILE *dest_f = fopen(dest_file_path, "wb");
    compress_stream_to_file(dest_f, file_provider, source_f);
    fclose(source_f);
    fclose(dest_f);

    buffered_file = read_file_to_buffer("/littlefs/cache/index.html.gz", &buffered_file_size);
    ESP_LOGI(TAG, "Cache has been generated successfully");
}
