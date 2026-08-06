#include "../services/zlib_compressor.h"
#include <sys/stat.h>
#include "esp_log.h"
#include "page_cache_generator.h"
#include <dirent.h>
#include <stdbool.h>
#include <unistd.h>

static const char *TAG = "cache-generator";
uint8_t *buffered_file = NULL;
size_t buffered_file_size = 0;

static void delete_folder_recursively(char *path_to_folder)
{
    DIR *dir = opendir(path_to_folder);
    struct dirent *en;
    int path_to_file_len = 60;
    char *path_to_file[path_to_file_len];
    char file_name[20];
    if (dir)
    {
        while ((en = readdir(dir)) != NULL)
        {
            for (int i = 0; i < 20; i++)
                file_name[i] = en->d_name[i];
            snprintf((char *)path_to_file, path_to_file_len - 1, "%s/%s", path_to_folder, (char *)file_name);
            // ESP_LOGI(TAG, "Path was removed %s", path_to_file);
            remove((char *)path_to_file);
        }
        closedir(dir);
        rmdir((char *)path_to_folder);
        ESP_LOGI(TAG, "Cache directory %s was removed ", path_to_folder);
    }
    else
    {
        ESP_LOGI(TAG, "Cache directory has not existed");
    }
}

static size_t file_provider(char *buf, size_t max_len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fread(buf, 1, max_len, f); // Returns 0 automatically on EOF
}
static uint8_t *read_file_to_buffer(const char *filename, size_t *out_size)
{
    FILE *file = fopen(filename, "rb");
    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fseek(file, 0, SEEK_SET);
    uint8_t *buffer = malloc(file_size + 1);

    size_t bytes_read = fread(buffer, 1, file_size, file);

    fclose(file);

    // Null-terminate the buffer for C-string function compatibility
    buffer[bytes_read] = '\0';
    *out_size = bytes_read;

    return buffer;
}
void generate_static_cache()
{
    char *dir_path = "/littlefs/cache";
    delete_folder_recursively(dir_path);
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
