
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

#include "BeaconTypes.hpp"
#include "IBeaconScanner.hpp"

namespace pet_access::tracking {

enum class ProximityState {
    Unknown,
    Away,         // Pet is out of range or far away
    Approaching,  // Pet is detected but not close enough yet
    AtFeeder      // Pet is at the bowl (Trigger door open)
};

class PetProximityTracker {
public:
    // Dependency Injection: Takes the scanner interface and the shared FreeRTOS queue.
    // We pass the target Instance ID (myCat's collar) to filter out other beacons.
    PetProximityTracker(
        bluetooth::IBeaconScanner& scanner,
        QueueHandle_t beacon_queue,
        const std::array<uint8_t, 6> target_instance_id,
        UBaseType_t task_priority,
        BaseType_t task_core = 1  // Default to App Core (Core 1)
    );

    ~PetProximityTracker();

    // Rule of Five: Delete copy/move constructors and assignment operators
    PetProximityTracker(const PetProximityTracker&) = delete;
    PetProximityTracker& operator=(const PetProximityTracker&) = delete;
    PetProximityTracker(PetProximityTracker&&) = delete;
    PetProximityTracker& operator=(PetProximityTracker&&) = delete;

    // Spawns the RTOS task and starts the injected scanner
    void start();
    void stop();

    // Thread-safe accessors for other parts of the system (like the door motor controller)
    ProximityState getProximityState() const;
    float get_current_rssi() const;
    uint16_t get_battery_mv() const;

private:
    bluetooth::IBeaconScanner& scanner_;
    QueueHandle_t beacon_queue_;
    TaskHandle_t task_handle_{nullptr};

    std::array<uint8_t, 6> target_instance_id_;
    std::optional<bluetooth::MacAddress> known_mac_{std::nullopt};  // Mapped dynamically

    // Thread-safe state variables
    static constexpr float RSSI_WEAK_SIGNAL = -100.0f;  // Default RSSI value when no signal is detected
    std::atomic<ProximityState> proximity_state_{ProximityState::Unknown};
    std::atomic<float> current_rssi_{RSSI_WEAK_SIGNAL};  // Initialize to very weak signal
    std::atomic<uint16_t> battery_mv_{0};

    // FreeRTOS tick tracking to handle the pet walking out of range completely
    TickType_t last_seen_ticks_{0};

    // Injected RTOS parameters
    UBaseType_t task_priority_;
    BaseType_t task_core_;
    // RTOS Task Entry Point
    static void task_entry(void* param);
    void run_event_loop();

    // Processing helpers (for incoming beacon data)
    void process_beacon_event(const bluetooth::BeaconEvent& data);
    void update_ema_and_state(int8_t raw_rssi);
    void check_for_timeout();

    [[nodiscard]] bool is_target_device(const bluetooth::MacAddress& incoming_mac) const;

    // Tuning parameters for the proximity thresholds and EMA smoothing factor
    // Exponential Moving Average (EMA): new_ema = (alpha * new_value) + ((1 - alpha) * current_ema)
    static constexpr float EMA_ALPHA = 0.2f;              // Smoothing factor for RSSI (20% new, 80% history)
    static constexpr float THRESHOLD_AT_FEEDER = -60.0f;  // RSSI above this means the pet is at the bowl
    static constexpr float THRESHOLD_AWAY = -72.0f;       // RSSI below this means the pet is away
    static constexpr float HYSTERESIS_MARGIN = 3.0f;      // Hysteresis margin to prevent rapid toggling
    static constexpr uint32_t TIMEOUT_MS = 5000;          // If no beacon seen for this long, consider the pet away
};

}  // namespace pet_access::tracking