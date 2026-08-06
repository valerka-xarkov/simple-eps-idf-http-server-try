#include <esp_system.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_clk_tree.h"
#include "soc/soc_caps.h"
#include "esp_flash.h"
#include "sys_information.h"
#include "esp_wifi.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "sys-info-service";
static struct interesting_system_information int_sys_info;
static temperature_sensor_handle_t temp_handle = NULL;

static const char *esp_chip_model_to_str(esp_chip_model_t model)
{
    switch (model)
    {
    case CHIP_ESP32:
        return "ESP32 Xtensa LX6";
    case CHIP_ESP32S2:
        return "ESP32-S2 Xtensa LX7";
    case CHIP_ESP32S3:
        return "ESP32-S3 Xtensa LX7";
    case CHIP_ESP32C2:
        return "ESP32-C2 RISC-V (RV32IMC)";
    case CHIP_ESP32C3:
        return "ESP32-C3 RISC-V (RV32IMC)";
    case CHIP_ESP32C5:
        return "ESP32-C5";
    case CHIP_ESP32C6:
        return "ESP32-C6 RISC-V (RV32IMAC)";
    case CHIP_ESP32C61:
        return "ESP32-C61";
    case CHIP_ESP32H2:
        return "ESP32-H2 RISC-V (RV32IMAC)";
    case CHIP_ESP32P4:
        return "ESP32-P4 RISC-V HP Core";
    case CHIP_POSIX_LINUX:
        return "POSIX/Linux Simulator";
    default:
        return "Unknown ESP Chip";
    }
}

void initialize_int_sys_info()
{

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    int_sys_info.model = esp_chip_model_to_str(chip_info.model);
    // ESP_LOGI(TAG, "Chip: cores, esp model: %d, %s", chip_info.cores, int_sys_info.model);
    uint32_t cpu_freq_hz;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &cpu_freq_hz);
    uint32_t cpu_freq_mhz = cpu_freq_hz / 1000000;
    ESP_LOGI(TAG, "Current CPU Frequency: %lu MHz", cpu_freq_mhz);
    int_sys_info.cores = chip_info.cores;

    // int_sys_info.model = malloc(chip_info.model);

    // strcpy(int_sys_info.model, model);
    int_sys_info.cpu_freq = cpu_freq_hz;

    // Flash Memory Info
    uint32_t flash_size;
    esp_flash_get_physical_size(NULL, &flash_size);
    // ESP_LOGI(TAG, "Physical flash size: %lu MB", flash_size / (1024 * 1024));

    size_t total_sram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (total_psram == 0)
    {
        int_sys_info.total_psram = 0;
    }
    else
    {
        int_sys_info.total_psram = total_psram;
    }

    int_sys_info.flash_size = flash_size;
    int_sys_info.total_sram = total_sram;

    // 1. Define the anticipated temperature range (e.g., -10°C to 80°C)
    temperature_sensor_config_t temp_sensor_config = {
        .range_min = -10,
        .range_max = 80,
    };

    temperature_sensor_install(&temp_sensor_config, &temp_handle);
}

int get_wifi_signal()
{
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err == ESP_OK)
    {
        return ap_info.rssi;
    }
    return 0;
}

struct interesting_system_information get_sys_int_info()
{
    int_sys_info.wifi_signal = get_wifi_signal();
    int_sys_info.free_ram = xPortGetFreeHeapSize();
    int_sys_info.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    // 4. Retrieve the temperature in Celsius
    float celsius = 0;
    temperature_sensor_enable(temp_handle);
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &celsius));
    temperature_sensor_disable(temp_handle);
    int_sys_info.cpu_temperature = celsius;

    return int_sys_info;
}
