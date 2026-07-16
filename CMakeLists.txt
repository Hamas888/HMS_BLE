# HMS_BLE/CMakeLists.txt

set(HMS_BLE_VERSION 1.0.0)

# Check if we're building with Zephyr
if(DEFINED ZEPHYR_BASE)
    zephyr_library_named(HMS_BLE)
    zephyr_library_sources(src/HMS_BLE.cpp src/nRF/HMS_BLE_ZEPHYR_nRF.cpp)
    target_include_directories(HMS_BLE PUBLIC include)

# Check if we're building with ESP-IDF
elseif(IDF_PROJECT)
    idf_component_register(
        SRCS "src/HMS_BLE.cpp"
        INCLUDE_DIRS "include"
    )

# Check if we're building on Linux with BlueZ
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BLUEZ REQUIRED bluez)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)

    add_library(HMS_BLE
        src/HMS_BLE.cpp
        src/Linux/HMS_BLE_BLUEZ_Linux.cpp
    )
    target_include_directories(HMS_BLE PUBLIC include)
    target_link_libraries(HMS_BLE PUBLIC
        ${BLUEZ_LIBRARIES}
        ${SYSTEMD_LIBRARIES}
    )
    target_compile_features(HMS_BLE PUBLIC cxx_std_17)
    target_compile_definitions(HMS_BLE PRIVATE HMS_BLE_BLUEZ_LINUX)

# STM32 / generic CMake project
else()
    add_library(HMS_BLE INTERFACE)
    target_include_directories(HMS_BLE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(HMS_BLE INTERFACE cxx_std_17)
endif()