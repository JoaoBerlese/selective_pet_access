#include "ApplicationManager.hpp"

#include <esp_log.h>
#include <esp_timer.h>

namespace pet_access::core {

static const char* TAG = "AppManager";

// =============================================================================
// Lifecycle Management
// =============================================================================

ApplicationManager::ApplicationManager(
    services::LidController& lid, services::TelemetryService& telemetry, UBaseType_t task_priority, BaseType_t task_core
)
    : lid_controller_(lid), telemetry_service_(telemetry), task_priority_(task_priority), task_core_(task_core) {
    // Create the event queue. Sized for 10 events to easily handle bursts.
    event_queue_ = xQueueCreate(10, sizeof(SystemEvent));
    if (event_queue_ == nullptr) {
        ESP_LOGE(TAG, "FATAL: Failed to allocate Orchestrator event queue.");
        abort();
    }
}

ApplicationManager::~ApplicationManager() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
    }
    if (event_queue_ != nullptr) {
        vQueueDelete(event_queue_);
    }
}

void ApplicationManager::start() {
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "app_manager",
        4096,  // FSM stack doesn't need to be huge
        this,
        task_priority_,
        &task_handle_,
        task_core_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "FATAL: Failed to create Application Manager task.");
        abort();
    }
    ESP_LOGI(TAG, "Application Manager Orchestrator started.");
}

// =============================================================================
// Event Handlers
// =============================================================================

void ApplicationManager::on_proximity_changed(tracking::ProximityState new_state) {
    // RUNS IN TRACKER CONTEXT - FAST PATH ONLY
    SystemEvent event{.type = EventType::ProximityUpdate, .proximity_state = new_state};

    // 0 block time. If the queue is full, we drop it. But a 10-item queue shouldn't
    // fill up just from proximity state changes unless the FSM task is completely stalled.
    if (xQueueSend(event_queue_, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full! Dropped proximity state update.");
    }
}

// =============================================================================
// RTOS Task Entry Point & Handlers
// =============================================================================

void ApplicationManager::task_entry(void* arg) {
    auto* instance = static_cast<ApplicationManager*>(arg);
    instance->task_loop();
}

void ApplicationManager::task_loop() {
    SystemEvent incoming_event;

    // 1-minute timeout to force a telemetry tick if no Bluetooth events happen
    const TickType_t queue_timeout = pdMS_TO_TICKS(60000);

    while (true) {
        if (xQueueReceive(event_queue_, &incoming_event, queue_timeout) == pdTRUE) {
            handle_event(incoming_event);
        } else {
            // Timeout occurred - no async events for a minute.
            // Synthesize a telemetry tick event.
            SystemEvent tick_event{
                .type = EventType::TelemetryTick, .proximity_state = tracking::ProximityState::Unknown
            };
            handle_event(tick_event);
        }
    }
}

void ApplicationManager::handle_event(const SystemEvent& event) {
    switch (event.type) {
        case EventType::ProximityUpdate:
            process_proximity_change(event.proximity_state);
            break;

        case EventType::TelemetryTick:
            update_telemetry_snapshot();
            break;
    }
}

void ApplicationManager::process_proximity_change(tracking::ProximityState new_state) {
    // FSM State Transition Logic
    switch (new_state) {
        case tracking::ProximityState::AtFeeder:
            if (current_state_ != SystemState::MealInProgress) {
                ESP_LOGI(TAG, "FSM: Target at feeder. Opening Lid.");

                // 1. Actuate Mechanics (Fire and Forget)
                lid_controller_.open();

                // 2. State Transition
                current_state_ = SystemState::MealInProgress;
                current_meal_start_us_ = esp_timer_get_time();
            }
            break;

        case tracking::ProximityState::Away:
        case tracking::ProximityState::Approaching:
            if (current_state_ == SystemState::MealInProgress) {
                ESP_LOGI(TAG, "FSM: Target departed. Closing Lid.");

                // 1. Actuate Mechanics (Fire and Forget)
                lid_controller_.close();

                // 2. Calculate Analytics
                uint64_t now_us = esp_timer_get_time();
                uint32_t duration_ms = static_cast<uint32_t>((now_us - current_meal_start_us_) / 1000);

                push_meal_record(current_meal_start_us_, duration_ms);
                ESP_LOGI(TAG, "Meal recorded. Duration: %lu ms", duration_ms);

                // 3. State Transition
                current_state_ = (new_state == tracking::ProximityState::Approaching) ? SystemState::Approaching
                                                                                      : SystemState::Standby;
            } else if (new_state == tracking::ProximityState::Approaching && current_state_ == SystemState::Standby) {
                // Optional: Wake up WiFi or prepare systems here
                ESP_LOGI(TAG, "FSM: Target approaching. System Standby -> Approaching.");
                current_state_ = SystemState::Approaching;
            } else if (new_state == tracking::ProximityState::Away && current_state_ == SystemState::Approaching) {
                ESP_LOGI(TAG, "FSM: Target walked away. System Approaching -> Standby.");
                current_state_ = SystemState::Standby;
            }
            break;

        case tracking::ProximityState::Unknown:
            // Ignore / No-op
            break;
    }
}

void ApplicationManager::update_telemetry_snapshot() {
    auto maybe_data = telemetry_service_.get_latest_data();
    if (maybe_data.has_value()) {
        latest_telemetry_ = maybe_data.value();
        ESP_LOGD(
            TAG,
            "Telemetry Snapshot Updated: Temp=%.1fC, Hum=%.1f%%, Feed=%d%%",
            latest_telemetry_.temperature_c,
            latest_telemetry_.humidity_percent,
            latest_telemetry_.feed_level_percent
        );

        // TODO (Next Milestone): If WiFi is connected, send this snapshot to MQTT
    }
}

// =============================================================================
// Helpers
// =============================================================================

void ApplicationManager::push_meal_record(uint64_t start_us, uint32_t duration_ms) {
    // Ring buffer insertion
    meal_buffer_[meal_buffer_head_] = MealRecord{.start_time_us = start_us, .duration_ms = duration_ms};

    // Advance head, wrap around if necessary
    meal_buffer_head_ = (meal_buffer_head_ + 1) % MAX_OFFLINE_MEALS;

    // Keep count up to max capacity
    if (meal_buffer_count_ < MAX_OFFLINE_MEALS) {
        meal_buffer_count_++;
    }
}

}  // namespace pet_access::core