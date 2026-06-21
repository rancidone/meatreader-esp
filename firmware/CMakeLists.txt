cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

# Firmware version — embedded via esp_app_description_t.version.
# Appends the short git hash so OTA builds are uniquely identifiable.
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_HASH)
    set(PROJECT_VER "0.5.0-${GIT_HASH}")
else()
    set(PROJECT_VER "0.5.0")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meatreader)

# Copy the final binary to the repo root after each build for easy access.
add_custom_command(
    TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
        "${CMAKE_SOURCE_DIR}/../meatreader.bin"
    COMMENT "Copying meatreader.bin to repo root"
    VERBATIM
)

