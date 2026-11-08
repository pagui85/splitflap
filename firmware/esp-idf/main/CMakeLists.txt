idf_component_register(SRCS "app_main.cpp"
    PRIV_REQUIRES scaffold_platform scaffold_runtime chainlink_bus esp_event esp_common log)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
