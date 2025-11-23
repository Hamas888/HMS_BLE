/*
 ============================================================================================================================================
 * File:        HMS_BLE.h
 * Author:      Hamas Saeed
 * Version:     Rev_1.0.0
 * Date:        Oct 21 2025
 * Brief:       This file provides BLE functionalities for embedded & Desktop systems (Arduino, ESP-IDF, Zephyr, STM32 HAL).
 ============================================================================================================================================
 * License: 
 * MIT License
 * 
 * Copyright (c) 2025 Hamas Saeed
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, 
 * publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do 
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * For any inquiries, contact Hamas Saeed at hamasaeed@gmail.com
 ============================================================================================================================================
 */

#ifndef HMS_BLE_H
#define HMS_BLE_H

/* Platform detection *//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(ARDUINO)
  #include <Arduino.h>
  #if defined(ESP32)
    #include <NimBLEDevice.h>
    #include <freertos/task.h>
    #include <freertos/FreeRTOS.h>
    #define HMS_BLE_ARDUINO_ESP32
  #elif defined(ESP8266)
    // Include ESP8266 BLE libraries here
    #define HMS_BLE_ARDUINO_ESP8266
  #endif
  #define HMS_BLE_PLATFORM_ARDUINO
#elif defined(ESP_PLATFORM)
  // ESP-IDF specific includes
  #define HMS_BLE_PLATFORM_ESP_IDF
#elif defined(__ZEPHYR__)
  // Zephyr specific includes
  #define HMS_BLE_PLATFORM_ZEPHYR
#elif defined(__STM32__)
  // STM32 HAL specific includes
  #define HMS_BLE_PLATFORM_STM32_HAL
#elif defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
  // Desktop specific includes
  #define HMS_BLE_PLATFORM_DESKTOP
#endif // Platform detection

/* Control Knobs *///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef HMS_BLE_DEBUG
  #define HMS_BLE_DEBUG_ENABLED                     1                                                                                               // Set to 1 to enable debug features
#endif

#ifndef HMS_BLE_MAX_DATA_LENGTH
  #define HMS_BLE_MAX_DATA_LENGTH                   32                                                                                              // Maximum data length for BLE characteristics
#endif

  #ifndef HMS_BLE_MAX_CHARACTERISTICS
    #define HMS_BLE_MAX_CHARACTERISTICS             8                                                                                               // Maximum number of characteristics supported
  #endif

#ifndef HMS_BLE_MAX_CLIENTS
  #define HMS_BLE_MAX_CLIENTS                       4                                                                                               // Maximum number of simultaneous BLE clients (increase for multi-client support)
#endif

#ifndef HMS_BLE_BACKGROUND_PROCESS_PRIORITY
  #define HMS_BLE_BACKGROUND_PROCESS_PRIORITY       5                                                                                               // Background process task priority
#endif

#ifndef HMS_BLE_BACKGROUND_PROCESS_STACK_SIZE
  #define HMS_BLE_BACKGROUND_PROCESS_STACK_SIZE     2048                                                                                            // Background process task stack size
#endif

#if HMS_BLE_DEBUG_ENABLED
  #if __has_include("ChronoLog.h")
    #include "ChronoLog.h"
    #ifndef HMS_BLE_LOG_LEVEL
      #define HMS_BLE_LOG_LEVEL CHRONOLOG_LEVEL_DEBUG
    #endif
    extern ChronoLogger *bleLogger;
  #else
    #error "HMS_BLE_DEBUG is enabled but ChronoLog.h is missing. Please include the https://github.com/Hamas888/ChronoLog in your project."
  #endif
#endif

#ifdef HMS_BLE_DEBUG_ENABLED
  #define BLE_LOGGER(level, msg, ...)   \
    do {                                \
      if (bleLogger)                    \
        bleLogger->level(               \
          msg, ##__VA_ARGS__            \
        );                              \
    } while (0);  
#else
    #define BLE_LOGGER(level, msg, ...)    do {} while (0)
#endif

/* Custom types *////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum {
  HMS_BLE_STATUS_SUCCESS              = 0,
  HMS_BLE_STATUS_ERROR_INIT           = -1,
  HMS_BLE_STATUS_ERROR_SEND           = -2,
  HMS_BLE_STATUS_ERROR_START          = -3,
  HMS_BLE_STATUS_ERROR_UNKNOWN        = -4,
  HMS_BLE_STATUS_ERROR_MAX_CHARS      = -5,
  HMS_BLE_STATUS_ERROR_INVALID_CHAR   = -6,
  HMS_BLE_STATUS_ERROR_NOT_CONNECTED  = -7,
} HMS_BLE_Status;

typedef enum {
  #if defined(HMS_BLE_ARDUINO_ESP32)
    HMS_BLE_PROPERTY_READ                 = NIMBLE_PROPERTY::READ,
    HMS_BLE_PROPERTY_WRITE                = NIMBLE_PROPERTY::WRITE,
    HMS_BLE_PROPERTY_NOTIFY               = NIMBLE_PROPERTY::NOTIFY,
    HMS_BLE_PROPERTY_INDICATE             = NIMBLE_PROPERTY::INDICATE,
    HMS_BLE_PROPERTY_BROADCAST            = NIMBLE_PROPERTY::BROADCAST,
    HMS_BLE_PROPERTY_READ_WRITE           = NIMBLE_PROPERTY::READ  | NIMBLE_PROPERTY::WRITE,
    HMS_BLE_PROPERTY_READ_NOTIFY          = NIMBLE_PROPERTY::READ  | NIMBLE_PROPERTY::NOTIFY,
    HMS_BLE_PROPERTY_WRITE_NOTIFY         = NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY,
    HMS_BLE_PROPERTY_READ_WRITE_NOTIFY    = NIMBLE_PROPERTY::READ  | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY,
    HMS_BLE_PROPERTY_READ_WRITE_INDICATE  = NIMBLE_PROPERTY::READ  | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE,
  #endif
} HMS_BLE_CharacteristicProperty;                                                                                                           // Characteristic properties enum

typedef struct {
  std::string uuid;                                                                                                                         // Characteristic UUID (e.g., "12345678-1234-1234-1234-123456789012")
  std::string name;                                                                                                                         // Human-readable characteristic name (visible in BLE apps)
  HMS_BLE_CharacteristicProperty properties;                                                                                                // Characteristic properties (read, write, notify, etc.)
} HMS_BLE_Characteristic;                                                                                                                   // Characteristic definition structure

typedef struct {
  std::array<uint8_t, 2> manufacturer_id;                                                                                                   // Company Identifier Code (0xFFFF for testing)
  std::array<uint8_t, 6> data;                                                                                                              // Manufacturer specific data (up to 6 bytes)
} HMS_BLE_ManufacturerData;

typedef std::function<void(bool connected, const uint8_t* deviceMac)> HMS_BLE_ConnectionCallback;
typedef std::function<void(const char* charUUID, uint8_t* data, size_t* length, const uint8_t* deviceMac)> HMS_BLE_ReadCallback;
typedef std::function<void(const char* charUUID, const uint8_t* data, size_t length, const uint8_t* deviceMac)> HMS_BLE_WriteCallback;
typedef std::function<void(const char* charUUID, bool enabled, const uint8_t* deviceMac)> HMS_BLE_NotifyCallback;

/* BLE Module *//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class HMS_BLE {
  public:
    HMS_BLE(const char* deviceName);
    ~HMS_BLE();

    void loop();
    HMS_BLE_Status removeCharacteristic(const char* characteristicUUID);
    HMS_BLE_Status begin(const char* serviceUUID, bool backThread = true);
    HMS_BLE_Status addCharacteristic(const HMS_BLE_Characteristic* characteristic);
    HMS_BLE_Status sendData(const char* characteristicUUID, const uint8_t* data, size_t length);                                            // Send data to specific characteristic


    bool isConnected() const                                         { return bleConnected;                                 }
    bool hasReceivedData() const                                     { return received;                                     }
    const uint8_t* getReceivedData() const                           { return data;                                         }
    size_t getReceivedDataLength() const                             { return dataLength;                                   }
    size_t getCharacteristicCount() const                            { return characteristicCount;                          }
    uint8_t getConnectedClients() const                              { return (bleServer != nullptr) ? bleServer->getConnectedCount() : 0; }
    uint8_t getMaxClients() const                                    { return HMS_BLE_MAX_CLIENTS;                          }

    void setReadCallback(HMS_BLE_ReadCallback callback)              { readCallback = callback;                             }
    void setWriteCallback(HMS_BLE_WriteCallback callback)            { writeCallback = callback;                            }
    void setNotifyCallback(HMS_BLE_NotifyCallback callback)          { notifyCallback = callback;                           }
    void setManufacturerData(HMS_BLE_ManufacturerData data)          { manufacturerData = data; manufacturerDataSet = true; }
    void setConnectionCallback(HMS_BLE_ConnectionCallback callback)  { connectionCallback = callback;                       }

  private:
    char                        serviceUUID[40];
    bool                        bleConnected;
    bool                        oldConnected;
    bool                        received;
    bool                        manufacturerDataSet;
    bool                        backgroundProcess;
    bool                        bleInitialized;
    size_t                      dataLength;
    size_t                      characteristicCount;
    uint8_t                     deviceAddress[6];
    uint8_t                     data[HMS_BLE_MAX_DATA_LENGTH];
    const char*                 deviceName;
    static HMS_BLE              *instance;
    HMS_BLE_Characteristic      characteristics[HMS_BLE_MAX_CHARACTERISTICS];
    HMS_BLE_ManufacturerData    manufacturerData;

    HMS_BLE_ReadCallback        readCallback;
    HMS_BLE_WriteCallback       writeCallback;
    HMS_BLE_NotifyCallback      notifyCallback;
    HMS_BLE_ConnectionCallback  connectionCallback;

    void stop();
    HMS_BLE_Status init();
    void restartAdvertising();
    void bleDelay(uint32_t ms);
    int findCharacteristicIndex(const char* uuid) const;    
    HMS_BLE_Status sendDataInternal(int charIndex, const uint8_t* data, size_t length);

    #if defined(HMS_BLE_ARDUINO_ESP32)
      NimBLEServer              *bleServer                                        = nullptr;
      NimBLEService             *bleService                                       = nullptr;
      TaskHandle_t              bleTaskHandle                                     = nullptr;
      NimBLECharacteristic*     bleCharacteristics[HMS_BLE_MAX_CHARACTERISTICS];                                                            // Map to store platform-specific characteristic pointers
      
      // Track notification subscriptions: [characteristic_index][client_index] = enabled
      bool                      notificationEnabled[HMS_BLE_MAX_CHARACTERISTICS][HMS_BLE_MAX_CLIENTS];

      class BLEData : public NimBLECharacteristicCallbacks {
        public:
          BLEData(HMS_BLE* instance, const char* charUUID = nullptr) : hms_ble(instance) {
            if(charUUID) strncpy(this->charUUID, charUUID, sizeof(this->charUUID) - 1);
          }
          void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
          void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
          void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;
        private:
          char      charUUID[40] = {0};
          HMS_BLE   *hms_ble;
      };
      
      class BLEConnectionStatus : public NimBLEServerCallbacks {
        public:
          BLEConnectionStatus(HMS_BLE* instance) : hms_ble(instance) {}
          void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
          void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
        private:
          HMS_BLE * hms_ble;
      };

      static void bleTask(void* pvParameters);
      static const uint8_t* getMacAddressBytes(const NimBLEAddress& address);

    #elif defined(HMS_BLE_ARDUINO_ESP8266)
      // Add ESP8266 specific members 
    #elif defined(HMS_BLE_PLATFORM_ESP_IDF)
      // Add ESP-IDF specific members
    #elif defined(HMS_BLE_PLATFORM_ZEPHYR)
      // Add Zephyr specific members
    #elif defined(HMS_BLE_PLATFORM_STM32_HAL)
      // Add STM32 HAL specific members
    #elif defined(HMS_BLE_PLATFORM_DESKTOP)
      // Add Desktop specific members
    #endif

};

#endif // HMS_BLE_H