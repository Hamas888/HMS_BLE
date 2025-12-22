# HMS_BLE Environmental Sensor - nRF52832 Example

A complete BLE implementation for nRF52832 using the HMS_BLE library and Zephyr RTOS.

## Quick Start

### Prerequisites
- nRF Connect SDK v3.0+ installed
- `west` tool installed
- nRF52DK board or compatible nRF52832 device

### Setup

1. **Clone the repository:**
   ```bash
   git clone <your-repo-url>
   cd BLE_lib_dev
   ```

2. **Fetch dependencies (HMS_BLE and ChronoLog modules):**
   ```bash
   west update
   ```
   
   This will automatically clone:
   - `HMS_BLE` → `Modules/HMS_BLE/`
   - `ChronoLog` → `Modules/ChronoLog/`

3. **Build the project:**
   ```bash
   west build -b nrf52dk/nrf52832
   ```

4. **Flash to device:**
   ```bash
   west flash
   ```

5. **Monitor serial output:**
   ```bash
   # Use nRF Terminal or any serial monitor at 115200 baud
   # You should see HMS_BLE initialization logs
   ```

## Project Structure

```
BLE_lib_dev/
├── CMakeLists.txt          # Main build configuration
├── west.yml                # West manifest (auto-fetches modules)
├── prj.conf                # Zephyr project config
├── src/
│   └── main.cpp            # Application code
├── Modules/                # (Auto-cloned by west update)
│   ├── HMS_BLE/            # HMS_BLE library
│   └── ChronoLog/          # Logging library
└── build/                  # Build artifacts (not tracked)
```

## Features

- **Dynamic GATT Database:** Create BLE services and characteristics at runtime
- **Standard UUIDs:** Uses standardized environmental sensing UUIDs (181A, 2A6E, 2A6F)
- **Cross-Platform:** Same HMS_BLE library works on ESP32 and nRF52
- **Thread-Aware Logging:** ChronoLog shows thread names automatically
- **Manufacturer Data:** Custom manufacturer-specific data in advertisements
- **Notifications & Indications:** Full support for CCC (Client Characteristic Configuration)

## Configuration

Key Kconfig settings in `Modules/HMS_BLE/HMS_BLE.conf`:

- `CONFIG_BT=y` - Enable Bluetooth
- `CONFIG_BT_PERIPHERAL=y` - Peripheral role (server)
- `CONFIG_BT_GATT_DYNAMIC_DB=y` - Dynamic GATT database
- `CONFIG_THREAD_NAME=y` - Thread names for logging
- `CONFIG_HEAP_MEM_POOL_SIZE=4096` - Heap for dynamic allocation

## Troubleshooting

**"undefined reference to `k_malloc`":**
- Ensure `CONFIG_HEAP_MEM_POOL_SIZE` is set in `Modules/HMS_BLE/HMS_BLE.conf`

**"Cannot find HMS_BLE module":**
- Run `west update` to fetch submodules
- Check `Modules/HMS_BLE/` exists with `ls Modules/`

**Device not appearing in nRF Connect:**
- Check serial output for "Advertising started"
- Verify 16-bit service UUID format (e.g., "181A" not full 128-bit)

## API Usage

```cpp
#include "HMS_BLE.h"

// Create BLE instance
HMS_BLE ble("DeviceName");

// Add characteristics
HMS_BLE_Characteristic tempChar = {
    .uuid = "2A6E",
    .name = "Temperature",
    .properties = HMS_BLE_PROPERTY_READ_NOTIFY
};
ble.addCharacteristic(&tempChar);

// Set callbacks
ble.setReadCallback([](const char* uuid, uint8_t* data, size_t* len, const uint8_t* mac) {
    // Handle read request
});

// Start BLE
ble.begin("181A");  // Service UUID

// Send notifications
uint16_t temp = 2500;  // 25.00°C
ble.sendData("2A6E", (uint8_t*)&temp, sizeof(temp));
```

## License

MIT License - See LICENSE file in HMS_BLE and ChronoLog repositories

## Support

For issues with HMS_BLE: https://github.com/Hamas888/HMS_BLE
For issues with ChronoLog: https://github.com/Hamas888/ChronoLog
