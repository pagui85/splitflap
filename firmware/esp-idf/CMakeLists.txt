cmake_minimum_required(VERSION 3.22)

# Explicit sources/components below keep the Arduino/PlatformIO tree isolated.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(splitflap_scaffold VERSION 0.1.0)

if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" VERSION_EQUAL "5.5.6")
    message(FATAL_ERROR "This milestone is validated with ESP-IDF v5.5.6; use the documented release.")
endif()
if(NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR "Only the provisional esp32 target has been audited for this milestone.")
endif()
