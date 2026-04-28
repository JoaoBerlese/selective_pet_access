#include "ExampleService.hpp"

#include <esp_log.h>

namespace pet_access::services {

static const char* TAG = "ExampleService";

// =============================================================================
// Lifecycle
// =============================================================================

ExampleService::ExampleService(i2c::I2CMasterBus& bus, const Config& config, IExampleObserver* observer)
    : bus_(bus), observer_(observer), config_(config) {}

ExampleService::~ExampleService() {
    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Terminating ExampleService background task.");
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

esp_err_t ExampleService::start() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "start() called twice; ignoring.");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "example_svc",
        config_.task_stack_size,
        this,
        config_.task_priority,
        &task_handle_,
        config_.task_core
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn ExampleService task (FreeRTOS heap exhausted?).");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Started on core %d at priority %u (interval %lu ms, addr 0x%02X)",
        static_cast<int>(config_.task_core),
        static_cast<unsigned>(config_.task_priority),
        static_cast<unsigned long>(config_.sample_interval_ms),
        config_.i2c_address
    );
    return ESP_OK;
}

// =============================================================================
// Public API (Non-Blocking)
// =============================================================================

std::optional<ExampleSample> ExampleService::get_latest_sample() const {
    // Fast-path acquire load: avoid taking the mutex when no sample exists yet.
    // Acquire ordering pairs with the release store in task_loop() below.
    if (!sample_valid_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // RAII LockGuard: mutex is released regardless of how we exit the scope.
    rtos::LockGuard lock(sample_mutex_);
    return latest_sample_;
}

// =============================================================================
// Private Helpers
// =============================================================================

esp_err_t ExampleService::sample_once(ExampleSample& out_sample) {
    // Read a single status byte from the device at the configured address.
    // I2CMasterBus is internally mutex-protected; safe to call from any task.
    uint8_t raw = 0;
    esp_err_t err = bus_.read(config_.i2c_address, &raw, 1, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        return err;
    }

    out_sample.sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    out_sample.value = static_cast<int32_t>(raw);
    return ESP_OK;
}

// =============================================================================
// Background RTOS Task
// =============================================================================

void ExampleService::task_entry(void* arg) {
    auto* instance = static_cast<ExampleService*>(arg);
    instance->task_loop();
}

void ExampleService::task_loop() {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(config_.sample_interval_ms);

    while (true) {
        ExampleSample sample{};
        esp_err_t err = sample_once(sample);

        if (err == ESP_OK) {
            // Publish: write under the mutex, then flip the validity flag with
            // release ordering so readers on other cores see a coherent snapshot.
            {
                rtos::LockGuard lock(sample_mutex_);
                latest_sample_ = sample;
            }
            sample_valid_.store(true, std::memory_order_release);

            // Notify the observer. The observer contract forbids blocking here.
            if (observer_ != nullptr) {
                observer_->on_sample(sample);
            }

            ESP_LOGD(
                TAG, "Sample #%lu = %ld", static_cast<unsigned long>(sample.sequence), static_cast<long>(sample.value)
            );
        } else {
            // Soft failure: keep the previous valid snapshot, log the degradation.
            ESP_LOGW(TAG, "HAL read failed: %s", esp_err_to_name(err));
        }

        // xTaskDelayUntil holds a precise period without drifting over time.
        xTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

}  // namespace pet_access::services
