if(CONFIG_BRINGUP_U213_OTA)
    set(ota_sources "ota.cpp" "persistent.cpp" "credentials.cpp")
endif()
idf_component_register(SRCS ${ota_sources} INCLUDE_DIRS "."
    REQUIRES esp_http_server
    PRIV_REQUIRES app_update bootloader_support esp_app_format esp_hw_support esp_partition
                  esp_https_server esp_timer nvs_flash mbedtls)
if(CONFIG_BRINGUP_U213_OTA)
    target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u splitflap_u213_ota_descriptor")
endif()
