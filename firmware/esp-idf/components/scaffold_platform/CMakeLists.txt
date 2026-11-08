idf_component_register(SRCS "scaffold_platform.cpp" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES nvs_flash)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
