#include "lid_controller.hpp"

#include <esp_log.h>

#include <algorithm>
#include <cmath>

namespace pet_access::services {

static const char* TAG = "LidController";

// =============================================================================
// Lifecycle
// =============================================================================

LidController::LidController(
    actuators::LedcServo& servo, DiagnosticsService& diagnostics, UBaseType_t task_priority, BaseType_t task_core
)
    : servo_(servo), diagnostics_(diagnostics), task_priority_(task_priority), task_core_(task_core) {
    ESP_LOGI(TAG, "LidController constructed");
}

LidController::~LidController() {
    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Terminating LidController background task.");
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

esp_err_t LidController::initialize() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Force the hardware to sync with our initial assumption (Closed)
    // We do a fast snap to the closed position on boot, then sleep.
    esp_err_t ret = servo_.set_angle(current_angle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial lid angle: %s", esp_err_to_name(ret));
        diagnostics_.push_error_record(ErrorSource::Servo, ret);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(500));  // Allow time for physical movement
    ret = servo_.sleep();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to put servo to sleep: %s", esp_err_to_name(ret));
        diagnostics_.push_error_record(ErrorSource::Servo, ret);
        return ret;
    }

    // Spawn the interpolation task
    // Stack size is minimal (2048) as this task only does basic math and driver calls.
    BaseType_t taskRet = xTaskCreatePinnedToCore(
        task_entry,
        "lid_ctrl_task",
        2048,
        this,
        task_priority_,  // <--- Injected Priority
        &task_handle_,
        task_core_  // <--- Injected Core
    );

    if (taskRet != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn lid controller task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LidController initialized and task spawned.");
    return ESP_OK;
}

// =============================================================================
// Public API (Non-Blocking)
// =============================================================================

esp_err_t LidController::open() {
    if (!task_handle_)
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Command: OPEN");
    xTaskNotify(task_handle_, CMD_OPEN, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t LidController::close() {
    if (!task_handle_)
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Command: CLOSE");
    xTaskNotify(task_handle_, CMD_CLOSE, eSetValueWithOverwrite);
    return ESP_OK;
}

// =============================================================================
// Background RTOS Task
// =============================================================================

void LidController::task_entry(void* arg) {
    // Cast the context pointer back to the object instance
    auto* instance = static_cast<LidController*>(arg);
    instance->task_loop();
}

void LidController::task_loop() {
    esp_err_t ret = ESP_OK;
    uint32_t command = 0, TRANSITION_TIME_MS;
    float target_angle = current_angle_;
    bool error_latched = false;

    // Zero-cost abstraction: Inline lambda to handle latched error logging + diagnostics push.
    // This saves Flash (.rodata) by reusing the format string and provides exact context.
    auto log_latched_error = [&](const char* context, esp_err_t err) {
        if (!error_latched) {
            ESP_LOGE(TAG, "Hardware fault during %s: %s. Suppressing further logs.", context, esp_err_to_name(err));
            diagnostics_.push_error_record(ErrorSource::Servo, err);
            error_latched = true;
        }
    };

    while (true) {
        // Lock the interpolation loop to the hardware PWM frequency (50Hz / 20ms)
        TickType_t wait_ticks = (current_angle_ == target_angle) ? portMAX_DELAY : pdMS_TO_TICKS(UPDATE_INTERVAL_MS);

        // xTaskNotifyWait returns pdTRUE if a notification was received, pdFALSE if it timed out.
        if (xTaskNotifyWait(0x00, ULONG_MAX, &command, wait_ticks) == pdTRUE) {
            error_latched = false;  // Reset latch on new command

            // 1. Process the incoming command
            if (command == CMD_OPEN) {
                target_angle = ANGLE_OPEN;
            } else if (command == CMD_CLOSE) {
                target_angle = ANGLE_CLOSED;
            }

            // 2. Calculate fractional kinematics
            if (current_angle_ != target_angle) {
                float total_distance = target_angle - current_angle_;
                if (command == CMD_OPEN)
                    TRANSITION_TIME_MS = TRANSITION_OPEN_TIME_MS;
                else
                    TRANSITION_TIME_MS = TRANSITION_CLOSE_TIME_MS;
                float total_steps = static_cast<float>(TRANSITION_TIME_MS) / UPDATE_INTERVAL_MS;
                angle_step_ = total_distance / total_steps;
            } else {
                // Already at destination, ensure hardware is asleep
                ret = servo_.sleep();
                if (ret != ESP_OK)
                    log_latched_error("idle sleep", ret);
            }

        } else {
            // TIMEOUT OCCURRED: Apply the fractional step.
            current_angle_ += angle_step_;

            // Floating-point overshoot protection: Snap exactly to target if we cross it
            if ((angle_step_ > 0.0f && current_angle_ >= target_angle) ||
                (angle_step_ < 0.0f && current_angle_ <= target_angle)) {
                current_angle_ = target_angle;
            }

            // Dispatch to HAL
            ret = servo_.set_angle(current_angle_);
            if (ret != ESP_OK)
                log_latched_error("lid angle stepping", ret);

            // Check if this step brought us to the final destination
            // If we've just reached the closed position, put the servo to sleep to save power.
            if ((current_angle_ == target_angle) && (command == CMD_CLOSE)) {
                ret = servo_.sleep();
                if (ret != ESP_OK)
                    log_latched_error("target sleep", ret);
            }
        }
    }
}

}  // namespace pet_access::services