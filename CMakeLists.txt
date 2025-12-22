# HMS_BLE/CMakeLists.txt

set(HMS_BLE_VERSION 1.0.0)

# Check if we're building with Zephyr
if(DEFINED ZEPHYR_BASE)
    zephyr_library_named(HMS_BLE)
    zephyr_library_sources(
        "src/HMS_BLE.cpp"
        "src/nRF/HMS_BLE_ZEPHYR_nRF.cpp"
    )
    zephyr_library_include_directories(include)
    
    # Enforce C++ standard for this library using compile options
    target_compile_options(HMS_BLE PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)

# Check if we're building with ESP-IDF
elseif(IDF_PROJECT)
    idf_component_register(
        SRCS "src/HMS_BLE.cpp"
        INCLUDE_DIRS "include"
    )
    
# STM32 / generic CMake project
else()
    add_library(HMS_BLE INTERFACE)
    target_include_directories(HMS_BLE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(HMS_BLE INTERFACE cxx_std_17)
endif()