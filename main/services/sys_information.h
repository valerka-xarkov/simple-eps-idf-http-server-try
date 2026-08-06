// #include <esp_system.h>

void get_interesting_system_info();
int get_wifi_signal();

struct interesting_system_information
{
    int cpu_frequency;
    const char *model;
    int cores;
    uint32_t cpu_freq;
    int flash_size;
    int total_sram;
    int total_psram;
};

extern struct interesting_system_information int_sys_info;
