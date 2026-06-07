/**
 * @file lid_controller.hpp
 * @author João Berlese
 * @brief Actuation Service: High-level non-blocking controller for the Pet Access Lid.
 */

#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

#include "DiagnosticsService.hpp"
#include "ledc_servo.hpp"

namespace pet_access::services {

class LidController {
public:
    /**
     * @brief Constructs the service with dependency injection of the HAL driver.
     * @param servo Reference to an already constructed LedcServo instance.
     * @param diagnostics Reference to the DiagnosticsService for logging and error reporting.
     * @param task_priority Priority of the background task.
     * @param task_core Core on which the background task will run.
     */
    LidController(
        actuators::LedcServo& servo,
        DiagnosticsService& diagnostics,
        UBaseType_t task_priority,
        BaseType_t task_core = 1
    );

    /**
     * @brief RAII Cleanup. Safely terminates the FreeRTOS task if running.
     */
    ~LidController();

    // Rule of Five - Delete Copy/Move to prevent task handle duplication
    LidController(const LidController&) = delete;
    LidController& operator=(const LidController&) = delete;
    LidController(LidController&&) = delete;
    LidController& operator=(LidController&&) = delete;

    /**
     * @brief Spawns the background task and syncs the initial mechanical state.
     * @return ESP_OK on success, ESP_ERR_NO_MEM if task creation fails.
     */
    [[nodiscard]] esp_err_t initialize();

    /**
     * @brief Commands the lid to open smoothly. Non-blocking.
     * @return ESP_OK if command was queued successfully.
     */
    [[nodiscard]] esp_err_t open();

    /**
     * @brief Commands the lid to close smoothly. Non-blocking.
     * @return ESP_OK if command was queued successfully.
     */
    [[nodiscard]] esp_err_t close();

private:
    actuators::LedcServo& servo_;
    DiagnosticsService& diagnostics_;
    TaskHandle_t task_handle_{nullptr};

    // Internal state tracking
    float current_angle_{ANGLE_CLOSED};
    float angle_step_{0.0f};  // The fractional step per 20ms tick

    // Tunable Mechanical Constants
    static constexpr float ANGLE_OPEN = 150.0f;
    static constexpr float ANGLE_CLOSED = 13.0f;
    static constexpr uint32_t TRANSITION_OPEN_TIME_MS = 3000;
    static constexpr uint32_t TRANSITION_CLOSE_TIME_MS = 5000;
    static constexpr uint32_t UPDATE_INTERVAL_MS = 20;  // 50Hz - strictly aligns with LEDC PWM period

    // Task Notification Commands
    static constexpr uint32_t CMD_OPEN = 1;
    static constexpr uint32_t CMD_CLOSE = 2;

    // Injected RTOS parameters
    UBaseType_t task_priority_;
    BaseType_t task_core_;
    // FreeRTOS Task entry & Loop
    static void task_entry(void* arg);
    void task_loop();
};

}  // namespace pet_access::services