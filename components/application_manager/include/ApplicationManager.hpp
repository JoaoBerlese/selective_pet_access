/**
 * @file application_manager.hpp
 * @author João Berlese
 * @brief The Core Orchestrator Finite State Machine (FSM) for the Selective Pet Access System.
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <array>
#include <cstdint>

#include "PetProximityTracker.hpp"
#include "lid_controller.hpp"
#include "telemetry_service.hpp"

namespace pet_access::core {

// Cloud-ready data structure for a single meal event
struct MealRecord {
    uint64_t start_time_us;
    uint32_t duration_ms;
    // Note: Cloud sync status will be added here in the next milestone
};

enum class SystemState {
    Standby,
    Approaching,     // Pet is near, but not eating. Good for waking up WiFi later.
    MealInProgress,  // Pet is at the feeder, Lid is open.
    Fault            // Reserved for hardware jams/errors.
};

enum class EventType { ProximityUpdate, TelemetryTick };

struct SystemEvent {
    EventType type;
    tracking::ProximityState proximity_state;  // Valid only if type == ProximityUpdate
};

class ApplicationManager final : public tracking::IProximityObserver {
public:
    /**
     * @brief Constructs the orchestrator and wires the subsystems.
     */
    ApplicationManager(
        services::LidController& lid,
        services::TelemetryService& telemetry,
        UBaseType_t task_priority,
        BaseType_t task_core = 1
    );

    ~ApplicationManager() override;

    // Rule of Five - Delete Copy/Move
    ApplicationManager(const ApplicationManager&) = delete;
    ApplicationManager& operator=(const ApplicationManager&) = delete;
    ApplicationManager(ApplicationManager&&) = delete;
    ApplicationManager& operator=(ApplicationManager&&) = delete;

    /**
     * @brief Spawns the main orchestrator FSM task.
     */
    void start();

    /**
     * @brief Implementation of IProximityObserver interface.
     * @warning Executes in the PetProximityTracker context. MUST NOT BLOCK.
     */
    void on_proximity_changed(tracking::ProximityState new_state) override;

private:
    // Subsystem References (Injected)
    services::LidController& lid_controller_;
    services::TelemetryService& telemetry_service_;

    // RTOS Task & Queue
    TaskHandle_t task_handle_{nullptr};
    QueueHandle_t event_queue_{nullptr};
    UBaseType_t task_priority_;
    BaseType_t task_core_;

    // FSM State
    SystemState current_state_{SystemState::Standby};

    // Data Model: Latest Telemetry Snapshot (for continuous cloud feed)
    services::TelemetryData latest_telemetry_{};

    uint64_t current_meal_start_us_{0};
    // Data Model: Offline Meal Ring Buffer (Zero Heap Allocation)
    static constexpr size_t MAX_OFFLINE_MEALS = 50;
    std::array<MealRecord, MAX_OFFLINE_MEALS> meal_buffer_{};
    size_t meal_buffer_head_{0};
    size_t meal_buffer_count_{0};

    // RTOS Entry and Loop
    static void task_entry(void* arg);
    void task_loop();
    // FSM Logic
    void handle_event(const SystemEvent& event);
    void process_proximity_change(tracking::ProximityState new_state);
    void update_telemetry_snapshot();

    // Helpers
    void push_meal_record(uint64_t start_us, uint32_t duration_ms);
};

}  // namespace pet_access::core