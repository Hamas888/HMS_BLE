  GNU nano 8.4                                                       CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)

project(BLE_lib_dev)

# Force C++17 support for HMS_BLE library
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)

# Add HMS_BLE as a subdirectory
add_subdirectory(Modules/HMS_BLE)
add_subdirectory(Modules/ChronoLog)

# Make ChronoLog headers visible to HMS_BLE during its compilation
target_include_directories(HMS_BLE PUBLIC Modules/ChronoLog/src)

add_executable(app src/main.cpp)

# Link HMS_BLE library to app (auto-pulls BlueZ + libsystemd via pkg-config)
target_link_libraries(app PRIVATE HMS_BLE)

# Add HMS_BLE include directory
target_include_directories(app PRIVATE Modules/HMS_BLE/include)

target_sources(app PRIVATE src/main.cpp)

# Define debug flag for HMS_BLE library and consumers
target_compile_definitions(HMS_BLE
    PUBLIC HMS_BLE_DEBUG=1
)
