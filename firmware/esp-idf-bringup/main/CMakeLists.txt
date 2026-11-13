set(bringup_deps console esp_system esp_hw_support esp_common spi_flash
                 efuse heap freertos log esp_app_format)
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND bringup_deps esp_psram esp_driver_gpio bringup_ethernet bringup_ota)
endif()
idf_component_register(SRCS "bringup_main.cpp" "diagnostic_core.cpp"
    PRIV_REQUIRES ${bringup_deps})
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
# Only a pure policy header is shared. Do not discover/link the Chainlink HAL.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../esp-idf/components/chainlink_bus/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
idf_build_get_property(bringup_python PYTHON)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/build_metadata.hpp"
    COMMAND "${bringup_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_metadata.py"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/build_metadata.hpp"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/bringup_main.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/diagnostic_core.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/diagnostic_core.hpp"
            "${CMAKE_CURRENT_LIST_DIR}/Kconfig.projbuild"
            "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt"
            "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt"
            "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_metadata.py"
            "${SDKCONFIG}"
    VERBATIM)
add_custom_target(bringup_metadata DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/build_metadata.hpp")
add_dependencies(${COMPONENT_LIB} bringup_metadata)
