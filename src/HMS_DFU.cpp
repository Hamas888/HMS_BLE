#include "HMS_DFU.h"

#if defined(HMS_BLE_ZEPHYR_nRF) && defined(CONFIG_HMS_BLE_DFU)

/* Pointer used by the Zephyr backend to dispatch MCUmgr events to the app
 * callback. Only one HMS_DFU instance is supported per device. */
static HMS_DFU *dfu_instance = nullptr;

HMS_DFU *hms_dfu_get_instance(void)
{
    return dfu_instance;
}

HMS_DFU::HMS_DFU() : ble(nullptr), active(false) {
    dfu_instance = this;
    BLE_LOGGER(debug, "HMS_DFU instance created");
}

HMS_DFU::~HMS_DFU() {
    if (active) {
        stop();
    }
    if (dfu_instance == this) {
        dfu_instance = nullptr;
    }
    BLE_LOGGER(debug, "HMS_DFU instance destroyed");
}

HMS_DFU_Status HMS_DFU::init(HMS_BLE* bleInstance) {
    if (!bleInstance) {
        BLE_LOGGER(error, "HMS_DFU init: null HMS_BLE instance");
        return HMS_DFU_STATUS_ERROR_INIT;
    }
    ble = bleInstance;
    BLE_LOGGER(debug, "HMS_DFU attached to HMS_BLE instance");
    return HMS_DFU_STATUS_SUCCESS;
}

HMS_DFU_Status HMS_DFU::start() {
    if (!ble) {
        BLE_LOGGER(error, "HMS_DFU start: not initialized (call init() first)");
        return HMS_DFU_STATUS_ERROR_INIT;
    }
    if (active) {
        BLE_LOGGER(debug, "HMS_DFU start: already active");
        return HMS_DFU_STATUS_SUCCESS;
    }

    /* Add the SMP service UUID to the advertised services so DFU clients
     * (nRF Connect app, mcumgr) can discover and connect. */
    const char* smpUuid = HMS_DFU_SMP_SERVICE_UUID_STR;
    if (ble->addAdvertisedService(smpUuid) != HMS_BLE_STATUS_SUCCESS) {
        BLE_LOGGER(error, "HMS_DFU start: failed to advertise SMP service");
        return HMS_DFU_STATUS_ERROR_INIT;
    }
    active = true;
    BLE_LOGGER(info, "HMS_DFU active — SMP service advertised, device OTA-updatable");
    return HMS_DFU_STATUS_SUCCESS;
}

HMS_DFU_Status HMS_DFU::stop() {
    if (!ble) {
        BLE_LOGGER(error, "HMS_DFU stop: not initialized");
        return HMS_DFU_STATUS_ERROR_INIT;
    }
    if (!active) {
        BLE_LOGGER(debug, "HMS_DFU stop: already inactive");
        return HMS_DFU_STATUS_SUCCESS;
    }

    ble->removeAdvertisedService(HMS_DFU_SMP_SERVICE_UUID_STR);
    active = false;
    BLE_LOGGER(info, "HMS_DFU inactive — SMP service no longer advertised");
    return HMS_DFU_STATUS_SUCCESS;
}

bool HMS_DFU::isActive() const {
    return active;
}

void HMS_DFU::setEventCallback(HMS_DFU_EventCallback cb) {
    eventCb = cb;
    BLE_LOGGER(debug, "HMS_DFU event callback %s",
               cb ? "registered" : "cleared");
}

void HMS_DFU::notifyEvent(HMS_DFU_Event event, int detail) {
    BLE_LOGGER(debug, "HMS_DFU event %d, detail %d", (int)event, detail);
    if (eventCb) {
        eventCb(event, detail);
    }
}

#endif /* HMS_BLE_ZEPHYR_nRF && CONFIG_HMS_BLE_DFU */
