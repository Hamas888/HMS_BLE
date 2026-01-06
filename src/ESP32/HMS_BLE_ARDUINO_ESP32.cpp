#include "HMS_BLE.h"

#if defined(HMS_BLE_ARDUINO_ESP32)

void HMS_BLE::stop() {
    if (bleTaskHandle != nullptr) {
        vTaskDelete(bleTaskHandle);
        bleTaskHandle = nullptr;
    }
    NimBLEDevice::deinit();
}

HMS_BLE_Status HMS_BLE::init() {
    NimBLEDevice::init(deviceName);
    bleServer = NimBLEDevice::createServer();
    if(!bleServer) {
        BLE_LOGGER(error, "Failed to create NimBLE server");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    bleServer->setCallbacks(new BLEConnectionStatus(instance));
    
    // Create all registered services
    for(size_t s = 0; s < serviceCount; s++) {
        NimBLEService* pService = bleServer->createService(NimBLEUUID(services[s].service.uuid.c_str()));
        if(!pService) {
            BLE_LOGGER(error, "Failed to create BLE service: %s", services[s].service.uuid.c_str());
            return HMS_BLE_STATUS_ERROR_INIT;
        }
        services[s].bleService = pService;
        
        BLE_LOGGER(debug, "Created service: %s (%s)", 
            services[s].service.uuid.c_str(), services[s].service.name.c_str()
        );
        
        // Create characteristics for this service
        for(size_t c = 0; c < services[s].characteristicCount; c++) {
            NimBLECharacteristic* pChar = pService->createCharacteristic(
                BLEUUID(services[s].characteristics[c].uuid.c_str()),
                static_cast<uint32_t>(services[s].characteristics[c].properties)
            );

            if(!pChar) {
                BLE_LOGGER(error, "Failed to create characteristic: %s", services[s].characteristics[c].uuid.c_str());
                return HMS_BLE_STATUS_ERROR_INIT;
            }

            // Pass service UUID, char UUID, and indices to callback
            pChar->setCallbacks(new BLEData(instance, 
                services[s].service.uuid.c_str(),
                services[s].characteristics[c].uuid.c_str(),
                s, c));
            services[s].bleCharacteristics[c] = pChar;

            BLE_LOGGER(debug, "  Created characteristic: %s (%s)", 
                services[s].characteristics[c].uuid.c_str(), services[s].characteristics[c].name.c_str()
            );
        }
        
        pService->start();
        BLE_LOGGER(debug, "Started service: %s with %d characteristics", 
            services[s].service.uuid.c_str(), services[s].characteristicCount
        );
    }

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if(!pAdvertising) {
        BLE_LOGGER(error, "Failed to get NimBLE advertising instance");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    pAdvertising->setName(deviceName);
    
    // Add service UUIDs to advertising
    if(advertisedServiceCount > 0) {
        // User specified which services to advertise
        for(size_t i = 0; i < advertisedServiceCount; i++) {
            int svcIdx = findServiceIndex(advertisedServices[i]);
            if(svcIdx >= 0) {
                pAdvertising->addServiceUUID(NimBLEUUID(services[svcIdx].service.uuid.c_str()));
                BLE_LOGGER(debug, "Advertising service: %s", services[svcIdx].service.uuid.c_str());
            }
        }
    } else {
        // Advertise first service by default (BLE advertising has limited space)
        if(serviceCount > 0) {
            pAdvertising->addServiceUUID(NimBLEUUID(services[0].service.uuid.c_str()));
            BLE_LOGGER(debug, "Advertising primary service: %s", services[0].service.uuid.c_str());
        }
    }

    if(manufacturerDataSet) {
        std::string mfgDataStr;
        mfgDataStr += (char)(manufacturerData.manufacturer_id[0] & 0xFF);
        mfgDataStr += (char)(manufacturerData.manufacturer_id[1] & 0xFF);
        mfgDataStr.append(reinterpret_cast<const char*>(manufacturerData.data.data()), manufacturerData.data.size());
        pAdvertising->setManufacturerData(mfgDataStr);
        BLE_LOGGER(debug, "Manufacturer data set in advertising packet");
    }

    pAdvertising->start();

    BLE_LOGGER(debug, "NimBLE advertising started");

    if(backgroundProcess) {
        xTaskCreatePinnedToCore(
            bleTask,
            "HMS_BLE_Task",
            HMS_BLE_BACKGROUND_PROCESS_STACK_SIZE,
            nullptr,
            HMS_BLE_BACKGROUND_PROCESS_PRIORITY,
            &bleTaskHandle,
            1
        );
        BLE_LOGGER(debug, "Background BLE task created");
    }

    return HMS_BLE_STATUS_SUCCESS;
}

void HMS_BLE::restartAdvertising() {
    if (bleServer) {
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        if(pAdvertising) {
            pAdvertising->start();
        }
    }
}

void HMS_BLE::bleTask(void* pvParameters) {
    HMS_BLE* pThis = HMS_BLE::instance;
    if(!pThis) return;
    while(true) {
        pThis->loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

const uint8_t* HMS_BLE::getMacAddressBytes(const NimBLEAddress& address) {
    const ble_addr_t* addrBase = address.getBase();
    return addrBase->val;
}

HMS_BLE_Status HMS_BLE::sendDataInternal(int serviceIndex, int charIndex, const uint8_t* data, size_t length) {
    if(serviceIndex < 0 || serviceIndex >= (int)serviceCount) {
        BLE_LOGGER(error, "Invalid service index: %d", serviceIndex);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    if(charIndex < 0 || charIndex >= (int)services[serviceIndex].characteristicCount) {
        BLE_LOGGER(error, "Invalid characteristic index: %d for service %d", charIndex, serviceIndex);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    NimBLECharacteristic* pChar = services[serviceIndex].bleCharacteristics[charIndex];
    if(!pChar) {
        BLE_LOGGER(error, "BLE characteristic pointer is null");
        return HMS_BLE_STATUS_ERROR_SEND;
    }

    pChar->setValue((uint8_t*)data, length);
    
    if(bleServer && bleServer->getConnectedCount() > 0) {
        // Check if any client has subscribed to notifications for this characteristic
        int subscribedCount = 0;
        for(int i = 0; i < HMS_BLE_MAX_CLIENTS; i++) {
            if(services[serviceIndex].notificationEnabled[charIndex][i]) {
                subscribedCount++;
            }
        }
        
        if(subscribedCount > 0) {
            bool result = pChar->notify();
            BLE_LOGGER(debug, "Notification sent on %s: %d bytes to %d client(s)", 
                pChar->getUUID().toString().c_str(), length, subscribedCount
            );
            return result ? HMS_BLE_STATUS_SUCCESS : HMS_BLE_STATUS_ERROR_SEND;
        } else {
            BLE_LOGGER(debug, "No clients subscribed to %s, skipping notification", 
                pChar->getUUID().toString().c_str());
            return HMS_BLE_STATUS_SUCCESS;
        }
    } else {
        BLE_LOGGER(warn, "No connected clients to notify for %s", pChar->getUUID().toString().c_str());
        return HMS_BLE_STATUS_ERROR_NOT_CONNECTED;
    }
}                                             

void HMS_BLE::BLEConnectionStatus::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    if(!hms_ble) return;
    hms_ble->bleConnected = true;
    hms_ble->oldConnected = true;
    BLE_LOGGER(debug, "BLE Client Connected");
    if(hms_ble->connectionCallback) {
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->connectionCallback(true, macBytes);
    }
}    

void HMS_BLE::BLEData::onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    if(!hms_ble) return;
    BLE_LOGGER(debug, "Read on service %s, characteristic: %s", serviceUUID, charUUID);
    
    if(hms_ble->readCallback) {
        uint8_t readData[HMS_BLE_MAX_DATA_LENGTH] = {0};
        size_t readLength = 0;
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->readCallback(serviceUUID, charUUID, readData, &readLength, macBytes);
        if(readLength > 0 && readLength <= HMS_BLE_MAX_DATA_LENGTH) {
            pCharacteristic->setValue(readData, readLength);
        }
    }
}

void HMS_BLE::BLEData::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    if(!hms_ble) return;
    std::string rxValue = pCharacteristic->getValue();
    
    // Store in per-service buffer
    if(serviceIndex >= 0 && serviceIndex < HMS_BLE_MAX_SERVICES) {
        memcpy(hms_ble->services[serviceIndex].data, rxValue.data(), 
            std::min(rxValue.length(), (size_t)HMS_BLE_MAX_DATA_LENGTH - 1));
        hms_ble->services[serviceIndex].dataLength = std::min(rxValue.length(), (size_t)HMS_BLE_MAX_DATA_LENGTH - 1);
        hms_ble->services[serviceIndex].data[hms_ble->services[serviceIndex].dataLength] = 0;
        hms_ble->services[serviceIndex].received = true;
    }
    
    // Also store in legacy shared buffer for backward compatibility
    memcpy(hms_ble->data, rxValue.data(), std::min(rxValue.length(), (size_t)HMS_BLE_MAX_DATA_LENGTH - 1));
    hms_ble->dataLength = std::min(rxValue.length(), (size_t)HMS_BLE_MAX_DATA_LENGTH - 1);
    hms_ble->data[hms_ble->dataLength] = 0;
    hms_ble->received = true;

    BLE_LOGGER(debug, "Write on service %s, characteristic: %s (%d bytes)", serviceUUID, charUUID, hms_ble->dataLength);

    if(hms_ble->writeCallback) {
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->writeCallback(serviceUUID, charUUID, hms_ble->data, hms_ble->dataLength, macBytes);
    }
}

void HMS_BLE::BLEConnectionStatus::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    if(!hms_ble) return;
    
    // Clear subscription data for this client across all services
    uint16_t connHandle = connInfo.getConnHandle();
    uint8_t clientIndex = connHandle % HMS_BLE_MAX_CLIENTS;
    
    for(size_t s = 0; s < hms_ble->serviceCount; s++) {
        for(size_t c = 0; c < hms_ble->services[s].characteristicCount; c++) {
            hms_ble->services[s].notificationEnabled[c][clientIndex] = false;
        }
    }
    
    hms_ble->bleConnected = false;
    BLE_LOGGER(debug, "BLE Client Disconnected - Reason: %d", reason);
    if(hms_ble->connectionCallback) {
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->connectionCallback(false, macBytes);
    }
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if(pAdvertising) {
        pAdvertising->start();
    }
}

void HMS_BLE::BLEData::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
    if(!hms_ble) return;
    
    if(serviceIndex < 0 || serviceIndex >= HMS_BLE_MAX_SERVICES ||
       charIndex < 0 || charIndex >= HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE) {
        BLE_LOGGER(error, "Invalid indices in subscription callback (svc=%d, char=%d)", serviceIndex, charIndex);
        return;
    }

    uint16_t connHandle = connInfo.getConnHandle();
    uint8_t clientIndex = connHandle % HMS_BLE_MAX_CLIENTS;
    
    bool notificationsEnabled = (subValue & 0x0001) != 0;
    
    hms_ble->services[serviceIndex].notificationEnabled[charIndex][clientIndex] = notificationsEnabled;
    
    BLE_LOGGER(debug, "Subscription changed on service %s, char %s (client %d): %s", 
        serviceUUID, charUUID, clientIndex, notificationsEnabled ? "ENABLED" : "DISABLED"
    );

    if(hms_ble->notifyCallback) {
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->notifyCallback(serviceUUID, charUUID, notificationsEnabled, macBytes);
    }
}
#endif