/*
 ============================================================================================================================================
 * File:        HMS_DFU.h
 * Author:      Hamas Saeed
 * Version:     Rev_1.0.0
 * Date:        Aug 5 2026
 * Brief:       Optional DFU (firmware update) feature for HMS_BLE.
 *              Provides OTA update capability over BLE using the SMP protocol
 *              (MCUmgr) with MCUboot swap-based rollback.
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

#ifndef HMS_DFU_H
#define HMS_DFU_H

#include "HMS_BLE.h"

#if defined(HMS_BLE_ZEPHYR_nRF) && defined(CONFIG_HMS_BLE_DFU)

/* Zephyr MCUmgr SMP GATT service UUID (128-bit) */
#define HMS_DFU_SMP_SERVICE_UUID_STR "8D53DC1D-1DB7-4CD3-868B-8A527460AA84"

/* Wait the CONFIG_HMS_BLE_DFU_AUTO_CONFIRM_DELAY_SEC delay, then confirm the
 * running image so MCUboot does not revert it (rollback gate). With delay=0
 * the image is never auto-confirmed — a "test" upgrade reverts on reset. */
void hms_dfu_auto_confirm_loop(void);

typedef enum {
    HMS_DFU_STATUS_SUCCESS    = 0,
    HMS_DFU_STATUS_ERROR_INIT = -1,
    HMS_DFU_STATUS_ERROR_BUSY = -2,
    HMS_DFU_STATUS_ERROR_UNSUPPORTED = -3,
} HMS_DFU_Status;

/* DFU lifecycle events delivered to the application callback. */
typedef enum {
    HMS_DFU_EVENT_UPLOAD_STARTED = 0,
    HMS_DFU_EVENT_UPLOAD_PROGRESS,   /* detail: "pct" (0-100) */
    HMS_DFU_EVENT_UPLOAD_COMPLETE,   /* image pending test */
    HMS_DFU_EVENT_IMAGE_CONFIRMED,
    HMS_DFU_EVENT_RESET_REQUESTED,
} HMS_DFU_Event;

/* Callback the application can register to be informed of DFU activity.
 *   event  — one of HMS_DFU_EVENT_*
 *   detail — event-specific payload:
 *            HMS_DFU_EVENT_UPLOAD_PROGRESS: percentage (0-100)
 *            others: 0 */
typedef std::function<void(HMS_DFU_Event event, int detail)> HMS_DFU_EventCallback;

/*!
 * \brief Optional DFU feature attached to an HMS_BLE instance.
 *
 * On Zephyr/nRF, the SMP (MCUmgr) GATT service is registered by the
 * Zephyr MCUmgr BT transport when HMS_DFU.conf is included in the build.
 * This class handles advertising integration (adding the SMP service UUID
 * to the advertisement) and runtime start/stop of DFU discoverability.
 *
 * After start(), call hms_dfu_auto_confirm_loop() (Zephyr backend) to wait
 * the configured delay and confirm the running image — the rollback gate.
 *
 * When CONFIG_HMS_BLE_DEBUG is enabled (which turns on the MCUmgr
 * notification hooks), the application can register an event callback via
 * setEventCallback() to receive DFU lifecycle updates.
 */
class HMS_DFU {
public:
    HMS_DFU();
    ~HMS_DFU();

    /* Attach to an HMS_BLE instance (call after ble.begin(), before start()). */
    HMS_DFU_Status init(HMS_BLE* ble);

    /* Start advertising with the SMP service UUID (DFU reachable). */
    HMS_DFU_Status start();

    /* Stop advertising the SMP service UUID (DFU unreachable, app services stay). */
    HMS_DFU_Status stop();

    /* true when DFU service is currently advertised. */
    bool isActive() const;

    /* Register an application callback for DFU lifecycle events. */
    void setEventCallback(HMS_DFU_EventCallback cb);

    /* Invoked by the backend on every DFU event (internal). */
    void notifyEvent(HMS_DFU_Event event, int detail);

private:
    HMS_BLE*               ble;
    bool                   active;
    HMS_DFU_EventCallback  eventCb;
};

/* Internal — returns the current HMS_DFU instance (used by the Zephyr
 * backend to dispatch MCUmgr DFU events to the app callback). */
HMS_DFU *hms_dfu_get_instance(void);

#endif /* HMS_BLE_ZEPHYR_nRF && CONFIG_HMS_BLE_DFU */

#endif /* HMS_DFU_H */
