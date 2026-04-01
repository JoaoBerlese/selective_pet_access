#include "NimbleScanner.hpp"

#include <esp_log.h>

#include <cstring>

// NimBLE core includes (C-style API)
#include "host/ble_hs.h"     // <--- Defines ble_hs_cfg and core host types
#include "host/util/util.h"  // <--- Defines ble_store_util_status_rr
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

namespace pet_access::bluetooth {

static const char* TAG = "NimbleScanner";

// Initialize static instance pointer (used only for the sync callback)
NimbleScanner* NimbleScanner::instance_ = nullptr;

// =============================================================================
// Lifecycle & Hardware Initialization
// =============================================================================

NimbleScanner::NimbleScanner(QueueHandle_t event_queue) : event_queue_(event_queue) {
    // Create a binary semaphore for synchronization with the NimBLE stack
    sync_semaphore_ = xSemaphoreCreateBinary();
    if (sync_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create synchronization semaphore");
        abort();
    }
    instance_ = this;  // Set the static instance pointer for callback access
}

NimbleScanner::~NimbleScanner() {
    if (initialized_) {
        nimble_port_stop();
        nimble_port_deinit();
    }
    if (sync_semaphore_ != nullptr) {
        vSemaphoreDelete(sync_semaphore_);
    }
    instance_ = nullptr;  // Clear the static instance pointer
}

esp_err_t NimbleScanner::initialize() {
    if (initialized_) {
        ESP_LOGE(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize the NimBLE host stack
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE port: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure NimBLE Host callbacks
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Start the NimBLE host task on a separate FreeRTOS core (Pinned to Core 0 via sdkconfig)
    nimble_port_freertos_init(nimble_host_task);

    // Wait for the stack to signal that it's ready (via the sync callback)
    if (xSemaphoreTake(sync_semaphore_, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout waiting for NimBLE stack to synchronize");
        return ESP_ERR_TIMEOUT;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "NimBLE stack synchronized and ready.");
    return ESP_OK;
}

// =============================================================================
// Public API
// =============================================================================

void NimbleScanner::start() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot start scanner: NimBLE stack not initialized");
        return;
    }

    // Configure the Coexistence-friendly scanning parameters
    struct ble_gap_disc_params disc_params{};
    disc_params.itvl = NIMBLE_SCAN_INTERVAL;
    disc_params.window = NIMBLE_SCAN_WINDOW;
    disc_params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;  // Accept all advertisements
    disc_params.limited = 0;                              // General discovery mode
    disc_params.passive = 1;                              // Passive scan only (saves radio TX time)
    // Report all advertisements, even duplicates. We WANT duplicate TLM packets to update battery/history
    disc_params.filter_duplicates = 0;

    ESP_LOGI(TAG, "Starting passive BLE scan...");

    // Pass 'this' as the cb_arg to route the C-callback back to this object instance
    int rc = ble_gap_disc(
        BLE_OWN_ADDR_PUBLIC,  // Use the public MAC address
        BLE_HS_FOREVER,       // Scan indefinitely until stopped
        &disc_params,
        gap_event_callback,
        this  // cb_arg for event callback
    );

    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start BLE scan: %d", rc);
    }
}

void NimbleScanner::stop() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot stop scanner: NimBLE stack not initialized");
        return;
    }

    int rc = ble_gap_disc_cancel();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop BLE scan: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE scan stopped.");
    }
}

// =============================================================================
// Private Functions
// =============================================================================

// --- C Callbacks bridges ---

void NimbleScanner::on_stack_sync() {
    ESP_LOGI(TAG, "NimBLE host and controller synced.");
    if ((instance_ != nullptr) && (instance_->sync_semaphore_ != nullptr)) {
        // Signal the waiting initialize() method that the stack is ready
        xSemaphoreGive(instance_->sync_semaphore_);
    }
}

void NimbleScanner::on_stack_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE stack reset: reason=%d", reason);
    // In a production environment, we would implement an auto-recovery state machine here.
}

void NimbleScanner::nimble_host_task(void* param) {
    ESP_LOGI(TAG, "NimBLE host task started on FreeRTOS core %d", xPortGetCoreID());
    // This will block indefinitely (until nimble_port_stop() is called), processing BLE events
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int NimbleScanner::gap_event_callback(struct ble_gap_event* event, void* arg) {
    // Cast the argument back to our C++ instance
    NimbleScanner* scanner = static_cast<NimbleScanner*>(arg);

    switch (event->type) {
        case BLE_GAP_EVENT_DISC:  // Advertisement discovered
            scanner->handle_discovery_event(event);
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            // ESP_LOGI(TAG, "BLE scan complete: reason=%d", event->disc_complete.reason);
            break;
        default:
            // For debugging: Log other events that we are not explicitly handling
            ESP_LOGD(TAG, "Received unhandled GAP event: type=%d", event->type);
            break;
    }
    return 0;  // Return success to indicate we've handled the event
}

// --- The Core RTOS Boundary ---

void NimbleScanner::handle_discovery_event(const struct ble_gap_event* event) {
    // We are currently executing in the NimBLE Host Task context (High Priority).
    // Rule #1: Do not block.
    // Rule #2: Do not use dynamic memory allocation (new/malloc).

    if (event->type != BLE_GAP_EVENT_DISC) {
        ESP_LOGE(TAG, "handle_discovery_event called with non-discovery event");
        return;
    }

    // Construct a BeaconEvent from the raw NimBLE discovery data
    BeaconEvent beacon_event{};

    // Reverse the MAC address (BLE sends it over the air in Little-Endian)
    for (size_t i = 0; i < 6; ++i) {
        beacon_event.mac[i] = event->disc.addr.val[5 - i];  // Copy MAC address
    }

    beacon_event.rssi = event->disc.rssi;  // RSSI in dBm
    beacon_event.payload_length = std::min(
        static_cast<size_t>(event->disc.length_data), MAX_ADV_PAYLOAD_SIZE
    );  // Truncate if payload exceeds max

    // Copy the raw bytes from NimBLE's temporary buffer into our trivially copyable struct
    std::memcpy(beacon_event.payload.data(), event->disc.data, beacon_event.payload_length);  // Copy payload

    // Push the struct across the RTOS boundary to the Application Task.
    // timeout = 0 means if the Application Task is busy and the queue is full, we drop the packet.
    if (xQueueSend(event_queue_, &beacon_event, 0) != pdTRUE) {
        // ESP_LOGW is too slow to put in a high-frequency radio callback.
        // In production, increment a static uint32_t dropped_packet_counter here instead.
    }
}

}  // namespace pet_access::bluetooth
