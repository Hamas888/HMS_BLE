#include "HMS_BLE.h"

#if HMS_BLE_DEBUG_ENABLED
    ChronoLogger    *bleLogger             = new ChronoLogger("HMS_BLE", HMS_BLE_LOG_LEVEL);
#endif

HMS_BLE*            HMS_BLE::instance   = nullptr;

HMS_BLE::HMS_BLE(const char* deviceName, HMS_BLE_Mode mode): 
    serviceCount(0), advertisedServiceCount(0), defaultServiceCreated(false),
    received(false), dataLength(0), characteristicCount(0),
    bleMode(mode), beaconDataSet(false), beaconADLen(0), beaconSDLen(0),
    bleConnected(false), oldConnected(false), manufacturerDataSet(false), backgroundProcess(false), bleInitialized(false),
    deviceName(deviceName) {


    BLE_LOGGER(debug, "HMS_BLE instance created (%s mode)",
               mode == HMS_BLE_MODE_BEACON ? "BEACON" : "PERIPHERAL");

    instance = this;
    memset(beaconAD, 0, sizeof(beaconAD));
    memset(beaconSD, 0, sizeof(beaconSD));
    memset(data, 0, sizeof(data));
    memset(serviceUUID, 0, sizeof(serviceUUID));
    memset(advertisedServices, 0, sizeof(advertisedServices));
    
    // Initialize services array
    for(int s = 0; s < HMS_BLE_MAX_SERVICES; s++) {
        services[s].service.uuid.clear();
        services[s].service.name.clear();
        services[s].characteristicCount = 0;
        services[s].dataLength = 0;
        services[s].received = false;
        memset(services[s].data, 0, sizeof(services[s].data));
        
        for(int c = 0; c < HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE; c++) {
            services[s].characteristics[c].uuid.clear();
            services[s].characteristics[c].name.clear();
            services[s].characteristics[c].properties = (HMS_BLE_CharacteristicProperty)0;
            #if defined(HMS_BLE_ARDUINO_ESP32)
                services[s].bleCharacteristics[c] = nullptr;
                for(int k = 0; k < HMS_BLE_MAX_CLIENTS; k++) {
                    services[s].notificationEnabled[c][k] = false;
                }
            #endif
        }
        #if defined(HMS_BLE_ARDUINO_ESP32)
            services[s].bleService = nullptr;
        #endif
    }
    
    // Legacy: initialize flat characteristics array
    for(int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++) {
        characteristics[i].uuid.clear();
        characteristics[i].name.clear();
        characteristics[i].properties = (HMS_BLE_CharacteristicProperty)0;
    }
}

HMS_BLE::~HMS_BLE() {
    stop();

    if (instance) instance = nullptr;

    memset(data, 0, sizeof(data));
    memset(serviceUUID, 0, sizeof(serviceUUID));
    
    // Clear services array
    for(int s = 0; s < HMS_BLE_MAX_SERVICES; s++) {
        services[s].service.uuid.clear();
        services[s].service.name.clear();
        services[s].characteristicCount = 0;
        for(int c = 0; c < HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE; c++) {
            services[s].characteristics[c].uuid.clear();
            services[s].characteristics[c].name.clear();
            services[s].characteristics[c].properties = (HMS_BLE_CharacteristicProperty)0;
        }
    }
    
    // Legacy: clear flat array
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

// ========== Service Lookup Helpers ==========

int HMS_BLE::findServiceIndex(const char* svcUUID) const {
    if(!svcUUID) return -1;
    for(size_t i = 0; i < serviceCount; i++) {
        if(strcmp(services[i].service.uuid.c_str(), svcUUID) == 0) {
            return i;
        }
    }
    return -1;
}

int HMS_BLE::findCharacteristicInService(int serviceIndex, const char* charUUID) const {
    if(serviceIndex < 0 || serviceIndex >= (int)serviceCount || !charUUID) return -1;
    for(size_t i = 0; i < services[serviceIndex].characteristicCount; i++) {
        if(strcmp(services[serviceIndex].characteristics[i].uuid.c_str(), charUUID) == 0) {
            return i;
        }
    }
    return -1;
}

// Legacy: find characteristic across all services (returns flat index for backward compatibility)
int HMS_BLE::findCharacteristicIndex(const char* uuid) const {
    if(!uuid) return -1;
    
    int flatIndex = 0;
    for(size_t s = 0; s < serviceCount; s++) {
        for(size_t c = 0; c < services[s].characteristicCount; c++) {
            if(strcmp(services[s].characteristics[c].uuid.c_str(), uuid) == 0) {
                return flatIndex;
            }
            flatIndex++;
        }
    }
    return -1;
}

size_t HMS_BLE::getTotalCharacteristicCount() const {
    size_t total = 0;
    for(size_t s = 0; s < serviceCount; s++) {
        total += services[s].characteristicCount;
    }
    return total;
}

size_t HMS_BLE::getCharacteristicCountForService(const char* svcUUID) const {
    int idx = findServiceIndex(svcUUID);
    if(idx < 0) return 0;
    return services[idx].characteristicCount;
}

// ========== Multi-Service API ==========

HMS_BLE_Status HMS_BLE::addService(const HMS_BLE_Service* service) {
    if(bleMode != HMS_BLE_MODE_PERIPHERAL) {
        BLE_LOGGER(error, "addService() is only valid in PERIPHERAL mode");
        return HMS_BLE_STATUS_ERROR_INIT;
    }
    if(!service) {
        BLE_LOGGER(error, "Null service pointer provided");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    if(bleInitialized) {
        BLE_LOGGER(error, "Cannot add services after begin() has been called");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    if(serviceCount >= HMS_BLE_MAX_SERVICES) {
        BLE_LOGGER(error, "Maximum services count (%d) reached", HMS_BLE_MAX_SERVICES);
        return HMS_BLE_STATUS_ERROR_MAX_CHARS;
    }
    
    // Check for duplicate service UUID
    for(size_t i = 0; i < serviceCount; i++) {
        if(strcmp(services[i].service.uuid.c_str(), service->uuid.c_str()) == 0) {
            BLE_LOGGER(error, "Service with UUID %s already exists", service->uuid.c_str());
            return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
        }
    }
    
    services[serviceCount].service.uuid = service->uuid;
    services[serviceCount].service.name = service->name;
    services[serviceCount].characteristicCount = 0;
    services[serviceCount].dataLength = 0;
    services[serviceCount].received = false;
    memset(services[serviceCount].data, 0, sizeof(services[serviceCount].data));
    serviceCount++;
    
    BLE_LOGGER(debug, "Service added: UUID=%s, Name=%s, Count=%d",
        service->uuid.c_str(), service->name.c_str(), serviceCount
    );
    
    return HMS_BLE_STATUS_SUCCESS;
}

HMS_BLE_Status HMS_BLE::addCharacteristicToService(const char* svcUUID, const HMS_BLE_Characteristic* characteristic) {
    if(bleMode != HMS_BLE_MODE_PERIPHERAL) {
        BLE_LOGGER(error, "addCharacteristicToService() is only valid in PERIPHERAL mode");
        return HMS_BLE_STATUS_ERROR_INIT;
    }
    if(!svcUUID || !characteristic) {
        BLE_LOGGER(error, "Null service UUID or characteristic pointer provided");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    if(bleInitialized) {
        BLE_LOGGER(error, "Cannot add characteristics after begin() has been called");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    int svcIdx = findServiceIndex(svcUUID);
    if(svcIdx < 0) {
        BLE_LOGGER(error, "Service UUID %s not found", svcUUID);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    if(services[svcIdx].characteristicCount >= HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE) {
        BLE_LOGGER(error, "Maximum characteristics per service (%d) reached for service %s",
            HMS_BLE_MAX_CHARACTERISTICS_PER_SERVICE, svcUUID);
        return HMS_BLE_STATUS_ERROR_MAX_CHARS;
    }
    
    // Check for duplicate characteristic UUID within this service
    for(size_t i = 0; i < services[svcIdx].characteristicCount; i++) {
        if(strcmp(services[svcIdx].characteristics[i].uuid.c_str(), characteristic->uuid.c_str()) == 0) {
            BLE_LOGGER(error, "Characteristic with UUID %s already exists in service %s",
                characteristic->uuid.c_str(), svcUUID);
            return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
        }
    }
    
    size_t charIdx = services[svcIdx].characteristicCount;
    services[svcIdx].characteristics[charIdx].uuid = characteristic->uuid;
    services[svcIdx].characteristics[charIdx].name = characteristic->name;
    services[svcIdx].characteristics[charIdx].properties = characteristic->properties;
    services[svcIdx].characteristicCount++;
    
    BLE_LOGGER(debug, "Characteristic added to service %s: UUID=%s, Name=%s, Count=%d",
        svcUUID, characteristic->uuid.c_str(), characteristic->name.c_str(),
        services[svcIdx].characteristicCount
    );
    
    return HMS_BLE_STATUS_SUCCESS;
}

HMS_BLE_Status HMS_BLE::setAdvertisedServices(const char** serviceUUIDs, size_t count) {
    if(!serviceUUIDs || count == 0) {
        // Clear advertised services list - will advertise all
        advertisedServiceCount = 0;
        BLE_LOGGER(debug, "Advertised services cleared, will advertise all services");
        return HMS_BLE_STATUS_SUCCESS;
    }
    
    if(count > HMS_BLE_MAX_SERVICES) {
        count = HMS_BLE_MAX_SERVICES;
        BLE_LOGGER(warn, "Too many services to advertise, limiting to %d", HMS_BLE_MAX_SERVICES);
    }
    
    advertisedServiceCount = 0;
    for(size_t i = 0; i < count; i++) {
        // Verify service exists
        if(findServiceIndex(serviceUUIDs[i]) >= 0) {
            advertisedServices[advertisedServiceCount++] = serviceUUIDs[i];
        } else {
            BLE_LOGGER(warn, "Service UUID %s not found, skipping from advertisement", serviceUUIDs[i]);
        }
    }
    
    BLE_LOGGER(debug, "Set %d services for advertising", advertisedServiceCount);
    return HMS_BLE_STATUS_SUCCESS;
}

HMS_BLE_Status HMS_BLE::addAdvertisedService(const char* serviceUUID) {
    if (!serviceUUID) {
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    // Already in the list?
    for (size_t i = 0; i < advertisedServiceCount; i++) {
        if (strcmp(advertisedServices[i], serviceUUID) == 0) {
            BLE_LOGGER(debug, "Service %s already in advertised list", serviceUUID);
            return HMS_BLE_STATUS_SUCCESS;
        }
    }

    if (advertisedServiceCount >= HMS_BLE_MAX_SERVICES) {
        BLE_LOGGER(error, "Cannot advertise more than %d services", HMS_BLE_MAX_SERVICES);
        return HMS_BLE_STATUS_ERROR_MAX_CHARS;
    }

    // Allow appending services that aren't registered as GATT services
    // (e.g. the SMP/DFU service, which Zephyr's MCUmgr registers itself).
    advertisedServices[advertisedServiceCount++] = serviceUUID;

    // Rebuild advertisement if already running
    if (bleInitialized) {
        restartAdvertising();
    }

    BLE_LOGGER(debug, "Service %s added to advertised list (%d)", serviceUUID, advertisedServiceCount);
    return HMS_BLE_STATUS_SUCCESS;
}

HMS_BLE_Status HMS_BLE::removeAdvertisedService(const char* serviceUUID) {
    if (!serviceUUID) {
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    for (size_t i = 0; i < advertisedServiceCount; i++) {
        if (strcmp(advertisedServices[i], serviceUUID) == 0) {
            // Shift remaining entries
            for (size_t j = i; j < advertisedServiceCount - 1; j++) {
                advertisedServices[j] = advertisedServices[j + 1];
            }
            advertisedServiceCount--;

            if (bleInitialized) {
                restartAdvertising();
            }

            BLE_LOGGER(debug, "Service %s removed from advertised list (%d)", serviceUUID, advertisedServiceCount);
            return HMS_BLE_STATUS_SUCCESS;
        }
    }

    BLE_LOGGER(warn, "Service %s not in advertised list", serviceUUID);
    return HMS_BLE_STATUS_SUCCESS;
}

// New begin() for multi-service - initializes all registered services
HMS_BLE_Status HMS_BLE::begin(bool backThread) {
    if(bleInitialized) {
        BLE_LOGGER(warn, "BLE already initialized. Call begin() only once");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    if(bleMode == HMS_BLE_MODE_BEACON) {
        if(!beaconDataSet) {
            BLE_LOGGER(error, "Beacon mode: call setBeaconData() before begin()");
            return HMS_BLE_STATUS_ERROR_INIT;
        }
        BLE_LOGGER(debug, "Starting BLE in beacon mode (%u bytes AD, %u bytes SD)",
                   beaconADLen, beaconSDLen);
        backgroundProcess = false; // beacon doesn't need background thread
        HMS_BLE_Status status = init();
        if(status == HMS_BLE_STATUS_SUCCESS) {
            bleInitialized = true;
        }
        return status;
    }
    
    if(serviceCount == 0) {
        BLE_LOGGER(error, "No services added. Call addService() and addCharacteristicToService() before begin()");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    // Verify at least one service has characteristics
    bool hasChars = false;
    for(size_t s = 0; s < serviceCount; s++) {
        if(services[s].characteristicCount > 0) {
            hasChars = true;
            break;
        }
    }
    
    if(!hasChars) {
        BLE_LOGGER(error, "No characteristics added to any service");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    backgroundProcess = backThread;
    
    // For legacy compatibility, store first service UUID
    if(serviceCount > 0) {
        strncpy(serviceUUID, services[0].service.uuid.c_str(), sizeof(serviceUUID) - 1);
    }
    
    BLE_LOGGER(debug, "Starting BLE with %d services, Total characteristics: %d",
        serviceCount, getTotalCharacteristicCount()
    );
    
    HMS_BLE_Status status = init();
    if(status != HMS_BLE_STATUS_SUCCESS) {
        return status;
    }
    
    bleInitialized = true;
    return status;
}

// Per-service data access methods
bool HMS_BLE::hasReceivedDataFromService(const char* svcUUID) const {
    int idx = findServiceIndex(svcUUID);
    if(idx < 0) return false;
    return services[idx].received;
}

const uint8_t* HMS_BLE::getReceivedDataFromService(const char* svcUUID) const {
    int idx = findServiceIndex(svcUUID);
    if(idx < 0) return nullptr;
    return services[idx].data;
}

size_t HMS_BLE::getReceivedDataLengthFromService(const char* svcUUID) const {
    int idx = findServiceIndex(svcUUID);
    if(idx < 0) return 0;
    return services[idx].dataLength;
}

void HMS_BLE::clearReceivedDataFromService(const char* svcUUID) {
    int idx = findServiceIndex(svcUUID);
    if(idx >= 0) {
        services[idx].received = false;
    }
}

HMS_BLE_Status HMS_BLE::sendDataToService(const char* svcUUID, const char* charUUID, const uint8_t* data, size_t length) {
    if(!bleConnected) {
        BLE_LOGGER(warn, "Cannot send data, no BLE connection");
        return HMS_BLE_STATUS_ERROR_NOT_CONNECTED;
    }
    
    if(!svcUUID || !charUUID || !data || length == 0) {
        BLE_LOGGER(error, "Invalid parameters for sendDataToService");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    if(length > HMS_BLE_MAX_DATA_LENGTH) {
        BLE_LOGGER(warn, "Data length exceeds maximum allowed size");
        return HMS_BLE_STATUS_ERROR_SEND;
    }
    
    int svcIdx = findServiceIndex(svcUUID);
    if(svcIdx < 0) {
        BLE_LOGGER(error, "Service UUID %s not found", svcUUID);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    int charIdx = findCharacteristicInService(svcIdx, charUUID);
    if(charIdx < 0) {
        BLE_LOGGER(error, "Characteristic UUID %s not found in service %s", charUUID, svcUUID);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    
    return sendDataInternal(svcIdx, charIdx, data, length);
}

// ========== Legacy Single-Service API (Backward Compatible) ==========

HMS_BLE_Status HMS_BLE::begin(const char* service_uuid, bool backThread) {
    if(bleMode == HMS_BLE_MODE_BEACON) {
        BLE_LOGGER(error, "begin(serviceUUID) not supported in beacon mode — use begin() without arguments");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    if(!service_uuid) {
        BLE_LOGGER(error, "Service UUID cannot be null");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(bleInitialized) {
        BLE_LOGGER(warn, "BLE already initialized. Call begin() only once");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    // Legacy mode: if characteristics were added via addCharacteristic() without addService(),
    // we need to create the default service now and migrate characteristics
    if(!defaultServiceCreated && characteristicCount > 0) {
        // Create the service with the provided UUID
        HMS_BLE_Service defaultSvc;
        defaultSvc.uuid = service_uuid;
        defaultSvc.name = "Default Service";
        
        HMS_BLE_Status status = addService(&defaultSvc);
        if(status != HMS_BLE_STATUS_SUCCESS) {
            return status;
        }
        
        // Migrate characteristics from flat array to the new service
        for(size_t i = 0; i < characteristicCount; i++) {
            status = addCharacteristicToService(service_uuid, &characteristics[i]);
            if(status != HMS_BLE_STATUS_SUCCESS) {
                BLE_LOGGER(error, "Failed to migrate characteristic %s to service", characteristics[i].uuid.c_str());
                return status;
            }
        }
        
        defaultServiceCreated = true;
    } else if(serviceCount == 0) {
        BLE_LOGGER(error, "No characteristics added. Call addCharacteristic() before begin()");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    backgroundProcess = backThread;
    strncpy(serviceUUID, service_uuid, sizeof(serviceUUID) - 1);

    BLE_LOGGER(debug, "Starting BLE with Service UUID: %s, Characteristics: %d",
        service_uuid, getTotalCharacteristicCount()
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
    
    if(bleInitialized) {
        BLE_LOGGER(error, "Cannot remove characteristics after begin() has been called");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    // Search in new services structure
    for(size_t s = 0; s < serviceCount; s++) {
        for(size_t c = 0; c < services[s].characteristicCount; c++) {
            if(strcmp(services[s].characteristics[c].uuid.c_str(), characteristicUUID) == 0) {
                // Shift remaining characteristics
                for(size_t i = c; i < services[s].characteristicCount - 1; i++) {
                    services[s].characteristics[i] = services[s].characteristics[i + 1];
                }
                services[s].characteristicCount--;
                
                BLE_LOGGER(debug, "Characteristic removed from service %s: UUID=%s",
                    services[s].service.uuid.c_str(), characteristicUUID);
                return HMS_BLE_STATUS_SUCCESS;
            }
        }
    }
    
    // Also check legacy flat array (for pre-begin compatibility)
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

    for(int i = index; i < (int)characteristicCount - 1; i++) {
        characteristics[i] = characteristics[i + 1];
    }
    characteristicCount--;
    
    BLE_LOGGER(debug, "Characteristic removed: UUID=%s, Remaining count=%d", 
        characteristicUUID, characteristicCount
    );

    return HMS_BLE_STATUS_SUCCESS;
}

// Legacy addCharacteristic - stores in flat array, will be migrated to default service on begin()
HMS_BLE_Status HMS_BLE::addCharacteristic(const HMS_BLE_Characteristic* characteristic) {
    if(!characteristic) {
        BLE_LOGGER(error, "Null characteristic pointer provided");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(bleInitialized) {
        BLE_LOGGER(error, "Cannot add characteristics after begin() has been called");
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    if(characteristicCount >= HMS_BLE_MAX_CHARACTERISTICS) {
        BLE_LOGGER(error, "Maximum characteristics count (%d) reached", HMS_BLE_MAX_CHARACTERISTICS);
        return HMS_BLE_STATUS_ERROR_MAX_CHARS;
    }

    // Check for duplicate UUID in legacy array
    for(size_t i = 0; i < characteristicCount; i++) {
        if(strcmp(characteristics[i].uuid.c_str(), characteristic->uuid.c_str()) == 0) {
            BLE_LOGGER(error, "Characteristic with UUID %s already exists", characteristic->uuid.c_str());
            return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
        }
    }

    // Store in legacy flat array - will be migrated on begin()
    characteristics[characteristicCount].uuid = characteristic->uuid;
    characteristics[characteristicCount].name = characteristic->name;
    characteristics[characteristicCount].properties = characteristic->properties;
    characteristicCount++;

    BLE_LOGGER(debug, "Characteristic added (legacy): UUID=%s, Name=%s, Count=%d", 
        characteristic->uuid.c_str(), characteristic->name.c_str(), characteristicCount
    );

    return HMS_BLE_STATUS_SUCCESS;
}

// Legacy sendData - finds characteristic across all services
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

    // Find characteristic across all services
    for(size_t s = 0; s < serviceCount; s++) {
        for(size_t c = 0; c < services[s].characteristicCount; c++) {
            if(strcmp(services[s].characteristics[c].uuid.c_str(), characteristicUUID) == 0) {
                return sendDataInternal(s, c, data, length);
            }
        }
    }
    
    BLE_LOGGER(error, "Characteristic UUID %s not found", characteristicUUID);
    return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
}

// ========== Beacon API ==========

HMS_BLE_Status HMS_BLE::setBeaconData(const uint8_t* ad, size_t adLen, const uint8_t* sd, size_t sdLen) {
    if (bleMode != HMS_BLE_MODE_BEACON) {
        BLE_LOGGER(warn, "setBeaconData() is only valid in BEACON mode");
        return HMS_BLE_STATUS_ERROR_INIT;
    }
    if (!ad || adLen == 0 || adLen > HMS_BLE_MAX_AD_DATA) {
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }
    if (sd && sdLen > HMS_BLE_MAX_AD_DATA) {
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    memcpy(beaconAD, ad, adLen);
    beaconADLen = adLen;

    if (sd && sdLen > 0) {
        memcpy(beaconSD, sd, sdLen);
        beaconSDLen = sdLen;
    } else {
        beaconSDLen = 0;
    }
    beaconDataSet = true;
    BLE_LOGGER(debug, "Beacon data set: AD=%u bytes, SD=%u bytes", adLen, beaconSDLen);

    // Restart advertising with new data if already initialized
    if (bleInitialized) {
        restartAdvertising();
    }
    return HMS_BLE_STATUS_SUCCESS;
}

// ---- Beacon format builders ----
// Eddystone TLD suffix codes (used by buildEddystoneURL)
// 0x00=.com/ 0x01=.org/ 0x02=.edu/ 0x03=.net/ 0x04=.info/ 0x05=.biz/ 0x06=.gov/
// 0x07=.com  0x08=.org  0x09=.edu  0x0A=.net  0x0B=.info  0x0C=.biz  0x0D=.gov
static const char* const tldList[] = { ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
                                       ".com",  ".org",  ".edu",  ".net",  ".info",  ".biz",  ".gov" };
#define TLD_COUNT (sizeof(tldList) / sizeof(tldList[0]))
#define TLD_SLASH_START 0
#define TLD_NOSLASH_START 7

size_t HMS_BLE::buildiBeaconAD(uint8_t* buf, size_t bufSize,
                                const uint8_t proximityUUID[16],
                                uint16_t major, uint16_t minor,
                                int8_t txPower_dBm) {
    // AD layout: [Flags(3)] [Manufacturer(28)] = 31 bytes max
    if (!buf || !proximityUUID || bufSize < 31) return 0;

    size_t pos = 0;
    // Flags AD
    buf[pos++] = 0x02;                             // AD length (type + data)
    buf[pos++] = 0x01;                             // AD type: Flags
    buf[pos++] = 0x06;                             // LE General Disc + BR/EDR not supported
    // Manufacturer AD — Apple iBeacon
    buf[pos++] = 26;                               // AD length (type + data: 1 + 25)
    buf[pos++] = 0xFF;                             // AD type: Manufacturer Specific
    buf[pos++] = 0x4C;                             // Company ID: Apple (little-endian)
    buf[pos++] = 0x00;
    buf[pos++] = 0x02;                             // iBeacon indicator
    buf[pos++] = 0x15;
    memcpy(&buf[pos], proximityUUID, 16);          // Proximity UUID
    pos += 16;
    buf[pos++] = (uint8_t)(major >> 8);            // Major (big-endian)
    buf[pos++] = (uint8_t)(major & 0xFF);
    buf[pos++] = (uint8_t)(minor >> 8);            // Minor (big-endian)
    buf[pos++] = (uint8_t)(minor & 0xFF);
    buf[pos++] = (uint8_t)txPower_dBm;             // Calibrated Tx Power
    return pos;                                     // 31 bytes
}

// Detect URL scheme prefix and encode it; returns scheme code and advances past it.
static int detectScheme(const char* url, const char** body) {
    static const struct { const char* prefix; int code; } schemes[] = {
        {"http://www.",  0},
        {"https://www.", 1},
        {"http://",      2},
        {"https://",     3},
    };
    for (int i = 0; i < 4; i++) {
        size_t plen = strlen(schemes[i].prefix);
        if (strncmp(url, schemes[i].prefix, plen) == 0) {
            *body = url + plen;
            return schemes[i].code;
        }
    }
    return -1; // unknown scheme
}

// Encode TLD at end of URL body; returns TLD code or -1 if not found.
// Also sets *tldLen to the length of the TLD string (including dot).
static int encodeTLD(const char* body, size_t bodyLen, size_t* tldLen) {
    for (size_t i = 0; i < TLD_COUNT; i++) {
        size_t tlen = strlen(tldList[i]);
        if (bodyLen >= tlen && memcmp(body + bodyLen - tlen, tldList[i], tlen) == 0) {
            *tldLen = tlen;
            return (int)(i + TLD_SLASH_START);
        }
    }
    return -1;
}

size_t HMS_BLE::buildEddystoneURL(uint8_t* buf, size_t bufSize,
                                   const char* url, int8_t txPower_dBm) {
    // AD layout: [Flags(3)] [UUID16(4)] [Service Data(5 + encoded_url)]
    if (!buf || !url || bufSize < 12) return 0;

    const char* body;
    int scheme = detectScheme(url, &body);
    if (scheme < 0) return 0;                       // invalid/no scheme

    size_t bodyLen = strlen(body);
    size_t tldLen = 0;
    int tldCode = encodeTLD(body, bodyLen, &tldLen);

    size_t encLen = bodyLen;
    if (tldCode >= 0) encLen = bodyLen - tldLen + 1; // TLD replaced by 1 byte

    size_t sdLen = 5 + encLen;                       // 0xAA 0xFE + frame(1) + tx(1) + scheme(1) + encoded
    if (sdLen > 27) return 0;                        // would exceed AD limit

    size_t pos = 0;
    // Flags AD
    buf[pos++] = 0x02;
    buf[pos++] = 0x01;
    buf[pos++] = 0x06;
    // Service UUID AD (Eddystone 0xFEAA)
    buf[pos++] = 0x03;
    buf[pos++] = 0x03;
    buf[pos++] = 0xAA;
    buf[pos++] = 0xFE;
    // Service Data AD
    buf[pos++] = (uint8_t)(sdLen + 1);               // AD length = type(1) + sdLen
    buf[pos++] = 0x16;                                // AD type: Service Data - 16-bit UUID
    buf[pos++] = 0xAA;
    buf[pos++] = 0xFE;
    buf[pos++] = 0x10;                                // Frame type: Eddystone-URL
    buf[pos++] = (uint8_t)txPower_dBm;
    buf[pos++] = (uint8_t)scheme;                     // URL scheme prefix code
    // Encoded URL body
    if (tldCode >= 0) {
        memcpy(&buf[pos], body, bodyLen - tldLen);    // copy body without TLD
        pos += bodyLen - tldLen;
        buf[pos++] = (uint8_t)tldCode;                // TLD suffix code
    } else {
        memcpy(&buf[pos], body, bodyLen);             // copy entire body as-is
        pos += bodyLen;
    }
    return pos;
}

size_t HMS_BLE::buildEddystoneUID(uint8_t* buf, size_t bufSize,
                                   const uint8_t namespaceID[10],
                                   const uint8_t instanceID[6],
                                   int8_t txPower_dBm) {
    // AD layout: [Flags(3)] [UUID16(4)] [Service Data(5 + 2 + 10 + 6)]
    // Frame data: 0x00 + txPower + namespace(10) + instance(6) = 18 bytes
    if (!buf || !namespaceID || !instanceID || bufSize < 27) return 0;

    size_t pos = 0;
    // Flags AD
    buf[pos++] = 0x02;
    buf[pos++] = 0x01;
    buf[pos++] = 0x06;
    // Service UUID AD
    buf[pos++] = 0x03;
    buf[pos++] = 0x03;
    buf[pos++] = 0xAA;
    buf[pos++] = 0xFE;
    // Service Data AD
    buf[pos++] = 20;                                 // AD length: type(1) + UUID(2) + frame(18) = 20
    buf[pos++] = 0x16;
    buf[pos++] = 0xAA;
    buf[pos++] = 0xFE;
    buf[pos++] = 0x00;                                // Frame type: Eddystone-UID
    buf[pos++] = (uint8_t)txPower_dBm;
    memcpy(&buf[pos], namespaceID, 10);               // 10-byte namespace
    pos += 10;
    memcpy(&buf[pos], instanceID, 6);                 // 6-byte instance
    pos += 6;
    return pos;                                        // 27 bytes
}

size_t HMS_BLE::buildManufacturerAD(uint8_t* buf, size_t bufSize,
                                     uint16_t companyID,
                                     const uint8_t* mfgData, size_t mfgLen) {
    // AD layout: [Flags(3)] [Manufacturer(3 + mfgLen)]
    if (!buf || !mfgData || bufSize < (6 + mfgLen) || mfgLen > 26) return 0;

    size_t pos = 0;
    // Flags AD
    buf[pos++] = 0x02;
    buf[pos++] = 0x01;
    buf[pos++] = 0x06;
    // Manufacturer AD
    buf[pos++] = (uint8_t)(1 + 2 + mfgLen);           // AD length: type(1) + companyID(2) + mfgData
    buf[pos++] = 0xFF;                                 // AD type: Manufacturer Specific
    buf[pos++] = (uint8_t)(companyID & 0xFF);          // Company ID (little-endian)
    buf[pos++] = (uint8_t)(companyID >> 8);
    memcpy(&buf[pos], mfgData, mfgLen);
    pos += mfgLen;
    return pos;
}