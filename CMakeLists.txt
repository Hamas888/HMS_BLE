# HMS_BLE/CMakeLists.txt

set(HMS_BLE_VERSION 1.0.0)

# Check if we're building with Zephyr
if(DEFINED ZEPHYR_BASE)
    zephyr_library_named(HMS_BLE)
    zephyr_library_sources(src/HMS_BLE.cpp src/nRF/HMS_BLE_ZEPHYR_nRF.cpp)
    target_include_directories(HMS_BLE PUBLIC include)

    # Optional DFU feature (SMP/MCUmgr OTA server) — enabled via Kconfig
    if(CONFIG_HMS_BLE_DFU)
        zephyr_library_sources(src/HMS_DFU.cpp src/nRF/HMS_DFU_ZEPHYR_nRF.cpp)
        # The DFU backend includes img_mgmt headers, which need MCUboot's
        # bootutil include path (exposed via the MCUBOOT_BOOTUTIL interface).
        if(TARGET MCUBOOT_BOOTUTIL)
            zephyr_library_link_libraries(MCUBOOT_BOOTUTIL)
        endif()
    endif()

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

# STM32 / generic CMake project
else()
    add_library(HMS_BLE INTERFACE)
    target_include_directories(HMS_BLE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(HMS_BLE INTERFACE cxx_std_17)
endif()