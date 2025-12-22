#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>

#include "HMS_BLE.h"
#include "ChronoLog.h"

#define SERVICE_UUID            "181A"                              // Environment Sensing Service (standard)
#define CHAR_UUID_HUMIDITY      "2A6F"                              // Humidity (%RH)
#define CHAR_UUID_TEMPERATURE   "2A6E"                              // Temperature (°C)

int16_t     temperature         = 250;                              // 25.0°C (in 0.01°C units for BLE standard)
uint16_t    humidity            = 650;                              // 65.0% (in 0.01% units for BLE standard)


HMS_BLE      *ble = nullptr;
ChronoLogger logger("HMS_BLE");

int32_t random_range(int32_t min, int32_t max) {                    // Helper for random range [min, max]
    return min + (sys_rand32_get() % (max - min + 1));
}

HMS_BLE_ConnectionCallback onConnect = [](bool connected, const uint8_t* deviceMac) {
    logger.info("Device %02X:%02X:%02X:%02X:%02X:%02X %s",
        deviceMac[5], deviceMac[4], deviceMac[3],
        deviceMac[2], deviceMac[1], deviceMac[0],
        connected ? "connected" : "disconnected"
    );
};

HMS_BLE_NotifyCallback onNotify = [](const char *charUUID, bool enabled, const uint8_t* deviceMac) {
    logger.info("Notification %s on %s from %02X:%02X:%02X:%02X:%02X:%02X",
        enabled ? "enabled" : "disabled",
        charUUID,
        deviceMac[5], deviceMac[4], deviceMac[3],
        deviceMac[2], deviceMac[1], deviceMac[0]
    );
};

HMS_BLE_ReadCallback onRead = [](const char *charUUID, uint8_t *data, size_t *length, const uint8_t *deviceMac) {
    logger.info("Read request on %s from %02X:%02X:%02X:%02X:%02X:%02X",
        charUUID,
        deviceMac[5], deviceMac[4], deviceMac[3],
        deviceMac[2], deviceMac[1], deviceMac[0]
    );
    if(strcmp(charUUID, CHAR_UUID_TEMPERATURE) == 0) {
        int16_t* pTemp = (int16_t*)data;
        *pTemp = temperature;
        *length = sizeof(int16_t);
        logger.info("  -> Sending temperature: %d (%.2f°C)", temperature, (double)(temperature / 100.0f));
        
    } else if(strcmp(charUUID, CHAR_UUID_HUMIDITY) == 0) {
        uint16_t* pHumidity = (uint16_t*)data;
        *pHumidity = humidity;
        *length = sizeof(uint16_t);
        logger.info("  -> Sending humidity: %d (%.2f%%)", humidity, (double)(humidity / 100.0f));
    }
};

void setup() {
    logger.info("Initializing HMS_BLE Environmental Sensor nRF52832 Example");
    
    ble = new HMS_BLE("SmartFitTower");

    HMS_BLE_ManufacturerData mData = {
        .manufacturer_id    = {0xFF, 0xFF},
        .data               = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}
    };

    HMS_BLE_Characteristic tempChar = {
        .uuid               = CHAR_UUID_TEMPERATURE,
        .name               = "TempSensor",
        .properties         = HMS_BLE_PROPERTY_READ_NOTIFY
    };

    HMS_BLE_Characteristic humidityChar = {
        .uuid               = CHAR_UUID_HUMIDITY,
        .name               = "HumiditySensor",
        .properties         = HMS_BLE_PROPERTY_READ_NOTIFY
    };


    ble->setReadCallback(onRead);
    ble->setNotifyCallback(onNotify);
    ble->setConnectionCallback(onConnect);
    

    ble->setManufacturerData(mData);
    ble->addCharacteristic(&tempChar);
    ble->addCharacteristic(&humidityChar);

    ble->begin(SERVICE_UUID);

    logger.info("Starting BLE Device...");
}

void loop() {
    static int64_t lastUpdate = 0;
    int64_t now = k_uptime_get();
    
    if(now - lastUpdate > 5000) {
        lastUpdate = now;

        temperature += random_range(-50, 50);                       // Random change: -5.0 to +5.0°C
        humidity += random_range(-30, 30);                          // Random change: -3.0 to +3.0%

        if(temperature < 1000) temperature = 1000;                  // 10.0°C min
        if(temperature > 3500) temperature = 3500;                  // 35.0°C max
        if(humidity < 2000) humidity = 2000;                        // 20.0% min
        if(humidity > 9500) humidity = 9500;                        // 95.0% max

        
        if (ble->isConnected()) {                                   // Notify clients if connected
            ble->sendData(CHAR_UUID_TEMPERATURE, (uint8_t*)&temperature, sizeof(temperature));
            ble->sendData(CHAR_UUID_HUMIDITY, (uint8_t*)&humidity, sizeof(humidity));
        }
    }
}

int main(void) {
    setup();
    while (1) {
        loop();
        k_msleep(10); // Yield
    }
    return 0;
}
