cmake_minimum_required(VERSION 3.22)

# Keep generated configuration inside each target's build directory.
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
# Explicit sources/components below keep the Arduino/PlatformIO tree isolated.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(splitflap_scaffold VERSION 0.1.0)

if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" VERSION_EQUAL "5.5.6")
    message(FATAL_ERROR "This milestone is validated with ESP-IDF v5.5.6; use the documented release.")
endif()
if(NOT IDF_TARGET STREQUAL "esp32" AND NOT IDF_TARGET STREQUAL "esp32s3" AND NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR "This project supports esp32, esp32s3 and esp32p4.")
endif()
