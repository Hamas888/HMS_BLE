#include "HMS_BLE.h"

#if defined(HMS_BLE_BLUEZ_LINUX)

#include <systemd/sd-bus.h>
#include <bluetooth/bluetooth.h>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <vector>
#include <unistd.h>

// BlueZ D-Bus interfaces
#define BLUEZ_ADAPTER1              "org.bluez.Adapter1"
#define BLUEZ_GATT_MANAGER1         "org.bluez.GattManager1"
#define BLUEZ_GATT_SERVICE1         "org.bluez.GattService1"
#define BLUEZ_GATT_CHARACTERISTIC1  "org.bluez.GattCharacteristic1"
#define BLUEZ_LE_ADVERTISEMENT1     "org.bluez.LEAdvertisement1"
#define BLUEZ_LE_ADVERTISING_MGR1   "org.bluez.LEAdvertisingManager1"
// ==========================================================================
// UUID conversion: hex-string to 16-byte little-endian (BlueZ D-Bus order)
// ==========================================================================
void HMS_BLE::uuidStringToBytes(const char* uuidStr, uint8_t bytes[16]) {
    if (!uuidStr || !bytes) return;
    size_t len = strlen(uuidStr);
    if (len == 4) {
        static const uint8_t base[16] = {
            0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,
            0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00
        };
        memcpy(bytes, base, 16);
        unsigned int u16 = 0;
        for (int i = 0; i < 4; i++) {
            char c = uuidStr[i]; u16 <<= 4;
            if (c >= '0' && c <= '9')      u16 |= (c - '0');
            else if (c >= 'a' && c <= 'f') u16 |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') u16 |= (c - 'A' + 10);
        }
        bytes[12] = u16 & 0xFF; bytes[13] = (u16 >> 8) & 0xFF;
        return;
    }
    memset(bytes, 0, 16);
    int byteIdx = 15;
    for (size_t i = 0; uuidStr[i] && byteIdx >= 0; i++) {
        if (uuidStr[i] == '-') continue;
        auto h2b = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        uint8_t high = h2b(uuidStr[i]); i++;
        if (!uuidStr[i]) { bytes[byteIdx--] = high; break; }
        uint8_t low = h2b(uuidStr[i]);
        bytes[byteIdx--] = (high << 4) | low;
    }
}

// ==========================================================================
// GattService1 property getters
// ==========================================================================
int HMS_BLE::bluezServiceGetUUID(sd_bus *bus, const char *path, const char *iface,
                                  const char *prop, sd_bus_message *reply,
                                  void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError;
    int svcIdx = (int)(intptr_t)userdata;
    if (!instance || svcIdx < 0 || (size_t)svcIdx >= instance->serviceCount)
        return sd_bus_reply_method_errorf(reply, "org.bluez.Error.Invalid", "Invalid service index");
    return sd_bus_message_append(reply, "s", instance->services[svcIdx].service.uuid.c_str());
}

int HMS_BLE::bluezServiceGetPrimary(sd_bus *bus, const char *path, const char *iface,
                                     const char *prop, sd_bus_message *reply,
                                     void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError; (void)userdata;
    return sd_bus_message_append(reply, "b", 1);
}

static const sd_bus_vtable bluez_service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID",    "s", HMS_BLE::bluezServiceGetUUID,    0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Primary", "b", HMS_BLE::bluezServiceGetPrimary, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END
};

// ==========================================================================
// GattCharacteristic1 property getters
// ==========================================================================
int HMS_BLE::bluezCharGetUUID(sd_bus *bus, const char *path, const char *iface,
                               const char *prop, sd_bus_message *reply,
                               void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(reply, "org.bluez.Error.Invalid", "");
    return sd_bus_message_append(reply, "s", instance->services[svcIdx].characteristics[localIdx].uuid.c_str());
}

int HMS_BLE::bluezCharGetService(sd_bus *bus, const char *path, const char *iface,
                                  const char *prop, sd_bus_message *reply,
                                  void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(reply, "org.bluez.Error.Invalid", "");
    char svcPath[64];
    snprintf(svcPath, sizeof(svcPath), "/com/hmsble/app/service%d", svcIdx);
    return sd_bus_message_append(reply, "o", svcPath);
}

int HMS_BLE::bluezCharGetValue(sd_bus *bus, const char *path, const char *iface,
                                const char *prop, sd_bus_message *reply,
                                void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(reply, "org.bluez.Error.Invalid", "");
    return sd_bus_message_append(reply, "ay",
                                 instance->services[svcIdx].data, instance->services[svcIdx].dataLength);
}

int HMS_BLE::bluezCharGetFlags(sd_bus *bus, const char *path, const char *iface,
                                const char *prop, sd_bus_message *reply,
                                void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(reply, "org.bluez.Error.Invalid", "");
    uint32_t props = static_cast<uint32_t>(instance->services[svcIdx].characteristics[localIdx].properties);
    int r = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "s");
    if (r < 0) return r;
    if (props & 0x02) { r = sd_bus_message_append(reply, "s", "read");      if (r < 0) return r; }
    if (props & 0x08) { r = sd_bus_message_append(reply, "s", "write");     if (r < 0) return r; }
    if (props & 0x10) { r = sd_bus_message_append(reply, "s", "notify");    if (r < 0) return r; }
    if (props & 0x20) { r = sd_bus_message_append(reply, "s", "indicate"); if (r < 0) return r; }
    if (props & 0x01) { r = sd_bus_message_append(reply, "s", "broadcast"); if (r < 0) return r; }
    return sd_bus_message_close_container(reply);
}

int HMS_BLE::bluezCharGetNotifying(sd_bus *bus, const char *path, const char *iface,
                                    const char *prop, sd_bus_message *reply,
                                    void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError;
    int globalCharIdx = (int)(intptr_t)userdata;
    return sd_bus_message_append(reply, "b",
        (instance && globalCharIdx >= 0 && globalCharIdx < HMS_BLE_MAX_CHARACTERISTICS)
        ? (int)instance->bluezNotifEnabled[globalCharIdx] : 0);
}

// ==========================================================================
// GattCharacteristic1 method handlers
// ==========================================================================
int HMS_BLE::bluezCharMethodReadValue(sd_bus_message* m, void* userdata, sd_bus_error* retError) {
    (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(m, "org.bluez.Error.Failed", "Characteristic not found");

    if (instance->readCallback) {
        uint8_t temp[HMS_BLE_MAX_DATA_LENGTH] = {0};
        size_t outLen = HMS_BLE_MAX_DATA_LENGTH;
        uint8_t mac[6] = {0};
        instance->readCallback(
            instance->services[svcIdx].service.uuid.c_str(),
            instance->services[svcIdx].characteristics[localIdx].uuid.c_str(),
            temp, &outLen, mac);
        if (outLen > 0) {
            memcpy(instance->services[svcIdx].data, temp, outLen);
            instance->services[svcIdx].dataLength = outLen;
            return sd_bus_reply_method_return(m, "ay", temp, outLen);
        }
    }
    return sd_bus_reply_method_return(m, "ay",
        instance->services[svcIdx].data, instance->services[svcIdx].dataLength);
}

int HMS_BLE::bluezCharMethodWriteValue(sd_bus_message* m, void* userdata, sd_bus_error* retError) {
    (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(m, "org.bluez.Error.Failed", "Characteristic not found");

    const void* buf = nullptr;
    size_t bufLen = 0;
    int r = sd_bus_message_read_array(m, 'y', &buf, &bufLen);
    if (r < 0) return r;

    size_t copyLen = (bufLen > HMS_BLE_MAX_DATA_LENGTH) ? HMS_BLE_MAX_DATA_LENGTH : bufLen;
    memcpy(instance->services[svcIdx].data, buf, copyLen);
    instance->services[svcIdx].dataLength = copyLen;
    instance->services[svcIdx].received = true;
    if (svcIdx == 0) {
        memcpy(instance->data, buf, copyLen);
        instance->dataLength = copyLen;
        instance->received = true;
    }

    BLE_LOGGER(debug, "Write on service %d char %d, %d bytes", svcIdx, localIdx, bufLen);

    if (instance->writeCallback) {
        uint8_t mac[6] = {0};
        instance->writeCallback(
            instance->services[svcIdx].service.uuid.c_str(),
            instance->services[svcIdx].characteristics[localIdx].uuid.c_str(),
            (const uint8_t*)buf, bufLen, mac);
    }

    // Emit PropertiesChanged for Value
    char* changedProps[] = { (char*)"Value", NULL };
    sd_bus_emit_properties_changed_strv(instance->bluezBus,
        instance->bluezCharPaths[gcidx].c_str(),
        BLUEZ_GATT_CHARACTERISTIC1, changedProps);

    return sd_bus_reply_method_return(m, "");
}

int HMS_BLE::bluezCharMethodStartNotify(sd_bus_message* m, void* userdata, sd_bus_error* retError) {
    (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(m, "org.bluez.Error.Failed", "Not found");

    instance->bluezNotifEnabled[gcidx] = true;
    BLE_LOGGER(info, "Notifications enabled for svc %d char %d", svcIdx, localIdx);

    if (instance->notifyCallback) {
        uint8_t mac[6] = {0};
        instance->notifyCallback(
            instance->services[svcIdx].service.uuid.c_str(),
            instance->services[svcIdx].characteristics[localIdx].uuid.c_str(), true, mac);
    }
    return sd_bus_reply_method_return(m, "");
}

int HMS_BLE::bluezCharMethodStopNotify(sd_bus_message* m, void* userdata, sd_bus_error* retError) {
    (void)retError;
    int gcidx = (int)(intptr_t)userdata;
    int svcIdx, localIdx;
    if (!instance || !instance->mapGlobalCharIdx(gcidx, &svcIdx, &localIdx))
        return sd_bus_reply_method_errorf(m, "org.bluez.Error.Failed", "Not found");

    instance->bluezNotifEnabled[gcidx] = false;
    BLE_LOGGER(info, "Notifications disabled for svc %d char %d", svcIdx, localIdx);

    if (instance->notifyCallback) {
        uint8_t mac[6] = {0};
        instance->notifyCallback(
            instance->services[svcIdx].service.uuid.c_str(),
            instance->services[svcIdx].characteristics[localIdx].uuid.c_str(), false, mac);
    }
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable bluez_char_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID",      "s",   HMS_BLE::bluezCharGetUUID,      0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Service",   "o",   HMS_BLE::bluezCharGetService,   0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Value",     "ay",  HMS_BLE::bluezCharGetValue,     0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Flags",     "as",  HMS_BLE::bluezCharGetFlags,     0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Notifying", "b",   HMS_BLE::bluezCharGetNotifying, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("ReadValue",   "a{sv}", "ay", HMS_BLE::bluezCharMethodReadValue,  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue",  "aya{sv}", "",  HMS_BLE::bluezCharMethodWriteValue, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StartNotify", "", "",          HMS_BLE::bluezCharMethodStartNotify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify",  "", "",          HMS_BLE::bluezCharMethodStopNotify,  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

// ==========================================================================
// LEAdvertisement1 property getters and methods
// ==========================================================================
int HMS_BLE::bluezAdvGetType(sd_bus *bus, const char *path, const char *iface,
                              const char *prop, sd_bus_message *reply,
                              void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError; (void)userdata;
    return sd_bus_message_append(reply, "s",
        instance && instance->getMode() == HMS_BLE_MODE_BEACON ? "broadcast" : "peripheral");
}

int HMS_BLE::bluezAdvGetServiceUUIDs(sd_bus *bus, const char *path, const char *iface,
                                      const char *prop, sd_bus_message *reply,
                                      void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError; (void)userdata;
    int r = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "s");
    if (r < 0) return r;
    if (instance) {
        size_t advCount = (instance->advertisedServiceCount > 0)
            ? instance->advertisedServiceCount : instance->serviceCount;
        for (size_t i = 0; i < advCount; i++) {
            const char* uuid = (instance->advertisedServiceCount > 0)
                ? instance->advertisedServices[i]
                : instance->services[i].service.uuid.c_str();
            r = sd_bus_message_append(reply, "s", uuid);
            if (r < 0) return r;
        }
    }
    return sd_bus_message_close_container(reply);
}

int HMS_BLE::bluezAdvGetLocalName(sd_bus *bus, const char *path, const char *iface,
                                   const char *prop, sd_bus_message *reply,
                                   void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError; (void)userdata;
    return sd_bus_message_append(reply, "s", instance ? instance->deviceName : "");
}

int HMS_BLE::bluezAdvGetDiscoverable(sd_bus *bus, const char *path, const char *iface,
                                      const char *prop, sd_bus_message *reply,
                                      void *userdata, sd_bus_error *retError) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)retError; (void)userdata;
    return sd_bus_message_append(reply, "b", 1);
}

int HMS_BLE::bluezAdvMethodRelease(sd_bus_message* m, void* userdata, sd_bus_error* retError) {
    (void)userdata; (void)retError;
    BLE_LOGGER(debug, "Advertisement released by BlueZ");
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable bluez_adv_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Type",          "s",  HMS_BLE::bluezAdvGetType,          0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ServiceUUIDs",  "as", HMS_BLE::bluezAdvGetServiceUUIDs,  0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LocalName",     "s",  HMS_BLE::bluezAdvGetLocalName,     0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Discoverable",  "b",  HMS_BLE::bluezAdvGetDiscoverable,  0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("Release", "", "", HMS_BLE::bluezAdvMethodRelease, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

// ==========================================================================
// Helper (member): map globalCharIdx → (svcIdx, localIdx)
// ==========================================================================
bool HMS_BLE::mapGlobalCharIdx(int globalCharIdx, int* svcIdx, int* localIdx) const {
    int acc = 0;
    for (size_t s = 0; s < serviceCount; s++) {
        for (size_t c = 0; c < services[s].characteristicCount; c++) {
            if (acc == globalCharIdx) { *svcIdx = (int)s; *localIdx = (int)c; return true; }
            acc++;
        }
    }
    return false;
}

// ==========================================================================
// BlueZ implementation: init()
// ==========================================================================
HMS_BLE_Status HMS_BLE::init() {
    int r;

    // 1. Open system bus
    r = sd_bus_open_system(&bluezBus);
    if (r < 0) {
        BLE_LOGGER(error, "Failed to connect to system D-Bus: %s", strerror(-r));
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    // 2. Request well-known bus name
    sd_bus_request_name(bluezBus, "com.hmsble", 0);

    // 3. Find BlueZ adapter
    r = bluezSetupAdapter();
    if (r != HMS_BLE_STATUS_SUCCESS) { sd_bus_unref(bluezBus); bluezBus = nullptr; return (HMS_BLE_Status)r; }

    // ---- Beacon mode ----
    if (bleMode == HMS_BLE_MODE_BEACON) {
        restartAdvertising();
        return HMS_BLE_STATUS_SUCCESS;
    }

    // 4. Register GATT services
    r = bluezRegisterApp();
    if (r != HMS_BLE_STATUS_SUCCESS) { sd_bus_unref(bluezBus); bluezBus = nullptr; return (HMS_BLE_Status)r; }

    // 5. Start advertising
    restartAdvertising();

    // 6. Background thread for loop()
    if (backgroundProcess) {
        bluezThreadRunning = true;
        bluezBleThread = new std::thread([]() {
            HMS_BLE* pThis = HMS_BLE::instance;
            if (!pThis) return;
            while (pThis->bluezThreadRunning) {
                pThis->loop();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        BLE_LOGGER(debug, "Background BLE thread created");
    }

    BLE_LOGGER(info, "BlueZ BLE initialized in %s mode",
               bleMode == HMS_BLE_MODE_BEACON ? "BEACON" : "PERIPHERAL");
    return HMS_BLE_STATUS_SUCCESS;
}

// ==========================================================================
// bluezSetupAdapter: find adapter, power on, set alias
// ==========================================================================
HMS_BLE_Status HMS_BLE::bluezSetupAdapter() {
    int r;
    sd_bus_error error = SD_BUS_ERROR_NULL;

    // Try common BlueZ adapter paths directly (simpler than parsing GetManagedObjects)
    const char* candidatePaths[] = { "/org/bluez/hci0", "/org/bluez/hci1", "/org/bluez/hci2" };
    bool found = false;

    for (size_t i = 0; i < sizeof(candidatePaths) / sizeof(candidatePaths[0]); i++) {
        sd_bus_message* propReply = nullptr;
        r = sd_bus_get_property(bluezBus, "org.bluez", candidatePaths[i],
                                BLUEZ_ADAPTER1, "Address",
                                &error, &propReply, "");
        if (r < 0) {
            sd_bus_error_free(&error);
            continue;
        }
        sd_bus_message_unref(propReply);

        found = true;
        bluezAdapterPath = strdup(candidatePaths[i]);
        BLE_LOGGER(debug, "Found BlueZ adapter at %s", candidatePaths[i]);
        break;
    }

    if (!found) {
        BLE_LOGGER(error, "No BlueZ adapter found. Check: btmgmt info, systemctl status bluetooth");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    // Power on adapter
    r = sd_bus_set_property(bluezBus, "org.bluez", bluezAdapterPath,
                            BLUEZ_ADAPTER1, "Powered", &error, "b", 1);
    if (r < 0) {
        BLE_LOGGER(warn, "Failed to set adapter powered: %s", error.message);
        sd_bus_error_free(&error);
    }

    // Set device alias
    r = sd_bus_set_property(bluezBus, "org.bluez", bluezAdapterPath,
                            BLUEZ_ADAPTER1, "Alias", &error, "s", deviceName);
    if (r < 0) {
        BLE_LOGGER(warn, "Failed to set alias: %s", error.message);
        sd_bus_error_free(&error);
    }

    return HMS_BLE_STATUS_SUCCESS;
}

// ==========================================================================
// bluezRegisterApp: create D-Bus objects for services + characteristics
// ==========================================================================
HMS_BLE_Status HMS_BLE::bluezRegisterApp() {
    int r;
    bluezAppPath = strdup("/com/hmsble/app");

    int globalCharIdx = 0;
    for (size_t s = 0; s < serviceCount; s++) {
        char svcPath[256];
        snprintf(svcPath, sizeof(svcPath), "/com/hmsble/app/service%zu", s);
        bluezServicePaths[s] = svcPath;

        r = sd_bus_add_object_vtable(bluezBus, nullptr, svcPath,
                                     BLUEZ_GATT_SERVICE1, bluez_service_vtable,
                                     (void*)(intptr_t)s);
        if (r < 0) {
            BLE_LOGGER(error, "Failed to add service object: %s", strerror(-r));
            return HMS_BLE_STATUS_ERROR_INIT;
        }

        for (size_t c = 0; c < services[s].characteristicCount; c++) {
            char charPath[256];
            snprintf(charPath, sizeof(charPath), "/com/hmsble/app/service%zu/char%zu", s, c);
            bluezCharPaths[globalCharIdx] = charPath;

            r = sd_bus_add_object_vtable(bluezBus, nullptr, charPath,
                                         BLUEZ_GATT_CHARACTERISTIC1, bluez_char_vtable,
                                         (void*)(intptr_t)globalCharIdx);
            if (r < 0) {
                BLE_LOGGER(error, "Failed to add char object: %s", strerror(-r));
                return HMS_BLE_STATUS_ERROR_INIT;
            }

            bluezNotifEnabled[globalCharIdx] = false;
            globalCharIdx++;
        }
        BLE_LOGGER(debug, "Service %zu at %s: %zu chars", s, svcPath, services[s].characteristicCount);
    }

    // Register application with GattManager1
    r = sd_bus_call_method(bluezBus, "org.bluez", bluezAdapterPath,
                           BLUEZ_GATT_MANAGER1, "RegisterApplication",
                           nullptr, nullptr,
                           "oa{sv}", bluezAppPath, 0);
    if (r < 0) {
        BLE_LOGGER(error, "RegisterApplication failed. Ensure: btmgmt le on");
    }

    BLE_LOGGER(info, "GATT app registered (%d chars, %d services)",
               globalCharIdx, serviceCount);
    return HMS_BLE_STATUS_SUCCESS;
}

// ==========================================================================
// restartAdvertising
// ==========================================================================
void HMS_BLE::restartAdvertising() {
    if (!bluezBus || !bluezAdapterPath) return;

    // Unregister previous advertisement
    if (bluezAdvPath) {
        sd_bus_call_method(bluezBus, "org.bluez", bluezAdapterPath,
                           BLUEZ_LE_ADVERTISING_MGR1, "UnregisterAdvertisement",
                           nullptr, nullptr, "o", bluezAdvPath);
        free(bluezAdvPath);
        bluezAdvPath = nullptr;
    }

    bluezAdvPath = strdup("/com/hmsble/advertisement0");

    int r = sd_bus_add_object_vtable(bluezBus, nullptr, bluezAdvPath,
                                     BLUEZ_LE_ADVERTISEMENT1, bluez_adv_vtable, nullptr);
    if (r < 0) {
        BLE_LOGGER(error, "Failed to create adv object: %s", strerror(-r));
        free(bluezAdvPath); bluezAdvPath = nullptr;
        return;
    }

    r = sd_bus_call_method(bluezBus, "org.bluez", bluezAdapterPath,
                           BLUEZ_LE_ADVERTISING_MGR1, "RegisterAdvertisement",
                           nullptr, nullptr, "oa{sv}", bluezAdvPath, 0);
    if (r < 0) {
        BLE_LOGGER(error, "RegisterAdvertisement failed. Ensure: btmgmt le on");
    } else {
        BLE_LOGGER(info, "%s advertising started",
                   bleMode == HMS_BLE_MODE_BEACON ? "Beacon" : "Peripheral");
    }
}

// ==========================================================================
// sendDataInternal
// ==========================================================================
HMS_BLE_Status HMS_BLE::sendDataInternal(int serviceIndex, int charIndex,
                                          const uint8_t* data, size_t length) {
    if (!bluezBus) return HMS_BLE_STATUS_ERROR_NOT_CONNECTED;
    if (serviceIndex < 0 || (size_t)serviceIndex >= serviceCount ||
        charIndex   < 0 || (size_t)charIndex   >= services[serviceIndex].characteristicCount)
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;

    int globalCharIdx = 0;
    for (int s = 0; s < serviceIndex; s++) globalCharIdx += (int)services[s].characteristicCount;
    globalCharIdx += charIndex;

    if (globalCharIdx >= 0 && globalCharIdx < HMS_BLE_MAX_CHARACTERISTICS &&
        !bluezNotifEnabled[globalCharIdx])
        return HMS_BLE_STATUS_SUCCESS;

    const std::string& charPath = bluezCharPaths[globalCharIdx];
    if (charPath.empty()) return HMS_BLE_STATUS_ERROR_INVALID_CHAR;

    size_t copyLen = length > HMS_BLE_MAX_DATA_LENGTH ? HMS_BLE_MAX_DATA_LENGTH : length;
    memcpy(services[serviceIndex].data, data, copyLen);
    services[serviceIndex].dataLength = copyLen;

    char* changedProps[] = { (char*)"Value", NULL };
    sd_bus_emit_properties_changed_strv(bluezBus, charPath.c_str(),
                                        BLUEZ_GATT_CHARACTERISTIC1, changedProps);

    BLE_LOGGER(debug, "Notification sent on svc %d char %d: %d bytes",
               serviceIndex, charIndex, length);
    return HMS_BLE_STATUS_SUCCESS;
}

// ==========================================================================
// bluezCleanupApp
// ==========================================================================
void HMS_BLE::bluezCleanupApp() {
    if (!bluezBus || !bluezAdapterPath || !bluezAppPath) return;
    sd_bus_call_method(bluezBus, "org.bluez", bluezAdapterPath,
                       BLUEZ_GATT_MANAGER1, "UnregisterApplication",
                       nullptr, nullptr, "o", bluezAppPath);
}

// ==========================================================================
// stop
// ==========================================================================
void HMS_BLE::stop() {
    if (!bluezBus) return;

    if (bluezBleThread && bluezThreadRunning) {
        bluezThreadRunning = false;
        bluezBleThread->join();
        delete bluezBleThread;
        bluezBleThread = nullptr;
    }

    if (bluezAdvPath && bluezAdapterPath) {
        sd_bus_call_method(bluezBus, "org.bluez", bluezAdapterPath,
                           BLUEZ_LE_ADVERTISING_MGR1, "UnregisterAdvertisement",
                           nullptr, nullptr, "o", bluezAdvPath);
    }

    if (bluezAppPath && bluezAdapterPath && bleMode != HMS_BLE_MODE_BEACON) {
        sd_bus_call_method(bluezBus, "org.bluez", bluezAdapterPath,
                           BLUEZ_GATT_MANAGER1, "UnregisterApplication",
                           nullptr, nullptr, "o", bluezAppPath);
    }

    free(bluezAdapterPath); bluezAdapterPath = nullptr;
    free(bluezAppPath);     bluezAppPath = nullptr;
    free(bluezAdvPath);     bluezAdvPath = nullptr;

    sd_bus_flush(bluezBus);
    sd_bus_unref(bluezBus);
    bluezBus = nullptr;

    for (int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++)
        bluezNotifEnabled[i] = false;

    BLE_LOGGER(info, "BlueZ BLE stopped");
}

#endif // HMS_BLE_BLUEZ_LINUX
