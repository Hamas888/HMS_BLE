cmake_minimum_required(VERSION 3.20.0)

# Set HMS_BLE config file BEFORE find_package(Zephyr)
set(CONF_FILE "prj.conf;Modules/HMS_BLE/HMS_BLE.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(BLE_lib_dev)

# Force C++17 support for HMS_BLE library
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)

# Add HMS_BLE as a subdirectory
add_subdirectory(Modules/HMS_BLE)
add_subdirectory(Modules/ChronoLog)

# Link HMS_BLE library to app
target_link_libraries(app PRIVATE HMS_BLE)

# Add HMS_BLE include directory
target_include_directories(app PRIVATE Modules/HMS_BLE/include)

target_sources(app PRIVATE src/main.cpp)

# Define debug flag for HMS_BLE library and consumers
target_compile_definitions(HMS_BLE 
    PUBLIC HMS_BLE_DEBUG=1
)
