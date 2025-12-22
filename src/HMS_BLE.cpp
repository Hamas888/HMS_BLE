#include "HMS_BLE.h"

#if HMS_BLE_DEBUG_ENABLED
    ChronoLogger    *bleLogger             = new ChronoLogger("HMS_BLE", HMS_BLE_LOG_LEVEL);
#endif

HMS_BLE*            HMS_BLE::instance   = nullptr;

HMS_BLE::HMS_BLE(const char* deviceName): 
    bleConnected(false), oldConnected(false), received(false), 
    manufacturerDataSet(false), backgroundProcess(false), bleInitialized(false), 
    dataLength(0), characteristicCount(0), deviceName(deviceName) {


    BLE_LOGGER(debug, "HMS_BLE instance created");

    instance = this;
    memset(data, 0, sizeof(data));
    memset(serviceUUID, 0, sizeof(serviceUUID));
    
    // CRITICAL FIX: Do not use memset on objects with std::string
    for(int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++) {
        characteristics[i].uuid.clear();
        characteristics[i].name.clear();
        characteristics[i].properties = (HMS_BLE_CharacteristicProperty)0;
    }
    
    #if defined(HMS_BLE_ARDUINO_ESP32)
        memset(bleCharacteristics, 0, sizeof(bleCharacteristics));
        memset(notificationEnabled, 0, sizeof(notificationEnabled));
    #endif
}

HMS_BLE::~HMS_BLE() {
    stop();

    if (instance) instance = nullptr;

    memset(data, 0, sizeof(data));
    memset(serviceUUID, 0, sizeof(serviceUUID));
    
    // CRITICAL FIX: Do not use memset on objects with std::string
    for(int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++) {
        characteristics[i].uuid.clear();
        characteristics[i].name.clear();
        characteristics[i].properties = (HMS_BLE_CharacteristicProperty)0;
    }
    
    BLE_LOGGER(debug, "HMS_BLE instance destroyed");
    #if HMS_BLE_DEBUG_ENABLED
        if(bleLogger) {
            delete bleLogger;
            bleLogger = nullptr;
        }
    #endif
    bleInitialized = false;
}

void HMS_BLE::loop() {
    if(backgroundProcess) {
        if (!bleConnected && oldConnected) {
            BLE_LOGGER(info, "Client disconnected, restarting advertising");
            bleDelay(500);                                                                              // Small delay to ensure proper disconnection handling
            oldConnected = bleConnected;
        }

        if(received) {
            BLE_LOGGER(debug, "Data received, invoking callback");
            received = false;
        }
    }
}

void HMS_BLE::bleDelay(uint32_t ms) {
    #if defined(HMS_BLE_PLATFORM_DESKTOP)
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    #elif defined(HMS_BLE_PLATFORM_ZEPHYR)
        k_msleep(ms);
    #elif defined(HMS_BLE_PLATFORM_STM32_HAL)
        HAL_Delay(ms);
    #elif defined(HMS_BLE_PLATFORM_ESP_IDF) || (defined(HMS_BLE_PLATFORM_ARDUINO) && defined(HMS_BLE_ARDUINO_ESP32))
        vTaskDelay(ms / portTICK_PERIOD_MS);
    #endif
}

int HMS_BLE::findCharacteristicIndex(const char* uuid) const {
    if(!uuid) return -1;
    
    for(size_t i = 0; i < characteristicCount; i++) {
        if(strcmp(characteristics[i].uuid.c_str(), uuid) == 0) {
            return i;
        }
    }
    return -1;
}

HMS_BLE_Status HMS_BLE::begin(const char* service_uuid, bool backThread) {
    if(!service_uuid) {
        BLE_LOGGER(error, "Service UUID cannot be null");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(bleInitialized) {                                                                                                                    // Prevent re-initialization
        BLE_LOGGER(warn, "BLE already initialized. Call begin() only once");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    if(characteristicCount == 0) {
        BLE_LOGGER(error, "No characteristics added. Call addCharacteristic() before begin()");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    backgroundProcess = backThread;
    strncpy(serviceUUID, service_uuid, sizeof(serviceUUID) - 1);

    BLE_LOGGER(debug, "Starting BLE with Service UUID: %s, Characteristics: %d",
        service_uuid, characteristicCount
    );

    HMS_BLE_Status status = init();
    if(status != HMS_BLE_STATUS_SUCCESS) {
        return status;
    }

    bleInitialized = true;
    return status;
}

HMS_BLE_Status HMS_BLE::removeCharacteristic(const char* characteristicUUID) {
    if(!characteristicUUID) {
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    int index = -1;
    for(size_t i = 0; i < characteristicCount; i++) {
        if(strcmp(characteristics[i].uuid.c_str(), characteristicUUID) == 0) {
            index = i;
            break;
        }
    }

    if(index == -1) {
        BLE_LOGGER(warn, "Characteristic UUID %s not found", characteristicUUID);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    for(int i = index; i < (int)characteristicCount - 1; i++) {                                                                                  // Shift remaining characteristics
        characteristics[i].uuid = characteristics[i + 1].uuid;
        characteristics[i].name = characteristics[i + 1].name;
        characteristics[i].properties = characteristics[i + 1].properties;
        #if defined(HMS_BLE_ARDUINO_ESP32)
            bleCharacteristics[i] = bleCharacteristics[i + 1];
        #endif
    }

    characteristicCount--;
    
    BLE_LOGGER(debug, "Characteristic removed: UUID=%s, Remaining count=%d", 
        characteristicUUID, characteristicCount
    );

    return HMS_BLE_STATUS_SUCCESS;
}

HMS_BLE_Status HMS_BLE::addCharacteristic(const HMS_BLE_Characteristic* characteristic) {
    if(!characteristic) {
        BLE_LOGGER(error, "Null characteristic pointer provided");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(bleInitialized) {                                                                                                                            // Prevent adding characteristics after BLE has been initialized
        BLE_LOGGER(error, "Cannot add characteristics after begin() has been called");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(characteristicCount >= HMS_BLE_MAX_CHARACTERISTICS) {
        BLE_LOGGER(error, "Maximum characteristics count (%d) reached", HMS_BLE_MAX_CHARACTERISTICS);
        return HMS_BLE_STATUS_ERROR_MAX_CHARS;
    }

    for(size_t i = 0; i < characteristicCount; i++) {                                                                                               // Check for duplicate UUID
        if(strcmp(characteristics[i].uuid.c_str(), characteristic->uuid.c_str()) == 0) {
            BLE_LOGGER(error, "Characteristic with UUID %s already exists", characteristic->uuid.c_str());
            return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
        }
    }

    characteristics[characteristicCount].uuid = characteristic->uuid;                                                                               // Properly copy the characteristic structure
    characteristics[characteristicCount].name = characteristic->name;
    characteristics[characteristicCount].properties = characteristic->properties;
    characteristicCount++;

    BLE_LOGGER(debug, "Characteristic added: UUID=%s, Name=%s, Count=%d", 
        characteristic->uuid.c_str(), characteristic->name.c_str(), characteristicCount
    );

    return HMS_BLE_STATUS_SUCCESS;
}

HMS_BLE_Status HMS_BLE::sendData(const char* characteristicUUID, const uint8_t* data, size_t length) {
    if(!bleConnected) {
        BLE_LOGGER(warn, "Cannot send data, no BLE connection");
        return HMS_BLE_STATUS_ERROR_NOT_CONNECTED;
    }

    if(!characteristicUUID || !data || length == 0) {
        BLE_LOGGER(error, "Invalid parameters for sendData");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(length > HMS_BLE_MAX_DATA_LENGTH) {
        BLE_LOGGER(warn, "Data length exceeds maximum allowed size");
        return HMS_BLE_STATUS_ERROR_SEND;
    }

    
        int charIndex = findCharacteristicIndex(characteristicUUID);
        if(charIndex == -1) {
            BLE_LOGGER(error, "Characteristic UUID %s not found", characteristicUUID);
            return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
        }
    #if defined(HMS_BLE_ARDUINO_ESP32)
        NimBLECharacteristic* pChar = static_cast<NimBLECharacteristic*>(bleCharacteristics[charIndex]);
        if(!pChar) {
            BLE_LOGGER(error, "BLE characteristic pointer is null");
            return HMS_BLE_STATUS_ERROR_SEND;
        }

        pChar->setValue((uint8_t*)data, length);
        
        // Check if any client has subscribed to notifications for this characteristic
        int subscribedCount = 0;
        for(int i = 0; i < HMS_BLE_MAX_CLIENTS; i++) {
            if(notificationEnabled[charIndex][i]) {
                subscribedCount++;
            }
        }
        
        if(subscribedCount > 0) {
            pChar->notify();
            BLE_LOGGER(debug, "Notification sent on %s: %d bytes to %d client(s)", 
                characteristicUUID, length, subscribedCount
            );
        } else {
            BLE_LOGGER(debug, "No clients subscribed to %s, skipping notification", characteristicUUID);
        }
    #elif defined(HMS_BLE_ZEPHYR_nRF)
        // Find the attribute for this characteristic
        const struct bt_gatt_attr *attr = nullptr;
        
        // Iterate through attributes to find the VALUE attribute for this characteristic
        // We stored the index in user_data for the value attribute
        // The simplest approach: just find by user_data (charIndex) and skip non-value attributes
        for(size_t i = 0; i < zephyrAttrCount; i++) {
            // Check if user_data matches our charIndex
            if(zephyrGattAttrs[i].user_data == (void*)((intptr_t)charIndex)) {
                // Make sure this is NOT a CCC, CHRC declaration, or CUD attribute
                // The value attribute has the characteristic UUID, not a standard GATT UUID
                // Standard UUIDs for non-value attrs: BT_UUID_GATT_CHRC (0x2803), BT_UUID_GATT_CCC (0x2902), etc.
                
                // Get the UUID type
                const struct bt_uuid *attrUuid = zephyrGattAttrs[i].uuid;
                
                // If it's a 16-bit UUID, check if it's a standard descriptor UUID
                if (attrUuid->type == BT_UUID_TYPE_16) {
                    uint16_t val = BT_UUID_16(attrUuid)->val;
                    // Skip standard GATT attribute UUIDs (0x2800-0x29FF range)
                    if (val >= 0x2800 && val <= 0x29FF) {
                        continue; // This is a GATT standard attribute, not our value
                    }
                }
                
                // This should be our value attribute
                attr = &zephyrGattAttrs[i];
                break;
            }
        }
        
        if (attr) {
            // Check if notifications are enabled for this characteristic
            // We check our ZephyrCCC struct to see if any peer has enabled notifications
            bool notifyEnabled = false;
            
            // Check if we have a CCC object for this characteristic
            // Note: We don't have a direct map from charIndex to CCC index if not all chars have CCC
            // But our array is indexed by charIndex, so we can check directly.
            // However, we need to know if this characteristic HAS a CCC.
            // We can check properties.
            
            if (characteristics[charIndex].properties & (HMS_BLE_PROPERTY_NOTIFY | HMS_BLE_PROPERTY_INDICATE)) {
                // Iterate through all possible connections in the CCC config
                for (int k = 0; k < BT_GATT_CCC_MAX; k++) {
                    if (zephyrCccObjects[charIndex].cfg[k].value != 0) {
                        notifyEnabled = true;
                        break;
                    }
                }
            }
            
            if (notifyEnabled) {
                // Send notification
                // If connection is NULL, it notifies all connected peers with CCC enabled.
                // We use NULL to broadcast to all subscribed peers, which matches the logic above.
                
                int err = bt_gatt_notify(NULL, attr, data, length);
                if (err) {
                    // -EACCES: Notification is not enabled (should be covered by our check, but race conditions exist)
                    // -ENOTCONN: Not connected
                    if (err != -ENOTCONN && err != -EACCES) {
                        BLE_LOGGER(warn, "Notification failed (err %d)", err);
                        return HMS_BLE_STATUS_ERROR_SEND;
                    }
                } else {
                    BLE_LOGGER(debug, "Notification sent on %s", characteristicUUID);
                }
            } else {
                // Silent return if no one is subscribed, just like ESP32 logic
                // BLE_LOGGER(debug, "No clients subscribed to %s, skipping notification", characteristicUUID);
            }
        } else {
            BLE_LOGGER(error, "Attribute not found for characteristic %s", characteristicUUID);
            return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
        }
    #endif

    return HMS_BLE_STATUS_SUCCESS;
}