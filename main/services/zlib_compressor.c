#include "zlib.h"
#include "esp_err.h"
#include "esp_log.h"
#include "zlib_compressor.h"
#include "esp_heap_caps.h"

#define CHUNK_OUT_SIZE 2048
static z_stream g_strm;
static uint8_t *out_buf = NULL;
static char *in_buf = NULL;
static const char *TAG = "zlib-compressor";

// 1. Define custom allocation functions for zlib
void *zlib_psram_alloc(void *opaque, unsigned int items, unsigned int size)
{
    return heap_caps_malloc(items * size, MALLOC_CAP_SPIRAM);
}

void zlib_psram_free(void *opaque, void *address)
{
    free(address);
}

esp_err_t init_global_zlib(void)
{
    g_strm.zalloc = zlib_psram_alloc; // Directs all internal allocations to PSRAM
    g_strm.zfree = zlib_psram_free;
    g_strm.opaque = Z_NULL;
    out_buf = (uint8_t *)heap_caps_malloc(COMPRESSOR_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    in_buf = (char *)heap_caps_malloc(COMPRESSOR_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    // 12 + 16 windowBits configures it for memory-friendly GZIP framing, 4 for 40kb mem usage
    int windowsBits = 15;
    int GZIP_ENCODING = 16;
    deflateInit2(&g_strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowsBits | GZIP_ENCODING, 8, Z_DEFAULT_STRATEGY);
    ESP_LOGI(TAG, "Cached single-threaded zlib engine initialized");
    return ESP_OK;
}

esp_err_t send_compressed_stream_cached(data_provider_cb provide_data, void *dp_context, compress_data_cb compress_cb, void *cc_context)
{
    int ret = deflateReset(&g_strm);
    if (ret != Z_OK)
    {
        ESP_LOGE(TAG, "Deflate engine reset failed");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;

    while (true)
    {
        size_t read_bytes = provide_data((char *)in_buf, COMPRESSOR_BUFFER_SIZE, dp_context);

        if (read_bytes == 0)
            break; // Source function empty

        g_strm.next_in = (uint8_t *)in_buf;
        g_strm.avail_in = read_bytes;

        while (g_strm.avail_in > 0)
        {
            g_strm.next_out = out_buf;
            g_strm.avail_out = COMPRESSOR_BUFFER_SIZE;

            deflate(&g_strm, Z_NO_FLUSH);

            size_t compressed_len = COMPRESSOR_BUFFER_SIZE - g_strm.avail_out;
            if (compressed_len > 0)
            {
                err = compress_cb((uint8_t *)out_buf, compressed_len, cc_context);
                if (err != ESP_OK)
                    return err;
            }
        }
    }

    // Finalize GZIP stream tail blocks
    bool finished = false;
    while (!finished)
    {
        g_strm.next_out = out_buf;
        g_strm.avail_out = COMPRESSOR_BUFFER_SIZE;

        ret = deflate(&g_strm, Z_FINISH);
        if (ret == Z_STREAM_END)
        {
            finished = true;
        }

        size_t compressed_len = COMPRESSOR_BUFFER_SIZE - g_strm.avail_out;
        if (compressed_len > 0)
        {
            err = compress_cb((uint8_t *)out_buf, compressed_len, cc_context);

            if (err != ESP_OK)
                return err;
        }
    }

    // Signal end of chunked session to client
    return ESP_OK;
}

esp_err_t compress_string_to_buffer(char *input_string, int string_len, uint8_t **out_buffer, size_t *out_size)
{
    if (deflateReset(&g_strm) != Z_OK)
    {
        ESP_LOGE(TAG, "Deflate reset failed");
        return ESP_FAIL;
    }

    // Assign the whole string to the entry tracking windows immediately
    g_strm.next_in = (z_const Bytef *)input_string;
    g_strm.avail_in = (uInt)string_len;

    // Allocate an initial dynamic destination buffer array space safely
    size_t current_capacity = CHUNK_OUT_SIZE;
    uint8_t *dest_buffer = (uint8_t *)heap_caps_malloc(current_capacity, MALLOC_CAP_SPIRAM);
    if (dest_buffer == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    size_t total_written = 0;

    int ret;
    // Compress string elements using the cached intermediate array space
    do
    {
        g_strm.next_out = out_buf;
        g_strm.avail_out = CHUNK_OUT_SIZE;

        // Since the entire payload data is visible in next_in, tell zlib to finish the stream output straight away
        ret = deflate(&g_strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR)
        {
            ESP_LOGE(TAG, "Critical string processing engine deflation failure");
            heap_caps_free(dest_buffer);
            return ESP_FAIL;
        }

        size_t produced = CHUNK_OUT_SIZE - g_strm.avail_out;
        if (produced > 0)
        {
            // Scale out target buffer memory layout if space limitations are hit
            if (total_written + produced > current_capacity)
            {
                current_capacity += produced; // Dynamically expand precisely by the chunk requirements
                uint8_t *new_dest = heap_caps_realloc(dest_buffer, current_capacity, MALLOC_CAP_SPIRAM);
                if (new_dest == NULL)
                {
                    heap_caps_free(dest_buffer);
                    return ESP_ERR_NO_MEM;
                }
                dest_buffer = new_dest;
            }
            // Move payload blocks from intermediate cache to their permanent dynamic home
            memcpy(dest_buffer + total_written, out_buf, produced);
            total_written += produced;
        }
    } while (ret != Z_STREAM_END);

    // Pass back pointer structures and size details safely
    *out_buffer = dest_buffer;
    *out_size = total_written;

    ESP_LOGI(TAG, "String successfully compressed. Original: %u bytes -> Compressed: %u bytes.", string_len, total_written);
    return ESP_OK;
}

esp_err_t compress_stream_to_file(FILE *dest_file, data_provider_cb provide_data, void *user_context)
{
    int ret = deflateReset(&g_strm);
    if (ret != Z_OK)
    {
        ESP_LOGE(TAG, "Deflate file engine reset failed");
        return ESP_FAIL;
    }

    // Core Loop: Pull dynamic text, compress, write out to storage
    while (true)
    {
        size_t read_bytes = provide_data(in_buf, sizeof(in_buf), user_context);
        if (read_bytes == 0)
            break; // Source function is complete

        g_strm.next_in = (uint8_t *)in_buf;
        g_strm.avail_in = read_bytes;

        while (g_strm.avail_in > 0)
        {
            g_strm.next_out = out_buf;
            g_strm.avail_out = COMPRESSOR_BUFFER_SIZE;

            deflate(&g_strm, Z_NO_FLUSH);

            size_t compressed_len = COMPRESSOR_BUFFER_SIZE - g_strm.avail_out;
            if (compressed_len > 0)
            {
                if (fwrite(out_buf, 1, compressed_len, dest_file) != compressed_len)
                {
                    ESP_LOGE(TAG, "Disk write error during stream execution");
                    return ESP_FAIL;
                }
            }
        }
    }

    // Finalize GZIP framing footer tags and write remainder
    bool finished = false;
    while (!finished)
    {
        g_strm.next_out = out_buf;
        g_strm.avail_out = COMPRESSOR_BUFFER_SIZE;

        ret = deflate(&g_strm, Z_FINISH);
        if (ret == Z_STREAM_END)
        {
            finished = true;
        }

        size_t compressed_len = COMPRESSOR_BUFFER_SIZE - g_strm.avail_out;
        if (compressed_len > 0)
        {
            if (fwrite(out_buf, 1, compressed_len, dest_file) != compressed_len)
            {
                ESP_LOGE(TAG, "Disk write error during stream finalization");
                return ESP_FAIL;
            }
        }
    }

    return ESP_OK;
}

esp_err_t compress_stream_to_buffer(data_provider_cb read_stream_func, void *cxt, uint8_t **out_buffer, size_t *out_size)
{
    if (deflateReset(&g_strm) != Z_OK)
    {
        ESP_LOGE(TAG, "Deflate file engine reset failed");
        return ESP_FAIL;
    }

    size_t current_capacity = 4096;
    uint8_t *dest_buffer = (uint8_t *)heap_caps_malloc(current_capacity, MALLOC_CAP_SPIRAM);

    if (dest_buffer == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    size_t total_written = 0;
    bool is_stream_finished = false;

    while (!is_stream_finished)
    {
        // --- 1. INVOKE STREAM CALLBACK GENERATOR ---
        // Dynamically request next stream slice using user-defined callback logic
        size_t bytes_read = read_stream_func(in_buf, COMPRESSOR_BUFFER_SIZE, cxt);

        if (bytes_read == 0)
        {
            is_stream_finished = true;
            break; // Move to flushing sequence immediately
        }

        g_strm.next_in = (uint8_t *)in_buf;
        g_strm.avail_in = bytes_read;

        // --- 2. DEFLATE THE GENERATED SEGMENT CHUNKS ---
        while (g_strm.avail_in > 0)
        {
            g_strm.next_out = out_buf;
            g_strm.avail_out = COMPRESSOR_BUFFER_SIZE;

            int ret = deflate(&g_strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR)
            {
                ESP_LOGE(TAG, "Compression mapping pipeline crash");
                heap_caps_free(dest_buffer);
                return ESP_FAIL;
            }

            size_t produced = COMPRESSOR_BUFFER_SIZE - g_strm.avail_out;
            if (produced > 0)
            {
                if (total_written + produced > current_capacity)
                {
                    current_capacity += (produced > 1024) ? produced : 1024;
                    uint8_t *new_dest = heap_caps_realloc(dest_buffer, current_capacity, MALLOC_CAP_SPIRAM);
                    if (new_dest == NULL)
                    {
                        heap_caps_free(dest_buffer);
                        return ESP_ERR_NO_MEM;
                    }
                    dest_buffer = new_dest;
                }
                memcpy(dest_buffer + total_written, out_buf, produced);
                total_written += produced;
            }
        }
    }

    // --- 3. FINAL FLUSH AND COMPACTION ---
    int flush_ret;
    do
    {
        g_strm.next_out = out_buf;
        g_strm.avail_out = COMPRESSOR_BUFFER_SIZE;

        flush_ret = deflate(&g_strm, Z_FINISH);
        if (flush_ret == Z_STREAM_ERROR)
        {
            heap_caps_free(dest_buffer);
            return ESP_FAIL;
        }

        size_t produced = COMPRESSOR_BUFFER_SIZE - g_strm.avail_out;
        if (produced > 0)
        {
            if (total_written + produced > current_capacity)
            {
                current_capacity += produced;
                uint8_t *new_dest = heap_caps_realloc(dest_buffer, current_capacity, MALLOC_CAP_SPIRAM);
                if (new_dest == NULL)
                {
                    heap_caps_free(dest_buffer);
                    return ESP_ERR_NO_MEM;
                }
                dest_buffer = new_dest;
            }
            memcpy(dest_buffer + total_written, out_buf, produced);
            total_written += produced;
        }
    } while (flush_ret != Z_STREAM_END);

    *out_buffer = dest_buffer;
    *out_size = total_written;

    return ESP_OK;
}
