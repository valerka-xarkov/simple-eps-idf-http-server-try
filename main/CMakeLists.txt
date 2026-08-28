

idf_component_register(SRCS  "main.c" "helpers/time_set_up.c" "services/request_counter.c" "wifi_config.c" "http_server_handling.c" 
"helpers/fs_operations.c" "services/zlib_compressor.c" "api/requests_quantity.c" "helpers/page_cache_generator.c" "helpers/led.c"
"services/sys_information.c" "api/sys_info.c" "api/led.c" "helpers/touch_events_helper.c" "web-modules/main-page/main_page.c" 
"web-modules/main-page/main_page_template.c" "api/performance-testing.c" "web-modules/assets/template_helpers.c"
                    INCLUDE_DIRS ".") 


# find_program(NODE_EXECUTABLE NAMES node nodejs REQUIRED)

# set(GENERATED_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/main/web-modules/main-page/main_page_template.c")

# set(MY_TARGET ${COMPONENT_LIB})

# add_custom_command(
#     TARGET ${MY_TARGET}
#     PRE_BUILD
#     COMMAND ${NODE_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../template-generator/dist/index.js"
#     COMMENT "Running Node.js script to generate ESP32-S3 templates..."
#     VERBATIM
# )


# Requires the esp_littlefs component added to your project
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front/")
if(EXISTS ${WEB_SRC_DIR}/dist)
    littlefs_create_partition_image(storage ${WEB_SRC_DIR}/dist FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "'${WEB_SRC_DIR}/dist' doesn't exist. Please run 'pnpm build' under '${WEB_SRC_DIR}'")
endif()
