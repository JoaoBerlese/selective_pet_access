/**
 * @file ExampleService.hpp
 * @author João Berlese
 * @brief Standard reference: standalone concrete periodic service.
 *
 * Embodies every tenet of the Berlese Standard. See
 * docs/06_Firmware_Design_Guidelines.md §12 for the rationale behind every line.
 *
 *   - DI by reference (no globals, no singletons)
 *   - StaticMutex + LockGuard for shared state (zero-heap, DRAM-resident)
 *   - std::atomic with explicit memory_order for inter-core fast-path reads
 *   - [[nodiscard]] esp_err_t at the HAL boundary; std::optional<T> at the service API
 *   - Rule of Five: copy/move deleted (resource-owning type)
 *   - Trampoline task pattern; priority/core injected from sys_config.hpp
 *   - No new/malloc; no exceptions; no RTTI; no printf
 *   - No service-level abstract interface: one impl exists, no test mock required (§6.1 YAGNI)
 */
#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <optional>

#include "I2CMasterBus.hpp"
#include "rtos.hpp"  // pet_access::rtos::StaticMutex, LockGuard

namespace pet_access::services {

/**
 * @brief Snapshot returned by ExampleService::get_latest_sample().
 *
 * Trivially copyable so it can cross task boundaries by value (no heap, no aliasing).
 */
struct ExampleSample {
    uint32_t sequence;  // Monotonically increasing per published sample
    int32_t value;      // Domain-agnostic measurement payload
};

/**
 * @brief Observer callback invoked whenever a new sample is published.
 *
 * @warning Implementations MUST NOT block. This method runs in the ExampleService
 * task context. Forward the event to your own task via xQueueSend(timeout=0)
 * or xTaskNotify and return immediately. See ApplicationManager::on_proximity_changed
 * (components/application_manager/ApplicationManager.cpp) for the canonical bridging
 * pattern.
 *
 * Observer interfaces are always abstract (§6.1): they represent the extension point
 * that external consumers implement. This is orthogonal to the YAGNI rule for service
 * interfaces.
 */
class IExampleObserver {
public:
    virtual ~IExampleObserver() = default;
    virtual void on_sample(const ExampleSample& sample) = 0;
};

class ExampleService final {
public:
    /**
     * @brief All tunables injected at construction. No #defines, no menuconfig
     * lookups inside the class.
     */
    struct Config {
        uint8_t i2c_address;          // 7-bit I2C address of the underlying device
        uint32_t sample_interval_ms;  // Period between background reads
        UBaseType_t task_priority;    // From sys::PRIORITY_*
        BaseType_t task_core;         // 0 or 1
        uint32_t task_stack_size;     // Bytes
    };

    /**
     * @brief Dependency injection.
     * @param bus      Mandatory non-owning reference to the shared I2C bus.
     * @param config   Tunables; copied into the instance.
     * @param observer Optional; nullptr disables notifications.
     */
    ExampleService(i2c::I2CMasterBus& bus, const Config& config, IExampleObserver* observer = nullptr);

    /// RAII cleanup; tears down the FreeRTOS task if running.
    ~ExampleService();

    // Rule of Five — resource-owning type, copy and move both forbidden.
    ExampleService(const ExampleService&) = delete;
    ExampleService& operator=(const ExampleService&) = delete;
    ExampleService(ExampleService&&) = delete;
    ExampleService& operator=(ExampleService&&) = delete;

    /**
     * @brief Start the background task. Must be called exactly once after construction.
     * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already started,
     *         ESP_ERR_NO_MEM if FreeRTOS could not allocate the task.
     */
    [[nodiscard]] esp_err_t start();

    /**
     * @brief Thread-safe snapshot of the most recent sample.
     * @return std::nullopt if no valid sample has been produced yet.
     */
    [[nodiscard]] std::optional<ExampleSample> get_latest_sample() const;

private:
    // Trampoline: FreeRTOS C ABI -> C++ instance method.
    static void task_entry(void* arg);
    void task_loop();

    // Performs one HAL read; returns ESP_OK on success.
    [[nodiscard]] esp_err_t sample_once(ExampleSample& out_sample);

    // ---- Injected dependencies (non-owning) ---------------------------------
    i2c::I2CMasterBus& bus_;
    IExampleObserver* observer_;
    const Config config_;

    // ---- RTOS handles -------------------------------------------------------
    TaskHandle_t task_handle_{nullptr};

    // ---- Shared state -------------------------------------------------------
    // The mutex protects the multi-field snapshot. Hot-path readers can also
    // poll `sample_valid_` without taking the mutex (acquire ordering).
    mutable rtos::StaticMutex sample_mutex_;
    ExampleSample latest_sample_{};
    std::atomic<bool> sample_valid_{false};
    std::atomic<uint32_t> sequence_{0};
};

}  // namespace pet_access::services
