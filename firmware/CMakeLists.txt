cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

# Firmware version — embedded via esp_app_description_t.version.
# Bump on each release; keep in sync with UI DeviceInfo.firmware_version.
set(PROJECT_VER "0.5.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meatreader)

spiffs_create_partition_image(storage ../thermometer-ui/dist FLASH_IN_PROJECT)
