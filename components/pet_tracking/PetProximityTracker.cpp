#include "PetProximityTracker.hpp"

#include <esp_log.h>

#include "EddystoneParser.hpp"

namespace pet_access::tracking {

static const char* TAG = "PetProximityTracker";

// =============================================================================
// Lifecycle Management
// =============================================================================

PetProximityTracker::PetProximityTracker(
    bluetooth::IBeaconScanner& scanner,
    QueueHandle_t beacon_queue,
    const std::array<uint8_t, 6> target_instance_id,
    UBaseType_t task_priority,
    IProximityObserver* observer,
    BaseType_t task_core
)
    : scanner_(scanner)
    , beacon_queue_(beacon_queue)
    , observer_(observer)
    , target_instance_id_(target_instance_id)
    , task_priority_(task_priority)
    , task_core_(task_core) {}

PetProximityTracker::~PetProximityTracker() {
    stop();
}

// =============================================================================
// Task Management
// =============================================================================

void PetProximityTracker::start() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Task already running");
        return;
    }

    // Pin the business logic strictly to Core 1 (App Core) to avoid stealing cycles
    // from the NimBLE stack running on Core 0.
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "PetProximityTrackerTask",
        4096,
        this,
        task_priority_,  // <--- Injected Priority
        &task_handle_,
        task_core_  // <--- Injected Core
    );

    if (ret == pdPASS) {
        scanner_.start();
        ESP_LOGI(TAG, "Pet tracking started. Listening for target collar...");
    } else {
        ESP_LOGE(TAG, "Failed to create tracker task.");
        abort();
    }
}

void PetProximityTracker::stop() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
        scanner_.stop();
        ESP_LOGI(TAG, "Pet tracking stopped.");
    }
}

// =============================================================================
// State Accessors
// =============================================================================

PetTelemetry PetProximityTracker::get_telemetry() const {
    // Memory Order Acquire ensures we get the most up-to-date values flushed
    // from Core 1's cache to Core 0 (if the motor controller runs there).
    return {
        proximity_state_.load(std::memory_order_acquire),
        current_rssi_.load(std::memory_order_acquire),
        battery_mv_.load(std::memory_order_acquire),
        state_changed_ticks_.load(std::memory_order_acquire),
        rssi_updated_ticks_.load(std::memory_order_acquire),
        battery_updated_ticks_.load(std::memory_order_acquire)
    };
}

// =============================================================================
// RTOS Task Entry Point
// =============================================================================

void PetProximityTracker::task_entry(void* param) {
    auto* self = static_cast<PetProximityTracker*>(param);
    self->run_event_loop();
}

void PetProximityTracker::run_event_loop() {
    // This loop continuously processes incoming beacon data and updates the proximity state.
    // It also handles timeouts to detect when the pet walks out of range.
    bluetooth::BeaconEvent beacon_event;

    while (true) {
        // Block until a beacon event arrives or timeout occurs (for checking away status)
        if (xQueueReceive(beacon_queue_, &beacon_event, pdMS_TO_TICKS(1000)) == pdPASS) {
            process_beacon_event(beacon_event);
        }

        // After processing any incoming events, check if we need to transition to "Away" due to timeout
        check_for_timeout();
    }
}

// =============================================================================
// Event Processing Helpers
// =============================================================================

void PetProximityTracker::process_beacon_event(const bluetooth::BeaconEvent& event) {
    // Zero-cost parse. If it's not Eddystone, this returns nullopt almost instantly.
    auto parsed_opt = bluetooth::EddystoneParser::parse(event.get_payload_span());
    if (!parsed_opt.has_value()) {
        return;
    }

    const auto& parsed = parsed_opt.value();

    // ==========================================
    // DEEP DEBUG TRACE (Temporary)
    // ==========================================
    // if (parsed.frame_type == bluetooth::EddystoneFrameType::UID) {
    //     ESP_LOGI(
    //         TAG,
    //         "[DEBUG] UID Frame | MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d",
    //         event.mac[0],
    //         event.mac[1],
    //         event.mac[2],
    //         event.mac[3],
    //         event.mac[4],
    //         event.mac[5],
    //         event.rssi
    //     );
    // } else if (parsed.frame_type == bluetooth::EddystoneFrameType::TLM) {
    //     ESP_LOGI(
    //         TAG,
    //         "[DEBUG] TLM Frame | MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d | Batt: %u mV | Temp: %.1f C",
    //         event.mac[0],
    //         event.mac[1],
    //         event.mac[2],
    //         event.mac[3],
    //         event.mac[4],
    //         event.mac[5],
    //         event.rssi,
    //         parsed.battery_mv,
    //         parsed.temperature_c
    //     );
    // }
    // ==========================================

    // Dynamic MAC Association (UID Frame Only)
    if (parsed.frame_type == bluetooth::EddystoneFrameType::UID) {
        // Check if the Instance ID matches our target pet collar
        if (parsed.instance_id == target_instance_id_) {
            if (!known_mac_.has_value()) {
                known_mac_ = event.mac;  // Dynamically learn the MAC address for this collar
                ESP_LOGI(
                    TAG,
                    "Learned target collar MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                    event.mac[0],
                    event.mac[1],
                    event.mac[2],
                    event.mac[3],
                    event.mac[4],
                    event.mac[5]
                );
            }
        }
    }
    // Telemetry Extraction (TLM Frame Only)
    else if (parsed.frame_type == bluetooth::EddystoneFrameType::TLM) {
        // Use the Pragmatic Mask to associate TLM frames with the known MAC address from the UID frame
        if (is_target_device(event.mac)) {
            battery_mv_.store(parsed.battery_mv, std::memory_order_relaxed);
            battery_updated_ticks_.store(xTaskGetTickCount(), std::memory_order_release);
            // We could also log or use the temperature data if needed: parsed.temperature_c
        }
    }
    // RSSI Processing (ALL Frames from Target)
    // If we have learned the MAC, and THIS packet came from that MAC, update the EMA!
    if (is_target_device(event.mac)) {
        update_ema_and_state(event.rssi);
    }
}

void PetProximityTracker::update_ema_and_state(int8_t raw_rssi) {
    // Update last seen time on every valid RSSI update
    rssi_updated_ticks_.store(xTaskGetTickCount(), std::memory_order_release);

    // Calculate the Exponential Moving Average (EMA) of RSSI for smoother proximity estimation
    float current_ema = current_rssi_.load(std::memory_order_relaxed);
    if (current_ema == RSSI_WEAK_SIGNAL) {
        // First valid RSSI reading, initialize EMA
        current_ema = static_cast<float>(raw_rssi);
    } else {
        // Subsequent readings, update EMA
        current_ema = (EMA_ALPHA * static_cast<float>(raw_rssi)) + ((1.0f - EMA_ALPHA) * current_ema);
    }
    current_rssi_.store(current_ema, std::memory_order_relaxed);

    // Apply Schmitt Trigger hysteresis for state management
    ProximityState current_state = proximity_state_.load(std::memory_order_acquire);
    ProximityState next = current_state;

    // State transition logic with hysteresis margins to prevent rapid toggling
    switch (current_state) {
        case ProximityState::Unknown:
        case ProximityState::Away:
            if (current_ema >= (THRESHOLD_AWAY + HYSTERESIS_MARGIN)) {
                next = ProximityState::Approaching;
            }
            break;
        case ProximityState::Approaching:
            if (current_ema >= THRESHOLD_AT_FEEDER) {
                next = ProximityState::AtFeeder;
            } else if (current_ema < (THRESHOLD_AWAY - HYSTERESIS_MARGIN)) {
                next = ProximityState::Away;
            }
            break;
        case ProximityState::AtFeeder:
            if (current_ema < (THRESHOLD_AT_FEEDER - HYSTERESIS_MARGIN)) {
                if (!feeder_exit_pending_) {
                    feeder_exit_pending_ = true;
                    feeder_exit_pending_since_ = xTaskGetTickCount();
                } else if (
                    (xTaskGetTickCount() - feeder_exit_pending_since_) >= pdMS_TO_TICKS(FEEDER_EXIT_DEBOUNCE_MS)
                ) {
                    next = ProximityState::Approaching;
                }
            } else {
                feeder_exit_pending_ = false;
            }
            break;
        default:
            break;
    }

    // ==========================================
    // DEEP DEBUG TRACE (Temporary)
    // ==========================================
    // ESP_LOGI(TAG, "RSSI EMA: %.2f dBm", current_ema);
    // ==========================================

    // If the state has changed, update it and log the transition
    set_proximity_state(next);
}

void PetProximityTracker::check_for_timeout() {
    if ((proximity_state_.load(std::memory_order_acquire) == ProximityState::Away) ||
        (proximity_state_.load(std::memory_order_acquire) == ProximityState::Unknown)) {
        return;  // Already in a non-present state, no need to check for timeout
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = now - rssi_updated_ticks_.load(std::memory_order_acquire);

    if (elapsed > pdMS_TO_TICKS(TIMEOUT_MS)) {
        // Consider the pet "Away" if we haven't seen any beacon for a certain timeout period
        set_proximity_state(ProximityState::Away);
        current_rssi_.store(RSSI_WEAK_SIGNAL, std::memory_order_relaxed);  // Reset RSSI to weak signal
    }
}

bool PetProximityTracker::is_target_device(const bluetooth::MacAddress& incoming_mac) const {
    if (!known_mac_.has_value()) {
        return false;
    }
    const auto& base_mac = known_mac_.value();

    // Check if the first 5 bytes (OUI + upper NIC) match exactly
    bool prefix_match = std::equal(base_mac.begin(), base_mac.begin() + 5, incoming_mac.begin());

    // Check if the last byte (lower NIC) is within a small range to allow for minor variations
    // This accounts for potential MAC address randomization or slight variations in the beacon's MAC.
    bool suffix_match = ((base_mac[5] & 0xF0) == (incoming_mac[5] & 0xF0));  // Match upper 4 bits of the last byte

    return prefix_match && suffix_match;
}

// =============================================================================
// State Management Helpers
// =============================================================================

void PetProximityTracker::set_proximity_state(ProximityState new_state) {
    ProximityState current_state = proximity_state_.load(std::memory_order_acquire);

    if (current_state != new_state) {
        feeder_exit_pending_ = false;
        proximity_state_.store(new_state, std::memory_order_release);
        state_changed_ticks_.store(xTaskGetTickCount(), std::memory_order_release);

        ESP_LOGI(
            TAG,
            "Proximity state changed: %s -> %s (RSSI EMA: %.2f dBm)",
            state_to_string(current_state),
            state_to_string(new_state),
            current_rssi_.load(std::memory_order_relaxed)
        );

        // Fire the callback to notify the Orchestrator
        if (observer_ != nullptr) {
            observer_->on_proximity_changed(new_state);
        }
    }
}

constexpr const char* PetProximityTracker::state_to_string(ProximityState state) {
    switch (state) {
        case ProximityState::Unknown:
            return "Unknown";
        case ProximityState::Away:
            return "Away";
        case ProximityState::Approaching:
            return "Approaching";
        case ProximityState::AtFeeder:
            return "AtFeeder";
        default:
            return "Invalid";
    }
}

}  // namespace pet_access::tracking