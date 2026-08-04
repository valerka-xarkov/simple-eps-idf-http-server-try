idf_component_register(SRCS  "main.c" "time_set_up.c" "services/request_counter.c" "wifi_config.c" "http_server_handling.c" 
"cpu_temperature.c" "fs_operations.c" "services/zlib_compressor.c" "api/requests_quantity.c"
                    INCLUDE_DIRS ".") 

# Requires the esp_littlefs component added to your project
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front/")
if(EXISTS ${WEB_SRC_DIR}/dist)
    littlefs_create_partition_image(storage ${WEB_SRC_DIR}/dist FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "'${WEB_SRC_DIR}/dist' doesn't exist. Please run 'pnpm build' under '${WEB_SRC_DIR}'")
endif()
