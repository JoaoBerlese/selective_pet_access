/**
 * @file rtos.hpp
 * @author João Berlese
 * @brief Zero-heap FreeRTOS Mutex wrapper and LockGuard for RAII-based synchronization
 *
 */
#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace pet_access::rtos {

inline constexpr const char* TAG = "rtos";

/**
 * @brief Zero-heap FreeRTOS Mutex wrapper.
 * @note In the ESP32-S3-N16R8, ensure the objects owning this primitive
 * are instantiated in internal SRAM (DRAM), NOT in the 8MB Octal PSRAM.
 * Placing OS primitives in PSRAM can cause severe scheduler latency due
 * to cache misses during context switches.
 */
class StaticMutex {
public:
    StaticMutex() {
        // Allocates the control block inside the object's footprint (buffer_)
        handle_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
        if (handle_ == nullptr) {
            // This should never happen since we're using static allocation, but we check just in case.
            ESP_LOGE(TAG, "Failed to create StaticMutex");
            abort();  // Enforce strict failure
        }
    }

    ~StaticMutex() {
        if (handle_ != nullptr) {
            vSemaphoreDelete(handle_);
        }
    }

    // Rule of Five: Disable copy and move semantics to prevent resource mishandling
    StaticMutex(const StaticMutex&) = delete;
    StaticMutex& operator=(const StaticMutex&) = delete;
    StaticMutex(StaticMutex&&) = delete;
    StaticMutex& operator=(StaticMutex&&) = delete;

    [[nodiscard]] bool lock(TickType_t timeout_ticks = portMAX_DELAY) {
        return xSemaphoreTake(handle_, timeout_ticks) == pdTRUE;
    }

    void unlock() {
        xSemaphoreGive(handle_);
    }

private:
    StaticSemaphore_t mutex_buffer_;     // Control block for the mutex
    SemaphoreHandle_t handle_{nullptr};  // Handle returned by FreeRTOS (points to mutex_buffer_)
};

class LockGuard {
public:
    explicit LockGuard(StaticMutex& mutex, TickType_t timeout_ticks = portMAX_DELAY) : mutex_(mutex) {
        is_acquired_value = mutex_.lock(timeout_ticks);
    }

    // Automatically releases the mutex when the stack frame unwinds
    ~LockGuard() {
        if (is_acquired_value) {
            mutex_.unlock();
        }
    }

    // A lock guard is strictly tied to its local scope; it cannot be copied or assigned.
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;

    // Must be checked by business logic if a timeout was specified
    [[nodiscard]] bool is_acquired() const noexcept {
        return is_acquired_value;
    }

private:
    StaticMutex& mutex_;
    bool is_acquired_value{false};
};

}  // namespace pet_access::rtos