# HMS_BLE 📡

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/Version-1.0.0-green.svg)](https://github.com/Hamas888/HMS_BLE)
[![Platform](https://img.shields.io/badge/Platform-ESP32%20|%20nRF52%20|%20STM32WB%20|%20RPi%20|%20Jetson-orange.svg)](https://github.com/Hamas888/HMS_BLE)

A **cross-platform Bluetooth Low Energy (BLE) library** for embedded systems and single-board computers. HMS_BLE provides a unified, easy-to-use API for BLE communication across different hardware platforms including ESP32, nRF52, STM32WB, Raspberry Pi, and NVIDIA Jetson, abstracting away platform-specific complexities.

## 📑 Table of Contents

- [✨ Features](#-features)
- [🎯 Supported Platforms](#-supported-platforms)
- [📦 Installation](#-installation)
  - [PlatformIO (Arduino & ESP-IDF)](#platformio-arduino--esp-idf)
  - [nRF Connect SDK (Zephyr)](#nrf-connect-sdk-zephyr)
  - [STM32Cube Project](#stm32cube-project)
  - [Raspberry Pi & Jetson (Linux SBC)](#raspberry-pi--jetson-linux-sbc)
- [🚀 Quick Start](#-quick-start)
  - [Basic BLE Peripheral](#basic-ble-peripheral)
  - [Environmental Sensor Example](#environmental-sensor-example)
  - [Multiple Characteristics](#multiple-characteristics)
- [📋 BLE Output Examples](#-ble-output-examples)
- [⚙️ Configuration](#️-configuration)
- [🛠️ Platform-Specific Requirements](#️-platform-specific-requirements)
- [🔧 Troubleshooting](#-troubleshooting)
- [📁 Repository Structure](#-repository-structure)
- [🛣️ Roadmap & Upcoming Features](#️-roadmap--upcoming-features)
- [💖 Support & Motivation](#-support--motivation)
- [🤝 Contributing](#-contributing)
- [📄 License](#-license)
- [👨‍💻 Author](#-author)
- [⭐ Show Your Support](#-show-your-support)

## ✨ Features

- **🎯 Automatic Platform Detection**: Seamlessly adapts to ESP32 (Arduino/ESP-IDF), nRF52 (Zephyr), STM32WB, and Linux-based SBCs
- **📡 Unified BLE API**: Single, consistent interface across all platforms
- **🏗️ Multi-Service Support**: Organize your device into multiple GATT services for complex applications
- **🔌 Easy Characteristic Management**: Simple functions to add, update, and manage BLE characteristics
- **📊 Multiple Characteristics**: Support for multiple services, each with multiple characteristics (configurable)
- **🔔 Notifications & Indications**: Built-in support for BLE notifications and indications
- **👥 Multi-Client Support**: Handle up to 4 simultaneous BLE connections (configurable)
- **🧵 Thread-Safe Operation**: Safe concurrent access from multiple tasks/threads
- **📝 Flexible Data Types**: Support for various data types (uint8, uint16, uint32, float, strings)
- **🔍 Debug Support**: Optional integration with [ChronoLog](https://github.com/Hamas888/ChronoLog) for detailed logging
- **💾 Memory Efficient**: Optimized for resource-constrained embedded systems
- **🚀 Zero-Copy Data Transfer**: Efficient data handling for high-frequency updates
- **⚙️ Highly Configurable**: Compile-time configuration for buffer sizes, max clients, and features
- **📦 Ready-to-Use Examples**: Complete examples for environmental sensors and custom BLE services

## 🎯 Supported Platforms

| Platform | Framework | Status | BLE Stack | Notes |
|----------|-----------|--------|-----------|-------|
| **ESP32** | Arduino | ✅ Supported | NimBLE | ESP32, ESP32-C3, ESP32-S3 |
| **ESP32** | ESP-IDF | 🔄 Coming Soon | NimBLE/Bluedroid | Full ESP-IDF support planned |
| **nRF52** | nRF Connect SDK (Zephyr) | ✅ Supported | Zephyr BLE | nRF52832, nRF52840 |
| **STM32WB** | STM32Cube HAL | 🔄 Coming Soon | STM32WB BLE Stack | STM32WB55, STM32WB35 planned |
| **Raspberry Pi** | Linux | 🔄 Coming Soon | BlueZ | All RPi models with BLE |
| **NVIDIA Jetson** | Linux | 🔄 Coming Soon | BlueZ | Jetson Nano, Xavier, Orin |

### Platform Status Legend
- ✅ **Supported**: Fully implemented and tested
- 🔄 **Coming Soon**: Planned for upcoming releases
- 🎯 **In Development**: Actively being developed

## 📦 Installation

### PlatformIO (Arduino & ESP-IDF)

#### Method 1: Add to platformio.ini
Add HMS_BLE to the `lib_deps` section in your `platformio.ini` file:

```ini
[env:your_board]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino

lib_deps = 
    HMS_BLE
    ChronoLog  # Optional: for debug logging
```

#### Method 2: Manual Installation
Clone the repository into your project's `lib` folder:
```bash
cd lib
git clone https://github.com/Hamas888/HMS_BLE.git
```

### nRF Connect SDK (Zephyr)

1. Create a `modules` folder in your project root
2. Clone the repository:
   ```bash
   cd modules
   git clone https://github.com/Hamas888/HMS_BLE.git
   ```
3. Add to your root `CMakeLists.txt`:
   ```cmake
   add_subdirectory(modules/HMS_BLE)
   target_link_libraries(app PRIVATE HMS_BLE)
   ```

4. Enable Bluetooth in your `prj.conf`:
   ```ini
   CONFIG_BT=y
   CONFIG_BT_PERIPHERAL=y
   CONFIG_BT_DEVICE_NAME="HMS_BLE_Device"
   ```

### STM32Cube Project

**Note**: STM32WB support is coming soon! The following will be the installation process:

1. Clone the repository in your project root:
   ```bash
   git clone https://github.com/Hamas888/HMS_BLE.git
   ```
2. Add to your `CMakeLists.txt` after `add_executable()`:
   ```cmake
   add_executable(${PROJECT_NAME} ${SOURCES} ${LINKER_SCRIPT})
   
   # Add HMS_BLE
   add_subdirectory(HMS_BLE)
   target_link_libraries(${PROJECT_NAME} HMS_BLE)
   ```

3. Enable BLE in STM32CubeMX:
   - Enable STM32WB Bluetooth Low Energy stack
   - Configure BLE middleware parameters
   - Generate code

### Raspberry Pi & Jetson (Linux SBC)

**Note**: Linux SBC support is coming soon! The following will be the installation process:

1. Install BlueZ development libraries:
   ```bash
   sudo apt-get update
   sudo apt-get install libbluetooth-dev bluez
   ```

2. Clone HMS_BLE to your project:
   ```bash
   git clone https://github.com/Hamas888/HMS_BLE.git
   ```

3. Add to your `CMakeLists.txt`:
   ```cmake
   add_subdirectory(HMS_BLE)
   target_link_libraries(your_target_name HMS_BLE bluetooth)
   ```

## 🚀 Quick Start

### Basic BLE Peripheral (Single Service)

```cpp
#include "HMS_BLE.h"

// Instantiate with device name
HMS_BLE ble("HMS_Device");

void setup() {
    Serial.begin(115200);
    
    // Define a characteristic
    HMS_BLE_Characteristic myChar = {
        .uuid = "87654321-4321-4321-4321-cba987654321",
        .name = "MyChar",
        .properties = HMS_BLE_PROPERTY_READ_WRITE_NOTIFY
    };
    
    // Add characteristic (auto-creates default service on begin)
    ble.addCharacteristic(&myChar);
    
    // Initialize BLE with a specific service UUID
    HMS_BLE_Status status = ble.begin("12345678-1234-1234-1234-123456789abc");
    if (status != HMS_BLE_STATUS_SUCCESS) {
        Serial.println("BLE initialization failed!");
        return;
    }
    
    Serial.println("BLE peripheral started!");
}

void loop() {
    // Update characteristic value every 2 seconds
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate > 2000) {
        uint32_t uptime = millis();
        ble.sendData("87654321-4321-4321-4321-cba987654321", (uint8_t*)&uptime, sizeof(uptime));
        lastUpdate = millis();
    }
}
```

### Advanced Multi-Service Example

```cpp
#include "HMS_BLE.h"

HMS_BLE ble("MultiService_Device");

void setup() {
    Serial.begin(115200);

    // 1. Define Service 1: Environmental
    HMS_BLE_Service envService = {
        .uuid = "0000181A-0000-1000-8000-00805F9B34FB",
        .name = "Environment"
    };
    ble.addService(&envService);

    // Add characteristics to Environment service
    HMS_BLE_Characteristic tempChar = {"00002A6E-0000-1000-8000-00805F9B34FB", "Temp", HMS_BLE_PROPERTY_READ_NOTIFY};
    ble.addCharacteristicToService(envService.uuid.c_str(), &tempChar);

    // 2. Define Service 2: Device Info
    HMS_BLE_Service infoService = {"0000180A-0000-1000-8000-00805F9B34FB", "DeviceInfo"};
    ble.addService(&infoService);

    HMS_BLE_Characteristic modelChar = {"00002A24-0000-1000-8000-00805F9B34FB", "Model", HMS_BLE_PROPERTY_READ};
    ble.addCharacteristicToService(infoService.uuid.c_str(), &modelChar);

    // 3. Initialize and Start
    ble.begin(); 
    
    // Optional: Explicitly set which services to advertise
    const char* advServices[] = { envService.uuid.c_str() };
    ble.setAdvertisedServices(advServices, 1);
}

void loop() {
    float temp = 24.5f;
    // Send data to specific service/characteristic
    ble.sendDataToService("0000181A-0000-1000-8000-00805F9B34FB", 
                         "00002A6E-0000-1000-8000-00805F9B34FB", 
                         (uint8_t*)&temp, sizeof(temp));
    delay(5000);
}
```

## 📋 BLE Output Examples

### ESP32 Arduino (NimBLE)
```
BLE Device initialized: Environmental_Sensor
Service UUID: 0000181A-0000-1000-8000-00805F9B34FB
  └─ Characteristic: 00002A6E (Temperature) - READ, NOTIFY
  └─ Characteristic: 00002A6F (Humidity) - READ, NOTIFY
Advertising started
Client connected: 00:11:22:33:44:55
Temp: 23.4°C, Humidity: 55.2%
Notification sent to characteristic 0
```

### nRF52 (Zephyr BLE)
```
[HMS_BLE] Bluetooth initialized
[HMS_BLE] Device name: HMS_nRF52_Sensor
[HMS_BLE] Advertising started
[HMS_BLE] Connection established
[HMS_BLE] Characteristic updated: index=0, value=25.6
```

### Debug Output (with ChronoLog)
```
14:32:15 | HMS_BLE        | INFO     | BLETask          | BLE stack initialized
14:32:15 | HMS_BLE        | DEBUG    | BLETask          | Adding characteristic: Temperature (UUID: 2A6E)
14:32:16 | HMS_BLE        | INFO     | BLETask          | Advertising started
14:32:20 | HMS_BLE        | INFO     | BLETask          | Client connected: 1 active connections
14:32:25 | HMS_BLE        | DEBUG    | SensorTask       | Updating temperature: 23.4°C
14:32:25 | HMS_BLE        | DEBUG    | BLETask          | Notification sent successfully
```

## ⚙️ Configuration

### Compile-Time Configuration

Configure HMS_BLE by defining these macros before including the header:

```cpp
// === Core BLE Configuration ===
#define HMS_BLE_MAX_SERVICES 4                  // Max number of services (default: 4)
#define HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE 8 // Max characteristics per service (default: 8)
#define HMS_BLE_MAX_DATA_LENGTH 32              // Max data length for characteristics (default: 32)
#define HMS_BLE_MAX_CLIENTS 4                   // Max simultaneous connections (default: 4)

// === Background Processing ===
#define HMS_BLE_BACKGROUND_PROCESS_PRIORITY 5   // Task priority for BLE processing
#define HMS_BLE_BACKGROUND_PROCESS_STACK_SIZE 2048  // Stack size for BLE task

// === Debug Logging (Optional) ===
#define HMS_BLE_DEBUG 1                         // Enable debug logging (requires ChronoLog)
#define HMS_BLE_LOG_LEVEL CHRONOLOG_LEVEL_DEBUG // Set log verbosity

#include "HMS_BLE.h"
```

### Example Configurations

#### Minimal Configuration (Memory Constrained)
```cpp
#define HMS_BLE_MAX_SERVICES 1                  // Single service only
#define HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE 4 
#define HMS_BLE_MAX_DATA_LENGTH 20              // Smaller data packets
#define HMS_BLE_MAX_CLIENTS 1                   // Single client only
```

#### Full-Featured Configuration
```cpp
#define HMS_BLE_MAX_SERVICES 8                  // More services
#define HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE 10
#define HMS_BLE_MAX_DATA_LENGTH 64              // Larger data packets
#define HMS_BLE_MAX_CLIENTS 8                   // Multi-client support
```

### BLE Property Flags

```cpp
HMS_BLE_PROPERTY_READ           // Read-only characteristic
HMS_BLE_PROPERTY_WRITE          // Write-only characteristic
HMS_BLE_PROPERTY_NOTIFY         // Notify-only characteristic
HMS_BLE_PROPERTY_INDICATE       // Indicate-only characteristic
HMS_BLE_PROPERTY_BROADCAST      // Broadcast characteristic
HMS_BLE_PROPERTY_READ_WRITE     // Read and write
HMS_BLE_PROPERTY_READ_NOTIFY    // Read with notifications
HMS_BLE_PROPERTY_WRITE_NOTIFY   // Write with notifications
```

### Callbacks API

HMS_BLE uses functional callbacks for handling BLE events. All callbacks now include the `serviceUUID` to support multi-service architectures.

```cpp
// 1. Connection Callback
ble.setConnectionCallback([](bool connected, const uint8_t* mac) {
    Serial.printf("Client %s: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                 connected ? "Connected" : "Disconnected",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
});

// 2. Write Callback (Data received from client)
ble.setWriteCallback([](const char* svcUUID, const char* charUUID, const uint8_t* data, size_t len, const uint8_t* mac) {
    Serial.printf("Write on %s/%s: %d bytes received\n", svcUUID, charUUID, len);
});

// 3. Read Callback (Client requested data)
ble.setReadCallback([](const char* svcUUID, const char* charUUID, uint8_t* data, size_t* len, const uint8_t* mac) {
    // Fill 'data' and set '*len'
    const char* response = "HelloClient";
    memcpy(data, response, strlen(response));
    *len = strlen(response);
});

// 4. Notify Callback (Client subscribed/unsubscribed)
ble.setNotifyCallback([](const char* svcUUID, const char* charUUID, bool enabled, const uint8_t* mac) {
    Serial.printf("Notifications %s on %s/%s\n", enabled ? "ENABLED" : "DISABLED", svcUUID, charUUID);
});
```

## 🛠️ Platform-Specific Requirements

### ESP32 (Arduino Framework)
- **Required**: ESP32 board support in Arduino IDE or PlatformIO
- **BLE Stack**: NimBLE (automatically included)
- **Minimum ESP-IDF**: v4.4+
- **Tested Boards**: ESP32, ESP32-C3, ESP32-S3

**Setup:**
```cpp
#include "HMS_BLE.h"

HMS_BLE ble;

void setup() {
    ble.init("ESP32_BLE_Device");
    ble.startAdvertising();
}
```

### nRF52 (nRF Connect SDK / Zephyr)
- **Required**: nRF Connect SDK v2.0+
- **BLE Stack**: Zephyr Bluetooth
- **Tested Chips**: nRF52832, nRF52840
- **Required Kconfig**: See `prj.conf` examples

**Required Configuration (`prj.conf`):**
```ini
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="HMS_nRF52"
CONFIG_BT_MAX_CONN=4
```

### STM32WB (Coming Soon)
- **Required**: STM32CubeWB v1.15+
- **BLE Stack**: STM32WB Bluetooth LE Stack
- **Supported**: STM32WB55, STM32WB35
- **IDE**: STM32CubeIDE or CMake

**Planned Setup:**
```cpp
#include "HMS_BLE.h"

HMS_BLE ble;

int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    ble.init("STM32WB_Device");
    ble.startAdvertising();
    
    while (1) {
        // Main loop
    }
}
```

### Raspberry Pi & Jetson (Linux SBC - Coming Soon)
- **Required**: BlueZ 5.50+
- **Permissions**: Requires CAP_NET_ADMIN capability or root
- **Tested**: Raspberry Pi 3/4/5, Jetson Nano/Xavier/Orin

**Planned Setup:**
```cpp
#include "HMS_BLE.h"

HMS_BLE ble;

int main() {
    ble.init("RaspberryPi_BLE");
    ble.startAdvertising();
    
    while (true) {
        // Update sensors
        sleep(1);
    }
    
    return 0;
}
```

## 🔧 Troubleshooting

### Common Issues & Solutions

**BLE initialization fails**
- Ensure Bluetooth is enabled in your platform configuration
- Check that your device supports BLE (not all ESP32 variants do)
- Verify sufficient memory is available for BLE stack

**Cannot add more characteristics**
- Increase `HMS_BLE_MAX_CHARACTERISTICS` limit
- Verify you haven't exceeded the BLE specification limits

**Notifications not received**
- Ensure characteristic has `NOTIFY` or `INDICATE` property
- Check that client has subscribed to notifications
- Verify client is still connected

**Multiple clients not connecting**
- Increase `HMS_BLE_MAX_CLIENTS` configuration
- Check platform-specific connection limits
- Verify advertising is still active

**Debug logging not working**
- Install [ChronoLog](https://github.com/Hamas888/ChronoLog) library
- Define `HMS_BLE_DEBUG 1` before including HMS_BLE.h
- Ensure `bleLogger` is properly initialized

**nRF52: Build errors**
- Enable required Kconfig options in `prj.conf`
- Check Zephyr SDK version compatibility
- Verify BLE is enabled: `CONFIG_BT=y`

## 📁 Repository Structure

```
HMS_BLE/
├── include/
│   └── HMS_BLE.h                       # Main library header (public API)
├── src/
│   ├── HMS_BLE.cpp                     # Core implementation
│   ├── HMS_BLE.h                       # Internal header
│   ├── ESP32/
│   │   └── HMS_BLE_ARDUINO_ESP32.cpp   # ESP32 Arduino implementation
│   ├── nRF/
│   │   └── HMS_BLE_ZEPHYR_nRF.cpp      # nRF52 Zephyr implementation
│   └── Template/
│       └── HMS_BLE_PLATFORM_CONTROLLER_TEMPLATE.cpp  # Platform template
├── examples/
│   ├── PlatformIO/
│   │   └── Arduino/
│   │       └── ESP32-C3_Enviornmental_Sensor/  # ESP32-C3 example
│   └── nRF_Connect/
│       └── nRF52/
│           └── nRF52-832_Enviornmental_Sensor/  # nRF52 example
├── CMakeLists.txt                      # Multi-platform build configuration
├── library.json                        # PlatformIO library manifest
├── HMS_BLE.conf                        # Default Kconfig for Zephyr
├── LICENSE                             # MIT License
└── README.md                           # This file
```

## 🛣️ Roadmap & Upcoming Features

HMS_BLE is actively developed with exciting features planned for upcoming releases:

### ✅ Currently Supported
- **ESP32 (Arduino)**: Full support for ESP32 family with NimBLE
- **nRF52 (Zephyr)**: Complete implementation for nRF Connect SDK
- **Multi-Service Architecture**: Organize characteristics into up to 4 services
- **Multi-Characteristic**: Support for multiple characteristics per service (up to 8)
- **Notifications & Indications**: Full BLE notification and indication support
- **Manufacturer Data**: Support for setting custom manufacturer data in advertising packets

### 🔜 Next Major Platforms (Q1-Q2 2026)
- **🔷 STM32WB Series**: Full support for STM32WB55/WB35 with STM32Cube HAL
  - STM32WB BLE stack integration
  - FreeRTOS and bare-metal support
  - Low-power modes and power optimization
  
- **🥧 Raspberry Pi (Linux)**: Complete BlueZ integration for all RPi models
  - BlueZ D-Bus API implementation
  - Support for RPi 3, 4, 5, Zero W/2W
  - GATT server and client modes
  
- **🟢 NVIDIA Jetson**: Full BLE support for Jetson platform
  - Jetson Nano, Xavier NX, Orin support
  - BlueZ integration optimized for Jetson
  - High-performance BLE for edge AI applications

### 🎯 Advanced Features (Q2-Q3 2026)
- **ESP-IDF Native Support**: Pure ESP-IDF implementation (non-Arduino)
- **BLE Central Mode**: Act as BLE central/client to connect to other devices
- **BLE Mesh**: Mesh networking capabilities for supported platforms
- **Long Range (Coded PHY)**: Extended range BLE communication
- **Security Features**: Bonding, pairing, and encryption support

### 🚀 Future Enhancements
- **📊 BLE Throughput Optimization**: Enhanced data transfer rates
- **🔋 Power Management**: Advanced low-power modes and sleep strategies
- **🛡️ Security**: Enhanced encryption and authentication
- **📱 Mobile SDKs**: React Native and Flutter companion libraries
- **🌐 Web Bluetooth**: Browser-based BLE interaction examples

**Want to influence the roadmap?** Share your platform needs through [GitHub Issues](https://github.com/Hamas888/HMS_BLE/issues) or reach out via [Patreon](https://patreon.com/hamas888)!

## 💖 Support & Motivation

HMS_BLE is developed with passion to provide seamless BLE communication across embedded platforms. Your support helps drive continued development of this and other embedded libraries.

### 🎯 Support the Project
- **⭐ Star this repository** - Helps others discover HMS_BLE
- **🔄 Patreon** - [Support ongoing development](https://patreon.com/hamas888)

### 💬 Get Help & Discuss
- **🐛 Issues**: Create a [GitHub Issue](https://github.com/Hamas888/HMS_BLE/issues) for bugs or feature requests
- **💭 Direct Help**: Reach out on [Patreon](https://patreon.com/hamas888) for direct support
- **📧 Email**: Contact me at hamasaeed@gmail.com

Your support enables:
- 🔧 Maintenance and platform updates
- 📡 New platform support (STM32WB, RPi, Jetson)
- 🚀 Advanced BLE features
- 📚 Documentation and examples

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request. For major changes, please open an issue first to discuss what you would like to change.

**Contribution Ideas:**
- New platform implementations
- Additional examples and tutorials
- Performance optimizations
- Documentation improvements
- Bug fixes and testing

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

This permissive license allows for both commercial and non-commercial use, making HMS_BLE suitable for any embedded BLE project.

## 👨‍💻 Author

**Hamas Saeed**
- GitHub: [@Hamas888](https://github.com/Hamas888)
- Email: hamasaeed@gmail.com
- Patreon: [hamas888](https://patreon.com/hamas888)

## ⭐ Show Your Support

If HMS_BLE helped you build awesome BLE applications, please give it a ⭐️ on [GitHub](https://github.com/Hamas888/HMS_BLE)!

---

**Related Projects:**
- [ChronoLog](https://github.com/Hamas888/ChronoLog) - Cross-platform logging library for embedded systems