#include "zlib.h"
#include "esp_err.h"
#include "esp_log.h"
#include "zlib_compressor.h"
#include "esp_heap_caps.h"

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

esp_err_t compress_string_to_buffer(const char *src, uint8_t *dest, size_t dest_max, size_t *out_len)
{
    int ret = deflateReset(&g_strm);
    if (ret != Z_OK)
    {
        ESP_LOGE(TAG, "Deflate reset failed");
        return ESP_FAIL;
    }

    // Assign input parameters
    g_strm.next_in = (uint8_t *)src;
    g_strm.avail_in = strlen(src);

    // Assign output destination parameters
    g_strm.next_out = dest;
    g_strm.avail_out = dest_max;

    // Run the compression pipeline in a single step (Z_FINISH forces total compression)
    ret = deflate(&g_strm, Z_FINISH);

    // Z_STREAM_END indicates that all input was successfully converted and closed out
    if (ret != Z_STREAM_END)
    {
        if (ret == Z_OK)
        {
            ESP_LOGE(TAG, "Destination buffer too small to fit compressed data");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGE(TAG, "Compression failed with error code: %d", ret);
        return ESP_FAIL;
    }

    // Calculate how many bytes were actually written to the output array
    *out_len = dest_max - g_strm.avail_out;
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