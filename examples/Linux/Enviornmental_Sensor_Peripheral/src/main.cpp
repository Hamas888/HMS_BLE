#include <cstdint>
#include <cstring>
#include <cmath>
#include <random>
#include <chrono>
#include <thread>
#include "HMS_BLE.h"
#include "ChronoLog.h"

/* ===================================================================================================
 *  HMS_BLE Multi-Service Demo — Environmental Sensor with Battery & Configuration
 *
 *  Demonstrates:
 *    • Multiple services (16-bit & 128-bit UUIDs)
 *    • Read / Notify / Write callbacks
 *    • Notification subscription tracking (only notify when client subscribed)
 *    • Client-configurable notification interval via write
 *    • Manufacturer data & selective service advertising
 *    • API error handling
 *
 *  Services:
 *    0x181A  Environmental Sensing    →  Temperature (0x2A6E), Humidity (0x2A6F)
 *    0x180F  Battery Service          →  Battery Level  (0x2A19)
 *    custom  Configuration Service    →  Notify Interval (R/W, uint16 seconds)
 * ===================================================================================================*/

#define ENV_SVC       "181A"
#define TEMP_CHAR     "2A6E"
#define HUMID_CHAR    "2A6F"

#define BATT_SVC      "180F"
#define BATT_LEVEL    "2A19"

// 128-bit custom service — demonstrates 128-bit UUID support
#define CFG_SVC       "00000001-0000-1000-8000-00805F9B34FB"
#define CFG_INTERVAL  "00000002-0000-1000-8000-00805F9B34FB"

#ifndef CLAMP
  #define CLAMP(val, lo, hi)  (((val) < (lo)) ? (lo) : (((val) > (hi)) ? (hi) : (val)))
#endif

// ---- Simulated sensor state ----
static int16_t   temperature     = 2500;               // 25.00°C
static uint16_t  humidity        = 6500;               // 65.00% RH
static uint8_t   batteryLevel    = 85;                 // 85%

// ---- Subscription tracking (client must enable notify per char) ----
static bool      tempSub         = false;
static bool      humidSub        = false;
static bool      battSub         = false;

// ---- Configurable via BLE Write ----
static uint16_t  notifyInterval  = 5;                  // seconds (1–60)

static HMS_BLE       *ble        = nullptr;
static ChronoLogger   logger("EnvSensor");

static std::mt19937 rng(std::random_device{}());

static int32_t randRange(int32_t min, int32_t max) {
    std::uniform_int_distribution<int32_t> dist(min, max);
    return dist(rng);
}

// ==================================== Callbacks ====================================

static void onConnect(bool connected, const uint8_t *mac) {
    if (connected) {
        logger.info(
            "Device %02X:%02X:%02X:%02X:%02X:%02X connected",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]
        );
    } else {
        logger.info(
            "Device %02X:%02X:%02X:%02X:%02X:%02X disconnected",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]
        );
        // Reset subscription flags — avoid sending to stale subscriber
        tempSub = humidSub = battSub = false;
    }
}

static void onNotify(const char *svc, const char *chr, bool enabled, const uint8_t *mac) {
    logger.info("Notification %s on %s/%s", enabled ? "enabled" : "disabled", svc, chr);
    if      (strcmp(chr, TEMP_CHAR)  == 0)  tempSub  = enabled;
    else if (strcmp(chr, HUMID_CHAR) == 0)  humidSub = enabled;
    else if (strcmp(chr, BATT_LEVEL) == 0)  battSub  = enabled;
}

static void onRead(const char *svc, const char *chr, uint8_t *data, size_t *len, const uint8_t *mac) {
    logger.info("Read request on %s/%s — preparing response", svc, chr);

    if (strcmp(chr, TEMP_CHAR) == 0) {
        memcpy(data, &temperature, sizeof(temperature));
        *len = sizeof(temperature);
        logger.info("  → Temperature: %d (%d.%02d°C)", temperature, temperature / 100, abs(temperature % 100));

    } else if (strcmp(chr, HUMID_CHAR) == 0) {
        memcpy(data, &humidity, sizeof(humidity));
        *len = sizeof(humidity);
        logger.info("  → Humidity: %d (%d.%02d%%)", humidity, humidity / 100, humidity % 100);

    } else if (strcmp(chr, BATT_LEVEL) == 0) {
        data[0] = batteryLevel;
        *len = 1;
        logger.info("  → Battery: %d%%", batteryLevel);

    } else if (strcmp(chr, CFG_INTERVAL) == 0) {
        memcpy(data, &notifyInterval, sizeof(notifyInterval));
        *len = sizeof(notifyInterval);
        logger.info("  → Interval: %u s", notifyInterval);
    }
}

static void onWrite(const char *svc, const char *chr, const uint8_t *data, size_t len, const uint8_t *mac) {
    logger.info("Write request on %s/%s (%u bytes)", svc, chr, len);

    if (strcmp(chr, CFG_INTERVAL) == 0 && len >= sizeof(uint16_t)) {
        notifyInterval = data[0] | ((uint16_t)data[1] << 8);            // BLE → LE
        if (notifyInterval < 1)  notifyInterval = 1;
        if (notifyInterval > 60) notifyInterval = 60;
        logger.info("  → Interval set to %u seconds", notifyInterval);
    }
}

// ==================================== Main ====================================

int main(void) {
    logger.info("=== HMS_BLE Multi-Service Demo (Linux/BlueZ) ===");

    ble = new HMS_BLE("HMS-BLE-Peripheral");

    // ---- 1) Manufacturer Data (appears in scan response) ----
    HMS_BLE_ManufacturerData mfg = {
        .manufacturer_id    = {0xFF, 0xFF},
        .data               = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
    };
    ble->setManufacturerData(mfg);

    // ---- 2) Register callbacks ----
    ble->setConnectionCallback(onConnect);
    ble->setNotifyCallback(onNotify);
    ble->setReadCallback(onRead);
    ble->setWriteCallback(onWrite);

    // ---- 3) Build services ----
    HMS_BLE_Service envSvc = {
        .uuid = ENV_SVC,
        .name = "Environment" };
    HMS_BLE_Characteristic tempCh = {
        .uuid = TEMP_CHAR,
        .name = "Temperature",
        .properties = HMS_BLE_PROPERTY_READ_NOTIFY
    };
    HMS_BLE_Characteristic humCh = {
        .uuid = HUMID_CHAR,
        .name = "Humidity",
        .properties = HMS_BLE_PROPERTY_READ_NOTIFY
    };

    HMS_BLE_Service battSvc = {
        .uuid = BATT_SVC,
        .name = "Battery"
    };
    HMS_BLE_Characteristic battCh = {
        .uuid = BATT_LEVEL,
        .name = "Battery Level",
        .properties = HMS_BLE_PROPERTY_READ_NOTIFY
    };

    HMS_BLE_Service cfgSvc = {
        .uuid = CFG_SVC,
        .name = "Configuration"
    };
    HMS_BLE_Characteristic cfgCh = {
        .uuid = CFG_INTERVAL,
        .name = "Notify Interval",
        .properties = (HMS_BLE_CharacteristicProperty)
            (HMS_BLE_PROPERTY_READ | HMS_BLE_PROPERTY_WRITE)
    };

    // ---- 4) Register services & characteristics ----
    HMS_BLE_Status st;

    st = ble->addService(&envSvc);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addService(Environment) failed: %d", st); return -1;
    }

    st = ble->addCharacteristicToService(ENV_SVC, &tempCh);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addChar(Temperature) failed: %d", st); return -1;
    }

    st = ble->addCharacteristicToService(ENV_SVC, &humCh);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addChar(Humidity) failed: %d", st); return -1;
    }

    st = ble->addService(&battSvc);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addService(Battery) failed: %d", st); return -1;
    }

    st = ble->addCharacteristicToService(BATT_SVC, &battCh);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addChar(Battery Level) failed: %d", st); return -1;
    }

    st = ble->addService(&cfgSvc);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addService(Config) failed: %d", st); return -1;
    }

    st = ble->addCharacteristicToService(CFG_SVC, &cfgCh);
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("addChar(Interval) failed: %d", st); return -1;
    }

    // ---- 5) Selective advertising — Environment + Battery visible, Config hidden ----
    const char *advSvcs[] = { ENV_SVC, BATT_SVC };
    ble->setAdvertisedServices(advSvcs, 2);

    // ---- 6) Start BLE ----
    st = ble->begin();
    if (st != HMS_BLE_STATUS_SUCCESS) {
        logger.error("BLE begin() failed: %d", st);
        return -1;
    }

    logger.info("BLE active — device name: HMS-BLE-Peripheral");
    logger.info("Advertised services: %s, %s", ENV_SVC, BATT_SVC);
    logger.info("Notify interval = %u s (write to %s to change)", notifyInterval, CFG_INTERVAL);

    // ==================================== Main loop ====================================
    while (1) {
        static auto last = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (elapsed >= (int64_t)notifyInterval * 1000) {
            last = now;

            // Simulate sensor drift
            temperature  += randRange(-100, 100);
            humidity     += randRange(-50, 50);
            batteryLevel  = (batteryLevel > 1) ? batteryLevel - 1 : 100;

            temperature   = CLAMP(temperature, 1500, 3500);
            humidity      = CLAMP(humidity,    3000, 9000);

            // Push notifications only to subscribed characteristics
            if (ble->isConnected()) {
                if (tempSub) {
                    st = ble->sendDataToService(ENV_SVC, TEMP_CHAR, (uint8_t *)&temperature, sizeof(temperature));
                    if (st != HMS_BLE_STATUS_SUCCESS) logger.warn("sendData(Temp) failed: %d", st);
                }
                if (humidSub) {
                    st = ble->sendDataToService(ENV_SVC, HUMID_CHAR, (uint8_t *)&humidity, sizeof(humidity));
                    if (st != HMS_BLE_STATUS_SUCCESS) logger.warn("sendData(Humidity) failed: %d", st);
                }
                if (battSub) {
                    st = ble->sendDataToService(BATT_SVC, BATT_LEVEL, (uint8_t *)&batteryLevel, 1);
                    if (st != HMS_BLE_STATUS_SUCCESS) logger.warn("sendData(Battery) failed: %d", st);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
