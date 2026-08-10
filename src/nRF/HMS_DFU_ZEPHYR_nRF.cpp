#include "HMS_DFU.h"

#if defined(HMS_BLE_ZEPHYR_nRF) && defined(CONFIG_HMS_BLE_DFU)

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>

/* Zephyr log module (app-controlled via CONFIG_LOG_* / module-level settings). */
LOG_MODULE_REGISTER(HMS_DFU_ZEPHYR);

/* Optional ChronoLog logger for DFU events — created only when the app
 * enables HMS_BLE_DEBUG (HMS_BLE.h pulls ChronoLog in that case). */
#if HMS_BLE_DEBUG_ENABLED
static ChronoLogger dfuLogger("HMS_DFU");
#define DFU_LOG_DEBUG(...) dfuLogger.debug(__VA_ARGS__)
#define DFU_LOG_INFO(...)  dfuLogger.info(__VA_ARGS__)
#define DFU_LOG_WARN(...)  dfuLogger.warn(__VA_ARGS__)
#define DFU_LOG_ERROR(...) dfuLogger.error(__VA_ARGS__)
#else
#define DFU_LOG_DEBUG(...) do {} while (0)
#define DFU_LOG_INFO(...)  do {} while (0)
#define DFU_LOG_WARN(...)  do {} while (0)
#define DFU_LOG_ERROR(...) do {} while (0)
#endif

/* Dispatch a DFU event to the application callback (if registered). */
static void dfu_dispatch(HMS_DFU_Event event, int detail)
{
    HMS_DFU *dfu = hms_dfu_get_instance();
    if (dfu) {
        dfu->notifyEvent(event, detail);
    }
}

/* Upload progress state. */
static uint32_t dfu_last_pct = 0;

/* Called for every MCUmgr event of interest (upload chunks, test/confirm,
 * reset). This is how the application log shows DFU activity. */
static enum mgmt_cb_return dfu_mgmt_callback(uint32_t event_id,
                                             enum mgmt_cb_return prev_status,
                                             int32_t *rc, uint16_t *group,
                                             bool *abort_more, void *data,
                                             size_t data_size)
{
    (void)prev_status;
    (void)rc;
    (void)group;
    (void)abort_more;
    (void)data_size;

    switch (event_id) {
    case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
        dfu_last_pct = 0;
        DFU_LOG_INFO("DFU upload started");
        LOG_INF("DFU upload started");
        dfu_dispatch(HMS_DFU_EVENT_UPLOAD_STARTED, 0);
        break;
    case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: {
        struct img_mgmt_upload_check *check = static_cast<struct img_mgmt_upload_check *>(data);
        if (check && check->req && check->action) {
            /* req->off is the byte offset within the image; action->size is
             * the total image size (available in the upload check data). */
            size_t off = check->req->off;
            size_t total = (size_t)check->action->size;
            if (total > 0 && off <= total) {
                uint32_t pct = (uint32_t)((off * 100) / total);
                /* Log on every 10% step (and 100%), not per chunk. */
                if ((pct >= 100 || (pct % 10) == 0) && pct != dfu_last_pct) {
                    dfu_last_pct = pct;
                    DFU_LOG_DEBUG("DFU upload progress: %u%% (%zu/%zu bytes)",
                                  pct, off, total);
                    LOG_DBG("DFU upload progress: %u%% (%zu/%zu bytes)",
                            pct, off, total);
                    dfu_dispatch(HMS_DFU_EVENT_UPLOAD_PROGRESS, (int)pct);
                }
            }
        }
        break;
    }
    case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
        dfu_last_pct = 0;
        DFU_LOG_INFO("DFU upload complete -- image pending test");
        LOG_INF("DFU upload complete -- image pending test");
        dfu_dispatch(HMS_DFU_EVENT_UPLOAD_COMPLETE, 0);
        break;
    case MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED:
        DFU_LOG_INFO("DFU image confirmed");
        LOG_INF("DFU image confirmed");
        dfu_dispatch(HMS_DFU_EVENT_IMAGE_CONFIRMED, 0);
        break;
    case MGMT_EVT_OP_OS_MGMT_RESET:
        DFU_LOG_INFO("DFU reset command received -- rebooting");
        LOG_INF("DFU reset command received -- rebooting");
        dfu_dispatch(HMS_DFU_EVENT_RESET_REQUESTED, 0);
        break;
    default:
        break;
    }
    return MGMT_CB_OK;
}

static struct mgmt_callback dfu_callback = {
    .callback = dfu_mgmt_callback,
    .event_id = MGMT_EVT_OP_IMG_MGMT_DFU_STARTED |
                MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK |
                MGMT_EVT_OP_IMG_MGMT_DFU_PENDING |
                MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED |
                MGMT_EVT_OP_OS_MGMT_RESET,
};

/* Called from main after the DFU feature has been set up.
 * Registers the DFU event callback, waits the configured delay, then
 * confirms the running image so MCUboot does not revert it on the next
 * reset. This is the rollback gate: with delay = 0, auto-confirm is
 * skipped and a "test" upgrade reverts on every reset unless the client
 * confirms it manually. */
void hms_dfu_auto_confirm_loop(void)
{
    mgmt_callback_register(&dfu_callback);

#if CONFIG_HMS_BLE_DFU_AUTO_CONFIRM_DELAY_SEC > 0
    DFU_LOG_INFO("DFU: waiting %d s before confirming image...",
                 CONFIG_HMS_BLE_DFU_AUTO_CONFIRM_DELAY_SEC);
    LOG_INF("DFU: waiting %d s before confirming image...",
            CONFIG_HMS_BLE_DFU_AUTO_CONFIRM_DELAY_SEC);
    k_sleep(K_SECONDS(CONFIG_HMS_BLE_DFU_AUTO_CONFIRM_DELAY_SEC));

    if (boot_is_img_confirmed()) {
        DFU_LOG_INFO("DFU: image already confirmed");
        LOG_INF("DFU: image already confirmed");
        return;
    }

    int rc = boot_write_img_confirmed();
    if (rc) {
        DFU_LOG_ERROR("DFU: image confirm failed (%d)", rc);
        LOG_ERR("DFU: image confirm failed (%d)", rc);
    } else {
        DFU_LOG_INFO("DFU: image confirmed OK");
        LOG_INF("DFU: image confirmed OK");
    }
#else
    DFU_LOG_WARN("DFU: auto-confirm disabled -- 'test' upgrades will revert on reset");
    DFU_LOG_WARN("DFU: confirm manually via: mcumgr image confirm");
    LOG_WRN("DFU: auto-confirm disabled -- 'test' upgrades will revert on reset");
    LOG_WRN("DFU: confirm manually via: mcumgr image confirm");
#endif
}

#endif /* HMS_BLE_ZEPHYR_nRF && CONFIG_HMS_BLE_DFU */
