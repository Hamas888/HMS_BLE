#include "HMS_BLE.h"

#if defined(HMS_BLE_ZEPHYR_nRF)

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(HMS_BLE_ZEPHYR);

// Static connection callbacks structure
static struct bt_conn_cb conn_callbacks;

// Helper to extract MAC address from bt_conn
static void extractMacAddress(struct bt_conn *conn, uint8_t *mac) {
    if (!conn || !mac) {
        memset(mac, 0, 6);
        return;
    }
    
    const bt_addr_le_t *addr = bt_conn_get_dst(conn);
    if (addr) {
        // Copy address (Zephyr uses little-endian, but we want MSB first for display)
        for (int i = 0; i < 6; i++) {
            mac[i] = addr->a.val[5 - i];
        }
    } else {
        memset(mac, 0, 6);
    }
}

// Helper to convert hex char to byte
static uint8_t hexCharToByte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Helper to check if UUID is a 16-bit UUID (4 hex chars like "181A" or "2A6E")
static bool is16BitUUID(const char* uuidStr) {
    if (!uuidStr) return false;
    size_t len = strlen(uuidStr);
    // 16-bit UUID is 4 hex chars (e.g., "181A")
    // Also check for format "0000XXXX-0000-1000-8000-00805F9B34FB" (Bluetooth Base UUID)
    return (len == 4);
}

// Helper to convert 16-bit UUID string to uint16_t
static uint16_t parse16BitUUID(const char* uuidStr) {
    uint16_t val = 0;
    for (int i = 0; i < 4 && uuidStr[i] != '\0'; i++) {
        val = (val << 4) | hexCharToByte(uuidStr[i]);
    }
    return val;
}

// Helper to convert UUID string (e.g., "12345678-1234-1234-1234-123456789012" or "181A") to bt_uuid_128
// If it's a 16-bit UUID, we still store it as 128-bit but flag it appropriately
void HMS_BLE::convertUUIDStringToZephyr(const char* uuidStr, struct bt_uuid_128* zephyrUUID) {
    if (!uuidStr || !zephyrUUID) return;

    // Check if this is a 16-bit UUID
    if (is16BitUUID(uuidStr)) {
        // For 16-bit UUIDs, we need to expand to Bluetooth Base UUID
        // Base UUID: 00000000-0000-1000-8000-00805F9B34FB
        // 16-bit UUID goes at bytes 2-3 (big-endian) or bytes 12-13 (little-endian in val[])
        static const uint8_t bt_base_uuid[] = {
            0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        memcpy(zephyrUUID->val, bt_base_uuid, 16);
        uint16_t uuid16 = parse16BitUUID(uuidStr);
        zephyrUUID->val[12] = uuid16 & 0xFF;
        zephyrUUID->val[13] = (uuid16 >> 8) & 0xFF;
        zephyrUUID->uuid.type = BT_UUID_TYPE_128;
        return;
    }

    // Initialize base UUID structure for full 128-bit UUID
    zephyrUUID->uuid.type = BT_UUID_TYPE_128;
    
    // Parse string in reverse order (little-endian for Zephyr)
    // Format: 8-4-4-4-12 (36 chars total with hyphens)
    int byteIdx = 15;
    for (int i = 0; uuidStr[i] != '\0' && byteIdx >= 0; i++) {
        if (uuidStr[i] == '-') continue;
        
        uint8_t high = hexCharToByte(uuidStr[i]);
        i++;
        if (uuidStr[i] == '\0') break; // Should not happen for valid UUID
        uint8_t low = hexCharToByte(uuidStr[i]);
        
        zephyrUUID->val[byteIdx--] = (high << 4) | low;
    }
}

HMS_BLE_Status HMS_BLE::init() {
    int err;

    // 1. Initialize Bluetooth Stack
    err = bt_enable(NULL);
    if (err) {
        BLE_LOGGER(error, "Bluetooth init failed (err %d)", err);
        return HMS_BLE_STATUS_ERROR_INIT;
    }
    BLE_LOGGER(info, "Bluetooth initialized");

    // 2. Set device name dynamically (requires CONFIG_BT_DEVICE_NAME_DYNAMIC=y)
    err = bt_set_name(deviceName);
    if (err) {
        BLE_LOGGER(warn, "Failed to set device name (err %d)", err);
        // Not a critical error, continue
    }

    // ---- Beacon mode: lightweight init, no GATT ----
    if (bleMode == HMS_BLE_MODE_BEACON) {
        restartAdvertising();
        return HMS_BLE_STATUS_SUCCESS;
    }

    // 3. Register Connection Callbacks
    conn_callbacks.connected = zephyrConnectedCallback;
    conn_callbacks.disconnected = zephyrDisconnectedCallback;
    bt_conn_cb_register(&conn_callbacks);

    // 4. Build GATT Attributes dynamically
    if (buildGattAttributes() != 0) {
        BLE_LOGGER(error, "Failed to build GATT attributes");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    // 5. Register GATT Services (one per service, each with its own bt_gatt_service)
    {
        size_t totalAttrs = 0U;
        for (size_t s = 0U; s < serviceCount; s++) {
            zephyrGattServices[s]             = new struct bt_gatt_service();
            zephyrGattServices[s]->attrs      = zephyrGattAttrArrays[s];
            zephyrGattServices[s]->attr_count = zephyrAttrCounts[s];

            err = bt_gatt_service_register(zephyrGattServices[s]);
            if (err) {
                BLE_LOGGER(error, "Failed to register GATT service %d (err %d)", s, err);
                return HMS_BLE_STATUS_ERROR_INIT;
            }
            totalAttrs += zephyrAttrCounts[s];
            BLE_LOGGER(debug, "Service %d registered: %d attrs", s, zephyrAttrCounts[s]);
        }
        BLE_LOGGER(info, "GATT: %d services, %d total attributes registered", serviceCount, totalAttrs);
    }

    // 5. Start Advertising
    restartAdvertising();

    // 6. Start background task if requested (similar to ESP32 FreeRTOS task)
    if (backgroundProcess) {
        // Allocate stack dynamically
        zephyrBleThreadStack = (k_thread_stack_t*)k_malloc(K_THREAD_STACK_LEN(HMS_BLE_BACKGROUND_PROCESS_STACK_SIZE));
        if (zephyrBleThreadStack) {
            zephyrBleThreadId = k_thread_create(
                &zephyrBleThread,
                zephyrBleThreadStack,
                HMS_BLE_BACKGROUND_PROCESS_STACK_SIZE,
                zephyrBleTask,
                NULL, NULL, NULL,
                HMS_BLE_BACKGROUND_PROCESS_PRIORITY,
                0,
                K_NO_WAIT
            );
            k_thread_name_set(zephyrBleThreadId, "HMS_BLE_Task");
            BLE_LOGGER(debug, "Background BLE task created");
        } else {
            BLE_LOGGER(warn, "Failed to allocate stack for background task");
        }
    }

    return HMS_BLE_STATUS_SUCCESS;
}

void HMS_BLE::restartAdvertising() {
    int err;

    bt_le_adv_stop();

    // ---- Beacon mode: raw AD/SD, non-connectable ----
    if (bleMode == HMS_BLE_MODE_BEACON) {
        static struct bt_data beaconAd[HMS_BLE_MAX_AD_DATA];
        uint8_t beaconAdCount = 0;
        static struct bt_data beaconSd[HMS_BLE_MAX_AD_DATA];
        uint8_t beaconSdCount = 0;

        // Parse raw AD buffer into bt_data entries
        size_t off = 0;
        while (off < beaconADLen && beaconAdCount < HMS_BLE_MAX_AD_DATA) {
            uint8_t len = beaconAD[off];
            if (len < 1 || off + 1 + len > beaconADLen) break;
            beaconAd[beaconAdCount].type     = beaconAD[off + 1];
            beaconAd[beaconAdCount].data_len = len - 1;
            beaconAd[beaconAdCount].data     = &beaconAD[off + 2];
            beaconAdCount++;
            off += 1 + len;
        }

        // Parse raw SD buffer into bt_data entries
        off = 0;
        while (off < beaconSDLen && beaconSdCount < HMS_BLE_MAX_AD_DATA) {
            uint8_t len = beaconSD[off];
            if (len < 1 || off + 1 + len > beaconSDLen) break;
            beaconSd[beaconSdCount].type     = beaconSD[off + 1];
            beaconSd[beaconSdCount].data_len = len - 1;
            beaconSd[beaconSdCount].data     = &beaconSD[off + 2];
            beaconSdCount++;
            off += 1 + len;
        }

        // Auto-append device name to scan response so phones show a recognizable name
        if (deviceName && strlen(deviceName) > 0) {
            size_t nameLen = strlen(deviceName);
            if (nameLen > 29) nameLen = 29;
            // Check if name is already present in user's SD
            bool nameFound = false;
            for (uint8_t i = 0; i < beaconSdCount; i++) {
                if (beaconSd[i].type == BT_DATA_NAME_COMPLETE ||
                    beaconSd[i].type == BT_DATA_NAME_SHORTENED) {
                    nameFound = true;
                    break;
                }
            }
            if (!nameFound && beaconSdCount < HMS_BLE_MAX_AD_DATA) {
                static uint8_t nameBuf[30];
                memcpy(nameBuf, deviceName, nameLen);
                beaconSd[beaconSdCount].type     = BT_DATA_NAME_COMPLETE;
                beaconSd[beaconSdCount].data_len = nameLen;
                beaconSd[beaconSdCount].data     = nameBuf;
                beaconSdCount++;
            }
        }

        k_msleep(50);
        static const struct bt_le_adv_param beaconParam =
            BT_LE_ADV_PARAM_INIT(0, BT_GAP_ADV_FAST_INT_MIN_2,
                                 BT_GAP_ADV_FAST_INT_MAX_2, NULL);
        err = bt_le_adv_start(
            &beaconParam,
            beaconAd, beaconAdCount,
            beaconSdCount > 0 ? beaconSd : NULL, beaconSdCount
        );
        if (err) {
            BLE_LOGGER(error, "Beacon advertising failed to start (err %d)", err);
        } else {
            BLE_LOGGER(info, "Beacon advertising started (%d AD, %d SD entries)",
                       beaconAdCount, beaconSdCount);
        }
        return;
    }

    // Collect service UUIDs to advertise
    // Use advertisedServices[] if set, otherwise iterate all registered services
    size_t advCount = (advertisedServiceCount > 0) ? advertisedServiceCount : serviceCount;
    const char** advUUIDs = (advertisedServiceCount > 0)
        ? advertisedServices
        : nullptr; // will iterate services[] directly

    // Separate 16-bit and 128-bit UUIDs
    uint8_t uuid16_le[HMS_BLE_MAX_SERVICES * 2]; // packed 2 bytes each
    int n16 = 0;
    int first128 = -1;

    for (size_t i = 0; i < advCount; i++) {
        const char* uuid;
        if (advertisedServiceCount > 0) {
            uuid = advUUIDs[i];
        } else {
            uuid = services[i].service.uuid.c_str();
        }
        if (is16BitUUID(uuid)) {
            uint16_t val = parse16BitUUID(uuid);
            uuid16_le[n16 * 2]     = (uint8_t)(val & 0xFF);
            uuid16_le[n16 * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
            n16++;
        } else if (first128 < 0) {
            // Store index for 128-bit lookup
            for (size_t s = 0; s < serviceCount; s++) {
                if (strcmp(services[s].service.uuid.c_str(), uuid) == 0) {
                    if (zephyrServiceIs16[s]) {
                        // already handled as 16-bit above — skip
                    } else {
                        first128 = (int)s;
                    }
                    break;
                }
            }
        }
    }

    // AD: Flags + service UUID(s)
    static const uint8_t ad_flags_val = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
    struct bt_data ad_buf[2];
    int ad_count = 1;
    ad_buf[0] = BT_DATA(BT_DATA_FLAGS, &ad_flags_val, sizeof(ad_flags_val));

    if (n16 > 0) {
        // Pack all 16-bit service UUIDs into one AD entry
        ad_buf[1] = BT_DATA(BT_DATA_UUID16_ALL, uuid16_le, (uint8_t)(n16 * 2));
        ad_count = 2;
    } else if (first128 >= 0) {
        // Advertise the first 128-bit service
        ad_buf[1] = BT_DATA(BT_DATA_UUID128_ALL, zephyrServiceUUIDArr[first128].val, 16);
        ad_count = 2;
    }

    // SD: Device name + optional manufacturer data + external 128-bit service UUIDs
    // (external = advertised UUIDs not registered as GATT services, e.g. the
    //  SMP/DFU service added via addAdvertisedService()).
    uint8_t sd_buf[8][34];
    struct bt_data sd[8];
    int sd_count = 0;

    sd[sd_count].type     = BT_DATA_NAME_COMPLETE;
    sd[sd_count].data_len = (uint8_t)strlen(deviceName);
    sd[sd_count].data     = (const uint8_t*)deviceName;
    sd_count++;

    if (manufacturerDataSet) {
        static uint8_t mfg[8];
        mfg[0] = manufacturerData.manufacturer_id[0];
        mfg[1] = manufacturerData.manufacturer_id[1];
        memcpy(&mfg[2], manufacturerData.data.data(), 6);
        sd[sd_count].type     = BT_DATA_MANUFACTURER_DATA;
        sd[sd_count].data_len = sizeof(mfg);
        sd[sd_count].data     = mfg;
        sd_count++;
    }

    // Append 128-bit UUIDs for advertised services that are NOT registered
    // GATT services (e.g. SMP/DFU). Registered 128-bit services are already
    // handled in the AD packet via zephyrServiceUUIDArr[].
    for (size_t i = 0; i < advCount; i++) {
        const char* uuid;
        if (advertisedServiceCount > 0) {
            uuid = advUUIDs[i];
        } else {
            uuid = services[i].service.uuid.c_str();
        }
        if (is16BitUUID(uuid)) {
            continue; // handled in AD
        }
        if (findServiceIndex(uuid) >= 0) {
            continue; // registered service — handled in AD
        }
        if (sd_count >= 8) {
            break;
        }
        struct bt_uuid_128* u128 = (struct bt_uuid_128*)&sd_buf[sd_count];
        convertUUIDStringToZephyr(uuid, u128);
        sd[sd_count].type     = BT_DATA_UUID128_ALL;
        sd[sd_count].data_len = 16;
        sd[sd_count].data     = u128->val;
        sd_count++;
    }

    #ifndef BT_LE_ADV_OPT_CONN
    #define BT_LE_ADV_OPT_CONN BIT(0)
    #endif

    static const struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_CONN,
        BT_GAP_ADV_FAST_INT_MIN_2,
        BT_GAP_ADV_FAST_INT_MAX_2,
        NULL
    );

    k_msleep(50);

    err = bt_le_adv_start(&param, ad_buf, ad_count, sd, sd_count);
    if (err) {
        BLE_LOGGER(error, "Advertising failed to start (err %d)", err);
        return;
    }
    BLE_LOGGER(info, "Advertising started");
}

void HMS_BLE::stop() {
    bt_le_adv_stop();
    if (bleMode == HMS_BLE_MODE_BEACON) return;        // beacon: no connection/thread to clean up
    
    // Stop background thread if running
    if (backgroundProcess && zephyrBleThreadId) {
        k_thread_abort(zephyrBleThreadId);
        zephyrBleThreadId = NULL;
        if (zephyrBleThreadStack) {
            k_free(zephyrBleThreadStack);
            zephyrBleThreadStack = NULL;
        }
    }
    
    // Note: Zephyr doesn't support full bt_disable() on all controllers
    if (zephyrConnection) {
        bt_conn_disconnect(zephyrConnection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
}

int HMS_BLE::buildGattAttributes() {
    size_t globalCharIdx = 0U;

    for (size_t s = 0U; s < serviceCount; s++) {
        // Count attrs for this service: 1 primary decl + 2 per char + optional CCC/CUD
        size_t svcAttrs = 1U;
        for (size_t c = 0U; c < services[s].characteristicCount; c++) {
            svcAttrs += 2U; // char decl + value
            if (services[s].characteristics[c].properties & (HMS_BLE_PROPERTY_NOTIFY | HMS_BLE_PROPERTY_INDICATE)) {
                svcAttrs += 1U; // CCC
            }
            if (!services[s].characteristics[c].name.empty()) {
                svcAttrs += 1U; // CUD
            }
        }

        zephyrGattAttrArrays[s] = new struct bt_gatt_attr[svcAttrs];
        zephyrAttrCounts[s]     = svcAttrs;
        size_t attrIdx          = 0U;

        // Primary service declaration
        const char *svcUuidStr = services[s].service.uuid.c_str();
        zephyrServiceIs16[s]   = is16BitUUID(svcUuidStr);
        if (zephyrServiceIs16[s]) {
            zephyrServiceUUID16Vals[s]          = parse16BitUUID(svcUuidStr);
            zephyrServiceUUID16Arr[s].uuid.type = BT_UUID_TYPE_16;
            zephyrServiceUUID16Arr[s].val       = zephyrServiceUUID16Vals[s];
            zephyrGattAttrArrays[s][attrIdx++]  = BT_GATT_PRIMARY_SERVICE(&zephyrServiceUUID16Arr[s]);
        } else {
            convertUUIDStringToZephyr(svcUuidStr, &zephyrServiceUUIDArr[s]);
            zephyrGattAttrArrays[s][attrIdx++] = BT_GATT_PRIMARY_SERVICE(&zephyrServiceUUIDArr[s]);
        }

        // Characteristics
        for (size_t c = 0U; c < services[s].characteristicCount; c++) {
            const HMS_BLE_Characteristic &ch = services[s].characteristics[c];

            bool char16 = is16BitUUID(ch.uuid.c_str());
            zephyrCharUUID16[globalCharIdx] = char16;
            if (char16) {
                uint16_t u16 = parse16BitUUID(ch.uuid.c_str());
                zephyrCharUUID16Structs[globalCharIdx].uuid.type = BT_UUID_TYPE_16;
                zephyrCharUUID16Structs[globalCharIdx].val       = u16;
            } else {
                convertUUIDStringToZephyr(ch.uuid.c_str(), &zephyrCharUUIDs[globalCharIdx]);
            }

            uint8_t props = 0U;
            uint8_t perms = 0U;
            if (ch.properties & HMS_BLE_PROPERTY_READ)     { props |= BT_GATT_CHRC_READ;    perms |= BT_GATT_PERM_READ; }
            if (ch.properties & HMS_BLE_PROPERTY_WRITE)    { props |= BT_GATT_CHRC_WRITE;   perms |= BT_GATT_PERM_WRITE; }
            if (ch.properties & HMS_BLE_PROPERTY_NOTIFY)   { props |= BT_GATT_CHRC_NOTIFY; }
            if (ch.properties & HMS_BLE_PROPERTY_INDICATE) { props |= BT_GATT_CHRC_INDICATE; }

            zephyrCharDeclarations[globalCharIdx].uuid = char16
                ? (const struct bt_uuid *)&zephyrCharUUID16Structs[globalCharIdx]
                : (const struct bt_uuid *)&zephyrCharUUIDs[globalCharIdx];
            zephyrCharDeclarations[globalCharIdx].value_handle = 0U;
            zephyrCharDeclarations[globalCharIdx].properties   = props;

            // Characteristic declaration attribute
            zephyrGattAttrArrays[s][attrIdx++] = BT_GATT_ATTRIBUTE(
                BT_UUID_GATT_CHRC, BT_GATT_PERM_READ,
                bt_gatt_attr_read_chrc, NULL,
                &zephyrCharDeclarations[globalCharIdx]
            );

            // Characteristic value attribute
            const struct bt_uuid *charValueUUID = char16
                ? (const struct bt_uuid *)&zephyrCharUUID16Structs[globalCharIdx]
                : &zephyrCharUUIDs[globalCharIdx].uuid;

            zephyrGattAttrArrays[s][attrIdx++] = BT_GATT_ATTRIBUTE(
                charValueUUID, perms,
                zephyrReadCallback, zephyrWriteCallback,
                (void *)(uintptr_t)globalCharIdx
            );

            // CCC for notify/indicate
            if (props & (BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE)) {
                memset(&zephyrCccObjects[globalCharIdx], 0, sizeof(ZephyrCCC));
                zephyrCccObjects[globalCharIdx].cfg_changed = zephyrCccChangedCallback;
                zephyrCccObjects[globalCharIdx].cfg_write   = NULL;
                zephyrCccObjects[globalCharIdx].cfg_match   = NULL;
                zephyrGattAttrArrays[s][attrIdx++] = BT_GATT_ATTRIBUTE(
                    BT_UUID_GATT_CCC,
                    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                    bt_gatt_attr_read_ccc, bt_gatt_attr_write_ccc,
                    &zephyrCccObjects[globalCharIdx]
                );
            }

            // CUD (user description)
            if (!ch.name.empty()) {
                strncpy(zephyrCharUserDesc[globalCharIdx], ch.name.c_str(),
                        sizeof(zephyrCharUserDesc[globalCharIdx]) - 1U);
                zephyrCharUserDesc[globalCharIdx][sizeof(zephyrCharUserDesc[globalCharIdx]) - 1U] = '\0';
                zephyrGattAttrArrays[s][attrIdx++] = BT_GATT_ATTRIBUTE(
                    BT_UUID_GATT_CUD, BT_GATT_PERM_READ,
                    bt_gatt_attr_read_cud, NULL,
                    zephyrCharUserDesc[globalCharIdx]
                );
            }

            // Store reverse mapping: globalCharIdx → (serviceIndex, localCharIndex)
            zephyrCharServiceMap[globalCharIdx] = s;
            zephyrCharLocalMap[globalCharIdx]   = c;

            globalCharIdx++;
        }

        BLE_LOGGER(debug, "Service %d: built %d attrs for %d chars",
                   s, svcAttrs, services[s].characteristicCount);
    }

    BLE_LOGGER(debug, "buildGattAttributes: %d services, %d total chars", serviceCount, globalCharIdx);

    // Reset notification tracking for all characteristics
    memset(zephyrNotifEnabled, 0, sizeof(zephyrNotifEnabled));
    return 0;
}

void HMS_BLE::zephyrConnectedCallback(struct bt_conn *conn, uint8_t err) {
    if (err) {
        BLE_LOGGER(error, "Connection failed (err %u)", err);
        return;
    }

    if (instance) {
        instance->zephyrConnection = bt_conn_ref(conn);
        instance->bleConnected = true;
        instance->oldConnected = true; // To prevent immediate disconnect logic
        
        BLE_LOGGER(info, "Device Connected");
        
        if (instance->connectionCallback) {
            uint8_t mac[6];
            extractMacAddress(conn, mac);
            instance->connectionCallback(true, mac);
        }
    }
}

void HMS_BLE::zephyrDisconnectedCallback(struct bt_conn *conn, uint8_t reason) {
    BLE_LOGGER(info, "Device Disconnected (reason %u)", reason);

    if (instance) {
        uint8_t mac[6];
        extractMacAddress(conn, mac);

        // Reset notification tracking for next connection
        memset(instance->zephyrNotifEnabled, 0, sizeof(instance->zephyrNotifEnabled));

        if (instance->zephyrConnection) {
            bt_conn_unref(instance->zephyrConnection);
            instance->zephyrConnection = NULL;
        }
        instance->bleConnected = false;
        
        if (instance->connectionCallback) {
            instance->connectionCallback(false, mac);
        }
    }
}

ssize_t HMS_BLE::zephyrReadCallback(
    struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset
) {
    int globalCharIdx = (int)((intptr_t)attr->user_data);

    if (instance && instance->readCallback && globalCharIdx >= 0) {
        int svcIdx = instance->zephyrCharServiceMap[globalCharIdx];

        size_t outLen = HMS_BLE_MAX_DATA_LENGTH;
        uint8_t tempBuf[HMS_BLE_MAX_DATA_LENGTH] = {0};
        uint8_t mac[6];
        extractMacAddress(conn, mac);

        instance->readCallback(
            instance->services[svcIdx].service.uuid.c_str(),
            instance->services[svcIdx].characteristics[instance->zephyrCharLocalMap[globalCharIdx]].uuid.c_str(),
            tempBuf, &outLen, mac
        );

        return bt_gatt_attr_read(conn, attr, buf, len, offset, tempBuf, outLen);
    }

    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

ssize_t HMS_BLE::zephyrWriteCallback(
    struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags
) {
    int globalCharIdx = (int)((intptr_t)attr->user_data);

    if (instance && globalCharIdx >= 0) {
        int svcIdx = instance->zephyrCharServiceMap[globalCharIdx];
        int localIdx = instance->zephyrCharLocalMap[globalCharIdx];

        // Store in per-service buffer
        size_t copyLen = (len > HMS_BLE_MAX_DATA_LENGTH) ? HMS_BLE_MAX_DATA_LENGTH : len;
        memcpy(instance->services[svcIdx].data, buf, copyLen);
        instance->services[svcIdx].dataLength = copyLen;
        instance->services[svcIdx].received = true;

        // Also store in legacy shared buffer for backward compatibility
        if (svcIdx == 0) {
            memcpy(instance->data, buf, copyLen);
            instance->dataLength = copyLen;
            instance->received = true;
        }

        BLE_LOGGER(debug, "Write received on service %d char %d, len %d", svcIdx, localIdx, len);

        if (instance->writeCallback) {
            uint8_t mac[6];
            extractMacAddress(conn, mac);
            instance->writeCallback(
                instance->services[svcIdx].service.uuid.c_str(),
                instance->services[svcIdx].characteristics[localIdx].uuid.c_str(),
                (const uint8_t*)buf, len, mac
            );
        }
    }

    return len;
}

void HMS_BLE::zephyrCccChangedCallback(const struct bt_gatt_attr *attr, uint16_t value) {
    if (!instance) return;

    int globalCharIdx = -1;
    for (int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++) {
        if (attr->user_data == &instance->zephyrCccObjects[i]) {
            globalCharIdx = i;
            break;
        }
    }

    if (globalCharIdx >= 0) {
        int svcIdx   = instance->zephyrCharServiceMap[globalCharIdx];
        int localIdx = instance->zephyrCharLocalMap[globalCharIdx];
        bool enabled = (value == BT_GATT_CCC_NOTIFY || value == BT_GATT_CCC_INDICATE);
        instance->zephyrNotifEnabled[globalCharIdx] = enabled;
        BLE_LOGGER(debug, "Notifications %s for service %d char %d",
                   enabled ? "enabled" : "disabled", svcIdx, localIdx);

        if (instance->notifyCallback) {
            uint8_t mac[6];
            if (instance->zephyrConnection) {
                extractMacAddress(instance->zephyrConnection, mac);
            } else {
                memset(mac, 0, 6);
            }
            instance->notifyCallback(
                instance->services[svcIdx].service.uuid.c_str(),
                instance->services[svcIdx].characteristics[localIdx].uuid.c_str(),
                enabled, mac);
        }
    }
}

void HMS_BLE::zephyrBleTask(void* p1, void* p2, void* p3) {
    HMS_BLE* pThis = HMS_BLE::instance;
    if(!pThis) return;
    
    while(true) {
        pThis->loop();
        k_msleep(10);
    }
}

HMS_BLE_Status HMS_BLE::sendDataInternal(int serviceIndex, int charIndex, const uint8_t* data, size_t length) {
    if (!bleConnected || !zephyrConnection) {
        return HMS_BLE_STATUS_ERROR_NOT_CONNECTED;
    }
    if (serviceIndex < 0 || (size_t)serviceIndex >= serviceCount ||
        charIndex   < 0 || (size_t)charIndex   >= services[serviceIndex].characteristicCount) {
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    // Walk the per-service attr array to find this characteristic's value attribute.
    // Layout per service: [primary_decl, (char_decl + char_value [+ CCC] [+ CUD]) * N]
    size_t attrIdx = 1U; // skip primary service declaration
    for (int c = 0; c < charIndex; c++) {
        attrIdx += 2U; // char decl + value
        if (services[serviceIndex].characteristics[c].properties & (HMS_BLE_PROPERTY_NOTIFY | HMS_BLE_PROPERTY_INDICATE)) {
            attrIdx += 1U;
        }
        if (!services[serviceIndex].characteristics[c].name.empty()) {
            attrIdx += 1U;
        }
    }
    attrIdx += 1U; // skip char decl → now at char value

    if (attrIdx >= zephyrAttrCounts[serviceIndex]) {
        BLE_LOGGER(error, "Attr index %d out of range for service %d (max %d)", attrIdx, serviceIndex, zephyrAttrCounts[serviceIndex]);
        return HMS_BLE_STATUS_ERROR_INVALID_CHAR;
    }

    // Compute global char index to check subscription state
    int globalCharIdx = 0;
    for (int s = 0; s < serviceIndex; s++) {
        globalCharIdx += services[s].characteristicCount;
    }
    globalCharIdx += charIndex;

    // Only notify if client has subscribed (CCC configured)
    if (globalCharIdx >= 0 && globalCharIdx < HMS_BLE_MAX_CHARACTERISTICS && !zephyrNotifEnabled[globalCharIdx]) {
        return HMS_BLE_STATUS_SUCCESS; // silently skip — no subscriber
    }

    int err = bt_gatt_notify(zephyrConnection, &zephyrGattAttrArrays[serviceIndex][attrIdx], data, length);
    if (err) {
        BLE_LOGGER(warn, "bt_gatt_notify failed (err %d)", err);
        return HMS_BLE_STATUS_ERROR_SEND;
    }
    return HMS_BLE_STATUS_SUCCESS;
}

#endif // HMS_BLE_ZEPHYR_nRF