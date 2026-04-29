/**
 * @file telemetry_service.hpp
 * @author João Berlese
 * @brief TelemetryService class definition for periodic sensor data acquisition and processing
 */
#pragma once

#include <array>
#include <optional>

#include "AHT25.hpp"  // Assuming HAL class name
#include "DiagnosticsService.hpp"
#include "VL53L0X.hpp"  // Assuming HAL class name
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtos.hpp"  // Provided StaticMutex & LockGuard

namespace pet_access::services {

// Pure data structure for the Orchestrator. No hardware specifics here.
struct TelemetryData {
    float temperature_c;
    float humidity_percent;
    uint8_t feed_level_percent;
    bool is_valid;
};

class TelemetryService {
public:
    /**
     * @brief Constructs the service with dependency injection of the HAL driver.
     * @param temp_sensor Reference to an already constructed AHT25 temperature/humidity sensor instance.
     * @param dist_sensor Reference to an already constructed VL53L0X distance sensor instance.
     * @param diagnostics Reference to the DiagnosticsService for logging and error reporting.
     * @param task_priority Priority of the background task.
     * @param task_core Core on which the background task will run.
     */
    TelemetryService(
        sensors::AHT25& temp_sensor,
        sensors::VL53L0X& dist_sensor,
        DiagnosticsService& diagnostics,
        UBaseType_t task_priority,
        BaseType_t task_core = 1
    );

    /**
     * @brief RAII Cleanup. Safely terminates the FreeRTOS task if running.
     */
    ~TelemetryService();

    // Rule of Five - Delete Copy/Move to prevent task handle duplication
    TelemetryService(const TelemetryService&) = delete;
    TelemetryService& operator=(const TelemetryService&) = delete;
    TelemetryService(TelemetryService&&) = delete;
    TelemetryService& operator=(TelemetryService&&) = delete;

    // Spawns the background FreeRTOS task
    void start(uint32_t sample_interval_ms = 60000);

    // Thread-safe getter. std::optional eliminates C-style pointer out-parameters.
    std::optional<TelemetryData> get_latest_data();

private:
    sensors::AHT25& temp_sensor_;
    sensors::VL53L0X& dist_sensor_;
    DiagnosticsService& diagnostics_;

    rtos::StaticMutex data_mutex_;
    TelemetryData current_data_{0.0f, 0.0f, 0, false};

    TaskHandle_t task_handle_ = nullptr;
    uint32_t sample_interval_ms_ = 60000;

    // Median filter size - compile-time constant
    static constexpr size_t kSampleSize = 5;

    // Calibration Constants for Feed Level
    static constexpr uint16_t kDistEmptyMm = 220;
    static constexpr uint16_t kDistFullMm = 95;

    // Helper function
    static constexpr uint8_t distance_to_percentage(uint16_t distance_mm);

    // Injected RTOS parameters
    UBaseType_t task_priority_;
    BaseType_t task_core_;
    // FreeRTOS Task Trampoline
    static void task_entry(void* arg);
    void task_loop();

    // Template helper for median filter to support both float and uint16_t
    template <typename T>
    T compute_median(std::array<T, kSampleSize>& buffer);
};

}  // namespace pet_access::services