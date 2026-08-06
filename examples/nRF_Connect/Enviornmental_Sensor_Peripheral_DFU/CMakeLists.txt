cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------------
# Auto-fetch HMS_BLE + ChronoLog modules (GitHub) into Modules/ if missing.
# The example is meant to be copied out and used standalone — CMake fetches
# both modules so the build "just works", no manual setup.
# ---------------------------------------------------------------------------

# ---- Fetch HMS_BLE if missing ----
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Modules/HMS_BLE/CMakeLists.txt")
    message(STATUS "Fetching HMS_BLE module (GitHub)...")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Modules")
    execute_process(
        COMMAND git clone --depth 1
                https://github.com/Hamas888/HMS_BLE.git
                "${CMAKE_CURRENT_SOURCE_DIR}/Modules/HMS_BLE"
        RESULT_VARIABLE HMS_BLE_FETCH_RESULT
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT HMS_BLE_FETCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to auto-fetch HMS_BLE from GitHub.")
    endif()
endif()

# ---- Fetch ChronoLog if missing ----
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Modules/ChronoLog/CMakeLists.txt")
    message(STATUS "Fetching ChronoLog module (GitHub)...")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Modules")
    execute_process(
        COMMAND git clone --depth 1
                https://github.com/Hamas888/ChronoLog.git
                "${CMAKE_CURRENT_SOURCE_DIR}/Modules/ChronoLog"
        RESULT_VARIABLE CHRONOLOG_FETCH_RESULT
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT CHRONOLOG_FETCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to auto-fetch ChronoLog from GitHub.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Config overlays BEFORE find_package(Zephyr)
# ---------------------------------------------------------------------------
set(CONF_FILE
    "prj.conf;Modules/HMS_BLE/HMS_BLE.conf;Modules/HMS_BLE/HMS_DFU.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(BLE_lib_dev)

# Force C++17 support for HMS_BLE library
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)

# Add HMS_BLE + ChronoLog as subdirectories
add_subdirectory(Modules/HMS_BLE)
add_subdirectory(Modules/ChronoLog)

# Link HMS_BLE + ChronoLog library to app
target_link_libraries(app PRIVATE HMS_BLE ChronoLog)

# Add HMS_BLE include directory
target_include_directories(app PRIVATE Modules/HMS_BLE/include)

target_sources(app PRIVATE src/main.cpp)
