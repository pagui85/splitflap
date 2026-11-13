idf_component_register(SRCS "display.cpp" "dashboard.cpp" "panel_timing.c" INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_i2c esp_hw_support esp_timer esp_app_format esp_psram bringup_ethernet bringup_ota)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)

idf_build_get_property(logo_python PYTHON)
set(logo_source "${CMAKE_CURRENT_LIST_DIR}/../../components/bringup_ethernet/assets/favicon.ico")
set(logo_header "${CMAKE_CURRENT_BINARY_DIR}/logo_pixels.hpp")
add_custom_command(OUTPUT "${logo_header}"
    COMMAND "${logo_python}" "${CMAKE_CURRENT_LIST_DIR}/embed_logo.py" "${logo_source}" "${logo_header}"
    DEPENDS "${logo_source}" "${CMAKE_CURRENT_LIST_DIR}/embed_logo.py"
    VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE "${logo_header}")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
