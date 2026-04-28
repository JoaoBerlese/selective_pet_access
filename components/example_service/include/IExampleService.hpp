/**
 * @file IExampleService.hpp
 * @author João Berlese
 * @brief Standard reference: abstract interface for a periodic, observable service.
 *
 * Canonical template for new service interfaces in the Selective Pet Access firmware.
 * See docs/06_Firmware_Design_Guidelines.md §6 (Decoupling & DI) and §12
 * (The Standard Example Component) for the rationale behind every line.
 */
#pragma once

#include <esp_err.h>

#include <cstdint>
#include <optional>

namespace pet_access::services {

/**
 * @brief Snapshot returned by IExampleService::get_latest_sample().
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
 */
class IExampleObserver {
public:
    virtual ~IExampleObserver() = default;
    virtual void on_sample(const ExampleSample& sample) = 0;
};

/**
 * @brief Abstract interface decoupling business logic from the concrete implementation.
 *
 * Wire concrete implementations into SystemController by reference. Consumers should
 * depend on this interface, never on the concrete class.
 */
class IExampleService {
public:
    virtual ~IExampleService() = default;

    /**
     * @brief Start the background task. Must be called exactly once after construction.
     * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already started,
     *         ESP_ERR_NO_MEM if FreeRTOS could not allocate the task.
     */
    [[nodiscard]] virtual esp_err_t start() = 0;

    /**
     * @brief Thread-safe snapshot of the most recent sample.
     * @return std::nullopt if no valid sample has been produced yet.
     */
    [[nodiscard]] virtual std::optional<ExampleSample> get_latest_sample() const = 0;
};

}  // namespace pet_access::services
