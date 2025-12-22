#include "HMS_BLE.h"

#if defined(HMS_BLE_CONTROLLER_TEMPLATE)

void HMS_BLE::stop() {
    // Platform-specific BLE stop implementation
}

HMS_BLE_Status HMS_BLE::init() {
    // Platform-specific BLE initialization implementation
    return HMS_BLE_STATUS_OK;
}

void HMS_BLE::restartAdvertising() {
    // Platform-specific BLE restart advertising implementation
}
#endif // HMS_BLE_ARDUINO_ESP32