# Dependencies are discovered before Kconfig is loaded. Only the opt-in
# profile compiles this component's source and references the network stack.
if(CONFIG_BRINGUP_ETHERNET)
    set(ethernet_sources "ethernet.cpp")
endif()
if(CONFIG_BRINGUP_U213_OTA)
    set(web_assets "assets/favicon.ico")
endif()
idf_component_register(SRCS ${ethernet_sources} EMBED_FILES ${web_assets} INCLUDE_DIRS "."
    PRIV_REQUIRES esp_eth esp_event esp_netif esp_http_server esp_driver_gpio esp_app_format soc bringup_ota)
if(CONFIG_BRINGUP_ETHERNET)
    target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
endif()
