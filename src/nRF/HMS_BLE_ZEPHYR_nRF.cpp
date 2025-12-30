#include "HMS_BLE.h"

#if defined(HMS_BLE_ZEPHYR_nRF)

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(HMS_BLE_ZEPHYR, LOG_LEVEL_DBG);

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

    // 3. Register Connection Callbacks
    conn_callbacks.connected = zephyrConnectedCallback;
    conn_callbacks.disconnected = zephyrDisconnectedCallback;
    bt_conn_cb_register(&conn_callbacks);

    // 4. Build GATT Attributes dynamically
    if (buildGattAttributes() != 0) {
        BLE_LOGGER(error, "Failed to build GATT attributes");
        return HMS_BLE_STATUS_ERROR_INIT;
    }

    // 5. Register GATT Service
    // We need to allocate the service structure dynamically or use a member variable
    zephyrGattService = new struct bt_gatt_service();
    zephyrGattService->attrs = zephyrGattAttrs;
    zephyrGattService->attr_count = zephyrAttrCount;
    
    err = bt_gatt_service_register(zephyrGattService);
    if (err) {
        BLE_LOGGER(error, "Failed to register GATT service (err %d)", err);
        return HMS_BLE_STATUS_ERROR_INIT;
    }
    BLE_LOGGER(info, "GATT Service registered with %d attributes", zephyrAttrCount);

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
    
    // Stop any existing advertising
    bt_le_adv_stop();

    // Check if service UUID is a standard 16-bit UUID
    bool use16BitUUID = is16BitUUID(serviceUUID);
    uint16_t uuid16 = use16BitUUID ? parse16BitUUID(serviceUUID) : 0;
    
    // For 16-bit UUIDs, advertise as 16-bit for proper recognition by apps
    // Store the 16-bit UUID in little-endian format for advertising
    uint8_t uuid16_le[2] = { (uint8_t)(uuid16 & 0xFF), (uint8_t)((uuid16 >> 8) & 0xFF) };
    
    // Define Advertising Data
    // Flags: General Discoverable, BR/EDR Not Supported
    // Service UUID: Use 16-bit or 128-bit based on the service UUID format
    struct bt_data ad[2];
    ad[0] = (struct bt_data)BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));
    
    if (use16BitUUID) {
        ad[1] = (struct bt_data)BT_DATA(BT_DATA_UUID16_ALL, uuid16_le, 2);
    } else {
        ad[1] = (struct bt_data)BT_DATA(BT_DATA_UUID128_ALL, zephyrServiceUUID.val, 16);
    }

    // Define Scan Response Data (Device Name + Manufacturer Data if set)
    struct bt_data sd[2];
    int sd_count = 1;
    sd[0] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE, deviceName, (uint8_t)strlen(deviceName));
    
    // Add manufacturer data if set
    if (manufacturerDataSet) {
        static uint8_t mfg_data[8]; // 2 bytes company ID + up to 6 bytes data
        mfg_data[0] = manufacturerData.manufacturer_id[0];
        mfg_data[1] = manufacturerData.manufacturer_id[1];
        memcpy(&mfg_data[2], manufacturerData.data.data(), 6);
        sd[1] = (struct bt_data)BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, 8);
        sd_count = 2;
    }

    // Start Advertising
    // Note: BT_LE_ADV_CONN uses BT_LE_ADV_OPT_CONNECTABLE which is deprecated in newer Zephyr
    // We use BT_LE_ADV_OPT_CONN which is the replacement (or BIT(0) if not defined).
    
    #ifndef BT_LE_ADV_OPT_CONN
    #define BT_LE_ADV_OPT_CONN BIT(0)
    #endif

    static const struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_CONN,
        BT_GAP_ADV_FAST_INT_MIN_2,
        BT_GAP_ADV_FAST_INT_MAX_2,
        NULL
    );
    
    err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, sd_count);
    if (err) {
        BLE_LOGGER(error, "Advertising failed to start (err %d)", err);
        return;
    }
    BLE_LOGGER(info, "Advertising started");
}

void HMS_BLE::stop() {
    bt_le_adv_stop();
    
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
    // Calculate total attributes needed:
    // 1 for Service Declaration
    // For each characteristic:
    //   1 for Characteristic Declaration
    //   1 for Characteristic Value
    //   1 for CCC (if Notify/Indicate is enabled)
    //   1 for CUD (User Description) if name is present
    
    size_t totalAttrs = 1; // Service itself
    for (size_t i = 0; i < characteristicCount; i++) {
        totalAttrs += 2; // Decl + Value
        if (characteristics[i].properties & (HMS_BLE_PROPERTY_NOTIFY | HMS_BLE_PROPERTY_INDICATE)) {
            totalAttrs += 1; // CCC
        }
        if (!characteristics[i].name.empty()) {
            totalAttrs += 1; // CUD
        }
    }

    // Allocate attributes array
    zephyrGattAttrs = new struct bt_gatt_attr[totalAttrs];
    zephyrAttrCount = totalAttrs;
    size_t attrIdx = 0;

    // 1. Service Declaration
    // Check if service UUID is 16-bit standard UUID
    zephyrServiceUUID16 = is16BitUUID(serviceUUID);
    if (zephyrServiceUUID16) {
        zephyrServiceUUID16Val = parse16BitUUID(serviceUUID);
        zephyrServiceUUID16Struct.uuid.type = BT_UUID_TYPE_16;
        zephyrServiceUUID16Struct.val = zephyrServiceUUID16Val;
        zephyrGattAttrs[attrIdx++] = BT_GATT_PRIMARY_SERVICE(&zephyrServiceUUID16Struct);
    } else {
        convertUUIDStringToZephyr(serviceUUID, &zephyrServiceUUID);
        zephyrGattAttrs[attrIdx++] = BT_GATT_PRIMARY_SERVICE(&zephyrServiceUUID);
    }

    // 2. Characteristics
    for (size_t i = 0; i < characteristicCount; i++) {
        // Check if characteristic UUID is 16-bit standard UUID
        zephyrCharUUID16[i] = is16BitUUID(characteristics[i].uuid.c_str());
        
        if (zephyrCharUUID16[i]) {
            uint16_t uuid16 = parse16BitUUID(characteristics[i].uuid.c_str());
            zephyrCharUUID16Structs[i].uuid.type = BT_UUID_TYPE_16;
            zephyrCharUUID16Structs[i].val = uuid16;
        } else {
            convertUUIDStringToZephyr(characteristics[i].uuid.c_str(), &zephyrCharUUIDs[i]);
        }

        // Determine Properties and Permissions
        uint8_t props = 0;
        uint8_t perms = 0;

        if (characteristics[i].properties & HMS_BLE_PROPERTY_READ) {
            props |= BT_GATT_CHRC_READ;
            perms |= BT_GATT_PERM_READ;
        }
        if (characteristics[i].properties & HMS_BLE_PROPERTY_WRITE) {
            props |= BT_GATT_CHRC_WRITE;
            perms |= BT_GATT_PERM_WRITE;
        }
        if (characteristics[i].properties & HMS_BLE_PROPERTY_NOTIFY) {
            props |= BT_GATT_CHRC_NOTIFY;
        }
        if (characteristics[i].properties & HMS_BLE_PROPERTY_INDICATE) {
            props |= BT_GATT_CHRC_INDICATE;
        }

        // Characteristic Declaration
        // We must use a struct bt_gatt_chrc for user_data, not just the properties byte.
        // The read_chrc callback expects this struct.
        // Use 16-bit or 128-bit UUID based on what we detected
        if (zephyrCharUUID16[i]) {
            zephyrCharDeclarations[i].uuid = (const struct bt_uuid *)&zephyrCharUUID16Structs[i];
        } else {
            zephyrCharDeclarations[i].uuid = (const struct bt_uuid *)&zephyrCharUUIDs[i];
        }
        zephyrCharDeclarations[i].value_handle = 0; // Stack will fix this up
        zephyrCharDeclarations[i].properties = props;

        zephyrGattAttrs[attrIdx++] = BT_GATT_ATTRIBUTE(
            BT_UUID_GATT_CHRC,
            BT_GATT_PERM_READ,
            bt_gatt_attr_read_chrc,
            NULL,
            &zephyrCharDeclarations[i] // Pass the struct pointer
        );
        
        // Characteristic Value - use 16-bit or 128-bit UUID
        const struct bt_uuid *charValueUUID;
        if (zephyrCharUUID16[i]) {
            charValueUUID = (const struct bt_uuid *)&zephyrCharUUID16Structs[i];
        } else {
            charValueUUID = &zephyrCharUUIDs[i].uuid;
        }
        
        zephyrGattAttrs[attrIdx] = BT_GATT_ATTRIBUTE(
            charValueUUID,
            perms,
            zephyrReadCallback,
            zephyrWriteCallback,
            (void*)i // Store index as user_data to identify characteristic in callbacks
        );
        attrIdx++;

        // CCC (Client Characteristic Configuration)
        if (props & (BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE)) {
            // Manually construct CCC attribute because BT_GATT_CCC macro is for static definition
            // and creates a local array which fails in assignment.
            // We use our pre-allocated zephyrCccCfg array.
            
            // Initialize the CCC config for this characteristic
            // We use our custom ZephyrCCC struct to match Zephyr's internal layout
            // Zero out the struct first
            memset(&zephyrCccObjects[i], 0, sizeof(ZephyrCCC));
            
            // Set the callbacks
            zephyrCccObjects[i].cfg_changed = zephyrCccChangedCallback;
            zephyrCccObjects[i].cfg_write = NULL; 
            zephyrCccObjects[i].cfg_match = NULL;

            zephyrGattAttrs[attrIdx] = BT_GATT_ATTRIBUTE(
                BT_UUID_GATT_CCC,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                bt_gatt_attr_read_ccc,
                bt_gatt_attr_write_ccc,
                &zephyrCccObjects[i] // Pass the pointer to the custom struct which mimics _bt_gatt_ccc
            );
            
            attrIdx++;
        }
        
        // CUD (Characteristic User Description)
        if (!characteristics[i].name.empty()) {
            // Copy name to storage
            strncpy(zephyrCharUserDesc[i], characteristics[i].name.c_str(), sizeof(zephyrCharUserDesc[i]) - 1);
            zephyrCharUserDesc[i][sizeof(zephyrCharUserDesc[i]) - 1] = '\0';
            
            zephyrGattAttrs[attrIdx] = BT_GATT_ATTRIBUTE(
                BT_UUID_GATT_CUD,
                BT_GATT_PERM_READ,
                bt_gatt_attr_read_cud,
                NULL,
                zephyrCharUserDesc[i] // Pass the string pointer
            );
            attrIdx++;
        }
    }

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
    struct bt_conn *conn, const struct bt_gatt_attr *attr,void *buf, uint16_t len, uint16_t offset
) {
    // Retrieve characteristic index from user_data
    int charIndex = (int)((intptr_t)attr->user_data);
    
    if (instance && instance->readCallback) {
        // Call user callback to update data if needed
        // Note: This is a blocking call in Zephyr context
        
        // We don't have a per-characteristic data buffer in the main class except 'data'
        // The main class design seems to share 'data' buffer or expects user to handle it?
        // Looking at HMS_BLE.h, 'data' is a single buffer.
        // But 'sendData' takes a charUUID.
        
        // For read, we'll return the last known data or 0 if not set?
        // The ESP32 implementation uses NimBLECharacteristic which holds value.
        // Here we don't hold value in the attribute.
        
        // We'll return 0 bytes for now unless we add value storage per characteristic
        // or invoke the callback to get data.
        // The signature of readCallback is: void(const char* charUUID, uint8_t* data, size_t* length, ...)
        
        size_t outLen = HMS_BLE_MAX_DATA_LENGTH;
        uint8_t tempBuf[HMS_BLE_MAX_DATA_LENGTH] = {0};
        uint8_t mac[6];
        extractMacAddress(conn, mac);
        
        instance->readCallback(
            instance->characteristics[charIndex].uuid.c_str(), 
            tempBuf, &outLen, mac
        );
                               
        return bt_gatt_attr_read(conn, attr, buf, len, offset, tempBuf, outLen);
    }

    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

ssize_t HMS_BLE::zephyrWriteCallback(
    struct bt_conn *conn, const struct bt_gatt_attr *attr,const void *buf, uint16_t len, uint16_t offset, uint8_t flags
) {
    int charIndex = (int)((intptr_t)attr->user_data);
    
    if (instance) {
        // Copy data to instance buffer (limited by max length)
        size_t copyLen = (len > HMS_BLE_MAX_DATA_LENGTH) ? HMS_BLE_MAX_DATA_LENGTH : len;
        memcpy(instance->data, buf, copyLen);
        instance->dataLength = copyLen;
        instance->received = true; // Flag for loop()
        
        BLE_LOGGER(debug, "Write received on char %d, len %d", charIndex, len);

        if (instance->writeCallback) {
            uint8_t mac[6];
            extractMacAddress(conn, mac);
            instance->writeCallback(
                instance->characteristics[charIndex].uuid.c_str(), 
                (const uint8_t*)buf, len, mac
            );
        }
    }
    
    return len;
}

void HMS_BLE::zephyrCccChangedCallback(const struct bt_gatt_attr *attr, uint16_t value) {
    // Find which characteristic this CCC belongs to
    // This is hard because attr->user_data points to the CCC cfg array, not the char index
    // We can iterate our attributes to find the match
    
    if (!instance) return;
    
    int charIndex = -1;
    // Simple search: check address of CCC cfg
    for (int i = 0; i < HMS_BLE_MAX_CHARACTERISTICS; i++) {
        if (attr->user_data == &instance->zephyrCccObjects[i]) {
            charIndex = i;
            break;
        }
    }
    
    if (charIndex >= 0) {
        bool enabled = (value == BT_GATT_CCC_NOTIFY || value == BT_GATT_CCC_INDICATE);
        BLE_LOGGER(info, "Notifications %s for char %d", enabled ? "enabled" : "disabled", charIndex);
        
        if (instance->notifyCallback) {
            // CCC callback doesn't provide the connection directly
            // Use the stored connection if available
            uint8_t mac[6];
            if (instance->zephyrConnection) {
                extractMacAddress(instance->zephyrConnection, mac);
            } else {
                memset(mac, 0, 6);
            }
            instance->notifyCallback(instance->characteristics[charIndex].uuid.c_str(), 
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

#endif // HMS_BLE_ZEPHYR_nRF