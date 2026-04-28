/**
 * @file ExampleService.hpp
 * @author João Berlese
 * @brief Standard reference: concrete implementation of IExampleService.
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
 */
#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <optional>

#include "I2CMasterBus.hpp"     // example HAL dependency
#include "IExampleService.hpp"  // public interface
#include "rtos.hpp"             // pet_access::rtos::StaticMutex, LockGuard

namespace pet_access::services {

class ExampleService final : public IExampleService {
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
    ~ExampleService() override;

    // Rule of Five — resource-owning type, copy and move both forbidden.
    ExampleService(const ExampleService&) = delete;
    ExampleService& operator=(const ExampleService&) = delete;
    ExampleService(ExampleService&&) = delete;
    ExampleService& operator=(ExampleService&&) = delete;

    // ---- IExampleService ----------------------------------------------------
    [[nodiscard]] esp_err_t start() override;
    [[nodiscard]] std::optional<ExampleSample> get_latest_sample() const override;

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
