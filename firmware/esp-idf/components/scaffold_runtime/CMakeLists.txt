idf_component_register(SRCS "scaffold_runtime.cpp" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES esp_event esp_timer esp_system freertos log)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
