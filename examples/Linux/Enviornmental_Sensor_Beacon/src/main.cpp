#include <cstdint>
#include <cstring>
#include <cmath>
#include <random>
#include <chrono>
#include <thread>
#include "HMS_BLE.h"
#include "ChronoLog.h"

/* ===================================================================================================
 *  HMS_BLE Beacon Demo — Environmental Sensor Beacon (Manufacturer-Specific)
 *
 *  Demonstrates:
 *    • Beacon (Broadcaster) mode — non-connectable advertising
 *    • Built-in beacon format builders (iBeacon, Eddystone, Manufacturer)
 *    • Custom manufacturer beacon with live sensor data in AD payload
 *    • API error handling
 *
 *  Beacon format: Manufacturer-specific (Company ID 0xFFFF)
 *    AD payload encodes: data format version (1B) + temperature in °C (2B, LE)
 *                        + humidity in %RH (2B, LE) + battery % (1B) = 6 bytes
 *
 *  Scan: Use nRF Connect / nRF Toolbox / any BLE scanner and look for
 *        manufacturer data on Company ID 0xFFFF.
 * ===================================================================================================*/

#ifndef CLAMP
  #define CLAMP(val, lo, hi)  (((val) < (lo)) ? (lo) : (((val) > (hi)) ? (hi) : (val)))
#endif

// ---- Simulated sensor state ----
static int16_t   temperature     = 2500;               // 25.00°C
static uint16_t  humidity        = 6500;               // 65.00% RH
static uint8_t   batteryLevel    = 85;                 // 85%

static HMS_BLE       *ble        = nullptr;
static ChronoLogger   logger("EnvBeacon");

static std::mt19937 rng(std::random_device{}());

static int32_t randRange(int32_t min, int32_t max) {
    std::uniform_int_distribution<int32_t> dist(min, max);
    return dist(rng);
}

// ==================================== Main ====================================

int main(void) {
    logger.info("=== HMS_BLE Beacon Demo (Linux/BlueZ) ===");

    // Create in BEACON mode
    ble = new HMS_BLE("HMS-BLE-Beacon", HMS_BLE_MODE_BEACON);

    // Build manufacturer AD payload with sensor data.
    // Format: [version(1)] [temperature_LE(2)] [humidity_LE(2)] [battery(1)] = 6 bytes
    uint8_t adBuf[HMS_BLE_MAX_AD_DATA];
    uint8_t mfgPayload[6];

    mfgPayload[0] = 0x01;                                                           // Data format version
    memcpy(&mfgPayload[1], &temperature, 2);                                        // Temperature (little-endian)
    memcpy(&mfgPayload[3], &humidity,    2);                                        // Humidity (little-endian)
    mfgPayload[5] = batteryLevel;                                                    // Battery %

    size_t adLen = HMS_BLE::buildManufacturerAD(adBuf, sizeof(adBuf),
                                                 0xFFFF,                            // Company ID (test)
                                                 mfgPayload, sizeof(mfgPayload));

    if (adLen == 0) {
        logger.error("Failed to build manufacturer AD");
        return -1;
    }

    HMS_BLE_Status st = ble->setBeaconData(adBuf, adLen);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("setBeaconData() failed: %d", st);
        return -1;
    }

    st = ble->begin();
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("BLE begin() failed: %d", st);
        return -1;
    }

    logger.info("Beacon active — device name: HMS-BLE-Beacon");
    logger.info("Company ID: 0xFFFF, Sensor data format v%d", mfgPayload[0]);
    logger.info("Scan for manufacturer data on phone");

    // ==================================== Main loop ====================================
    while (1) {
        static auto lastUpdate = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count();
        if (elapsed >= 5000) {
            lastUpdate = now;

            // Simulate sensor drift (same as Peripheral demo)
            temperature  += randRange(-100, 100);
            humidity     += randRange(-50, 50);
            batteryLevel  = (batteryLevel > 1) ? batteryLevel - 1 : 100;

            temperature   = CLAMP(temperature, 1500, 3500);
            humidity      = CLAMP(humidity,    3000, 9000);

            // Rebuild AD with fresh sensor data
            mfgPayload[0] = 0x01;
            memcpy(&mfgPayload[1], &temperature, 2);
            memcpy(&mfgPayload[3], &humidity,    2);
            mfgPayload[5] = batteryLevel;

            adLen = HMS_BLE::buildManufacturerAD(adBuf, sizeof(adBuf),
                                                 0xFFFF, mfgPayload, sizeof(mfgPayload));

            // Update beacon data (restarts advertising internally)
            if (adLen > 0) {
                ble->setBeaconData(adBuf, adLen);
                logger.info("Beacon updated — Temp: %d.%02d°C, Humidity: %d.%02d%%, Battery: %d%%",
                            temperature / 100, abs(temperature % 100),
                            humidity / 100, humidity % 100,
                            batteryLevel);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
