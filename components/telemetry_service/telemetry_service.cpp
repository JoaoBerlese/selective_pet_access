#include "telemetry_service.hpp"

#include <esp_log.h>

#include <algorithm>  // For std::nth_element

namespace pet_access::services {

static const char* TAG = "TelemetryService";

// =============================================================================
// Lifecycle
// =============================================================================

TelemetryService::TelemetryService(
    sensors::AHT25& temp_sensor, sensors::VL53L0X& dist_sensor, UBaseType_t task_priority, BaseType_t task_core
)
    : temp_sensor_(temp_sensor), dist_sensor_(dist_sensor), task_priority_(task_priority), task_core_(task_core) {}

TelemetryService::~TelemetryService() {
    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Terminating TelemetryService background task.");
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

void TelemetryService::start(uint32_t sample_interval_ms) {
    sample_interval_ms_ = sample_interval_ms;

    // Pinned to Core 1 (App Core) to avoid interfering with WiFi/BT on Core 0
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "telemetry_tsk",
        4096,
        this,
        task_priority_,  // <--- Injected Priority
        &task_handle_,
        task_core_  // <--- Injected Core
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate telemetry task!");
    }
}

// =============================================================================
// Public API (Non-Blocking)
// =============================================================================

std::optional<TelemetryData> TelemetryService::get_latest_data() {
    // RAII LockGuard ensures mutex is released regardless of how we exit the scope
    rtos::LockGuard lock(data_mutex_);

    if (!current_data_.is_valid) {
        return std::nullopt;
    }
    return current_data_;
}

// =============================================================================
// Private Helpers
// =============================================================================

constexpr uint8_t TelemetryService::distance_to_percentage(uint16_t distance_mm) {
    // Clamp limits to prevent overflow/underflow
    if (distance_mm >= kDistEmptyMm)
        return 0;
    if (distance_mm <= kDistFullMm)
        return 100;

    // Integer linear interpolation
    return static_cast<uint8_t>(((kDistEmptyMm - distance_mm) * 100) / (kDistEmptyMm - kDistFullMm));
}

// =============================================================================
// Background RTOS Task
// =============================================================================

void TelemetryService::task_entry(void* arg) {
    auto* instance = static_cast<TelemetryService*>(arg);
    instance->task_loop();
}

void TelemetryService::task_loop() {
    // Static/Stack allocation: No heap fragmentation
    std::array<float, kSampleSize> temp_buffer{};
    std::array<float, kSampleSize> hum_buffer{};
    std::array<uint16_t, kSampleSize> dist_buffer{};

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(sample_interval_ms_);

    while (true) {
        bool burst_success = true;

        for (size_t i = 0; i < kSampleSize; ++i) {
            // AHT25
            sensors::AHT25::reading reading_values;
            if (temp_sensor_.read(reading_values) == ESP_OK) {  // Assuming ESP_OK return
                temp_buffer[i] = reading_values.temperature;
                hum_buffer[i] = reading_values.humidity;
            } else {
                burst_success = false;
                ESP_LOGW(TAG, "AHT25 sample %zu failed", i);
            }

            // VL53L0X
            uint16_t dist = 0;
            if (dist_sensor_.read_single_shot(dist) == ESP_OK) {  // Assuming ESP_OK return
                dist_buffer[i] = dist;
            } else {
                burst_success = false;
                ESP_LOGW(TAG, "VL53L0X sample %zu failed", i);
            }

            // Yield between samples to allow lower priority tasks to run and avoid I2C bus saturation
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (burst_success) {
            float median_temp = compute_median(temp_buffer);
            float median_hum = compute_median(hum_buffer);
            uint16_t median_dist = compute_median(dist_buffer);

            // Convert raw distance to business-logic percentage
            uint8_t feed_percent = distance_to_percentage(median_dist);

            {
                // Scoped lock just for the assignment to minimize blocking time
                rtos::LockGuard lock(data_mutex_);
                current_data_.temperature_c = median_temp;
                current_data_.humidity_percent = median_hum;
                current_data_.feed_level_percent = feed_percent;
                current_data_.is_valid = true;
            }
            ESP_LOGD(TAG, "State Updated | T:%.1f H:%.1f F:%u%%", median_temp, median_hum, feed_percent);
        } else {
            // If the burst fails, we maintain the 'Last Valid State' but log the degradation.
            // The orchestrator will still receive the last known good value.
            ESP_LOGW(TAG, "Sensor burst incomplete. Retaining last valid state.");
            rtos::LockGuard lock(data_mutex_);
            current_data_.is_valid = false;
        }

        // vTaskDelayUntil guarantees precise periodicity without drifting over time
        xTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

template <typename T>
T TelemetryService::compute_median(std::array<T, kSampleSize>& buffer) {
    // std::nth_element is zero-cost abstract logic. It partitions the array such
    // that the element at the median index is strictly the median value. O(N) complexity.
    auto median_it = buffer.begin() + buffer.size() / 2;
    std::nth_element(buffer.begin(), median_it, buffer.end());
    return *median_it;
}

}  // namespace pet_access::services