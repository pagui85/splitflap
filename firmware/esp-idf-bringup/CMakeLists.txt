cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(IDF_TARGET STREQUAL "esp32p4")
    project(splitflap_p4_bringup VERSION 0.4.0)
else()
    project(splitflap_s3_bringup VERSION 0.3.0)
endif()
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" VERSION_EQUAL "5.5.6")
    message(FATAL_ERROR "Use the validated ESP-IDF v5.5.6 release.")
endif()
if(IDF_TARGET STREQUAL "esp32s3")
    if(CONFIG_SPIRAM)
        message(FATAL_ERROR "S3 bring-up keeps PSRAM disabled until the memory interface is identified.")
    endif()
    if(NOT CONFIG_ESP_CONSOLE_UART_DEFAULT OR NOT CONFIG_ESP_CONSOLE_UART_NUM EQUAL 0)
        message(FATAL_ERROR "S3 bring-up requires default UART0.")
    endif()
elseif(IDF_TARGET STREQUAL "esp32p4")
    if(NOT CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG AND NOT CONFIG_ESP_CONSOLE_UART_DEFAULT)
        message(FATAL_ERROR "P4 bring-up requires primary USB Serial/JTAG or default UART0.")
    endif()
    if(CONFIG_BRINGUP_BOARD_U213 AND (NOT CONFIG_ESP32P4_SELECTS_REV_LESS_V3
                                    OR NOT CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
                                    OR NOT CONFIG_ESPTOOLPY_FLASHSIZE_16MB
                                    OR NOT CONFIG_BRINGUP_ETHERNET))
        message(FATAL_ERROR "U213 requires the early P4, USB Serial/JTAG, 16 MB flash and Ethernet profiles.")
    endif()
    if(CONFIG_BRINGUP_U213_OTA AND (NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
                                   OR NOT CONFIG_ESP_HTTPS_SERVER_ENABLE
                                   OR NOT CONFIG_PARTITION_TABLE_CUSTOM
                                   OR NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_u213_ota.csv"))
        message(FATAL_ERROR "U213 OTA requires its dedicated partitions, rollback bootloader and HTTPS.")
    endif()
    if(CONFIG_SPIRAM AND (NOT CONFIG_SPIRAM_IGNORE_NOTFOUND OR NOT CONFIG_SPIRAM_USE_CAPS_ALLOC
                          OR CONFIG_SPIRAM_MEMTEST OR CONFIG_SPIRAM_XIP_FROM_PSRAM
                          OR CONFIG_SPIRAM_FETCH_INSTRUCTIONS OR CONFIG_SPIRAM_RODATA
                          OR CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
                          OR CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY))
        message(FATAL_ERROR "P4 bring-up requires optional PSRAM with no boot-critical allocations.")
    endif()
else()
    message(FATAL_ERROR "Standalone bring-up supports esp32s3 and esp32p4.")
endif()
