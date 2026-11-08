idf_component_register(SRCS "chainlink_bus.cpp" INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_gpio esp_driver_spi freertos
    PRIV_REQUIRES heap log)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
