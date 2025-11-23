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
    bleService = bleServer->createService(NimBLEUUID(serviceUUID));
    if(!bleService) {
        BLE_LOGGER(error, "Failed to create BLE service");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    for(size_t i = 0; i < characteristicCount; i++) {
        NimBLECharacteristic* pChar = bleService->createCharacteristic(
            BLEUUID(characteristics[i].uuid.c_str()),
            static_cast<uint32_t>(characteristics[i].properties)
        );

        if(!pChar) {
            BLE_LOGGER(error, "Failed to create characteristic: %s", characteristics[i].uuid.c_str());
            return HMS_BLE_STATUS_ERROR_INIT;
        }

        pChar->setCallbacks(new BLEData(instance, characteristics[i].uuid.c_str()));
        bleCharacteristics[i] = pChar;


        BLE_LOGGER(debug, "Created characteristic: %s (%s)", 
            characteristics[i].uuid.c_str(), characteristics[i].name.c_str()
        );
    }

    bleService->start();

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if(!pAdvertising) {
        BLE_LOGGER(error, "Failed to get NimBLE advertising instance");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    pAdvertising->setName(deviceName);
    pAdvertising->addServiceUUID(NimBLEUUID(serviceUUID));

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

HMS_BLE_Status HMS_BLE::sendDataInternal(int charIndex, const uint8_t* data, size_t length) {
    NimBLECharacteristic* pChar = static_cast<NimBLECharacteristic*>(bleCharacteristics[charIndex]);
    if(!pChar) {
        BLE_LOGGER(error, "BLE characteristic pointer is null");
        return HMS_BLE_STATUS_ERROR_SEND;
    }

    pChar->setValue((uint8_t*)data, length);
    
    if(bleServer && bleServer->getConnectedCount() > 0) {
        return pChar->notify() ? HMS_BLE_STATUS_SUCCESS : HMS_BLE_STATUS_ERROR_SEND;

        BLE_LOGGER(debug, "Notification sent on %s: %d bytes to %d client(s)", 
            pChar->getUUID().toString().c_str(), length, bleServer->getConnectedCount()
        );
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
    BLE_LOGGER(debug, "Read on characteristic: %s", charUUID);
    
    if(hms_ble->readCallback) {
        uint8_t readData[HMS_BLE_MAX_DATA_LENGTH] = {0};
        size_t readLength = 0;
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->readCallback(charUUID, readData, &readLength, macBytes);
        if(readLength > 0 && readLength <= HMS_BLE_MAX_DATA_LENGTH) {
            pCharacteristic->setValue(readData, readLength);
        }
    }
}

void HMS_BLE::BLEData::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    if(!hms_ble) return;
    std::string rxValue = pCharacteristic->getValue();
    memcpy(hms_ble->data, rxValue.data(), std::min(rxValue.length(), (size_t)HMS_BLE_MAX_DATA_LENGTH - 1));
    hms_ble->dataLength = std::min(rxValue.length(), (size_t)HMS_BLE_MAX_DATA_LENGTH - 1);
    hms_ble->data[hms_ble->dataLength] = 0;
    hms_ble->received = true;

    BLE_LOGGER(debug, "Write on characteristic: %s (%d bytes)", charUUID, hms_ble->dataLength);

    if(hms_ble->writeCallback) {
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->writeCallback(charUUID, hms_ble->data, hms_ble->dataLength, macBytes);
    }
}

void HMS_BLE::BLEConnectionStatus::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    if(!hms_ble) return;
    
    // Clear subscription data for this client
    uint16_t connHandle = connInfo.getConnHandle();                                                                             // Get the connection handle and map it to a client index
    uint8_t clientIndex = connHandle % HMS_BLE_MAX_CLIENTS;                                                                     // Map connection handle to client index
    
    for(int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++) {
        hms_ble->notificationEnabled[i][clientIndex] = false;
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
    
    int charIndex = hms_ble->findCharacteristicIndex(charUUID);
    if(charIndex == -1) {
        BLE_LOGGER(error, "Characteristic %s not found in subscription callback", charUUID);
        return;
    }

    uint16_t connHandle = connInfo.getConnHandle();                                                                             // Get the connection handle and map it to a client index
    uint8_t clientIndex = connHandle % HMS_BLE_MAX_CLIENTS;                                                                     // Map connection handle to client index
    
    // subValue contains the CCCD value (0x0000=disabled, 0x0001=notify, 0x0002=indicate)
    bool notificationsEnabled = (subValue & 0x0001) != 0;                                                                       // Check if notify bit is set
    
    hms_ble->notificationEnabled[charIndex][clientIndex] = notificationsEnabled;                                                // Track notification subscription for this client and characteristic
    
    BLE_LOGGER(debug, "Subscription changed on %s (client %d): %s", 
        charUUID, clientIndex, notificationsEnabled ? "ENABLED" : "DISABLED"
    );

    if(hms_ble->notifyCallback) {
        const uint8_t* macBytes = getMacAddressBytes(connInfo.getAddress());
        hms_ble->notifyCallback(charUUID, notificationsEnabled, macBytes);
    }
}
#endif