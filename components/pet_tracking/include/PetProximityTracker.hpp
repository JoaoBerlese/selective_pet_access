
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

// Unified telemetry payload. Returned by value for lock-free thread safety.
struct PetTelemetry {
    ProximityState state;
    float rssi_ema;
    uint16_t battery_mv;
    TickType_t state_changed_at_ticks;
    TickType_t rssi_updated_at_ticks;
    TickType_t battery_updated_at_ticks;
};

// Observer interface for applications that want to react to proximity changes.
class IProximityObserver {
public:
    virtual ~IProximityObserver() = default;

    // CRITICAL RTOS WARNING:
    // This method executes in the PetProximityTracker task context.
    // Implementations MUST NOT block. Use xTaskNotify or FreeRTOS queues
    // to wake up your application task and return immediately.
    virtual void on_proximity_changed(ProximityState new_state) = 0;
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
        IProximityObserver* observer = nullptr,
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

    // Thread-safe accessors for the current proximity state and telemetry data
    [[nodiscard]] PetTelemetry get_telemetry() const;

private:
    bluetooth::IBeaconScanner& scanner_;
    QueueHandle_t beacon_queue_;
    TaskHandle_t task_handle_{nullptr};

    IProximityObserver* observer_{nullptr};

    std::array<uint8_t, 6> target_instance_id_;
    std::optional<bluetooth::MacAddress> known_mac_{std::nullopt};  // Mapped dynamically

    // Thread-safe state variables
    std::atomic<ProximityState> proximity_state_{ProximityState::Unknown};
    std::atomic<float> current_rssi_{RSSI_WEAK_SIGNAL};  // Initialize to very weak signal
    std::atomic<uint16_t> battery_mv_{0};
    // Timestamps for telemetry freshness (in FreeRTOS ticks)
    std::atomic<TickType_t> rssi_updated_ticks_{0};  // Used also for last seen time to detect "Away" status
    std::atomic<TickType_t> battery_updated_ticks_{0};
    std::atomic<TickType_t> state_changed_ticks_{0};

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

    void set_proximity_state(ProximityState new_state);
    static constexpr const char* state_to_string(ProximityState state);

    [[nodiscard]] bool is_target_device(const bluetooth::MacAddress& incoming_mac) const;

    // Tuning parameters for the proximity thresholds and EMA smoothing factor
    // Exponential Moving Average (EMA): new_ema = (alpha * new_value) + ((1 - alpha) * current_ema)
    static constexpr float EMA_ALPHA = 0.35f;             // Smoothing factor for RSSI (30% new, 70% history)
    static constexpr float THRESHOLD_AT_FEEDER = -66.0f;  // RSSI above this means the pet is at the bowl
    static constexpr float THRESHOLD_AWAY = -75.0f;       // RSSI below this means the pet is away
    static constexpr float RSSI_WEAK_SIGNAL = -100.0f;    // Default RSSI value when no signal is detected
    static constexpr float HYSTERESIS_MARGIN = 0.75f;     // Hysteresis margin to prevent rapid toggling
    static constexpr uint32_t TIMEOUT_MS = 5000;          // If no beacon seen for this long, consider the pet away
};

}  // namespace pet_access::tracking