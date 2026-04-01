/**
 * @file NimbleScanner.hpp
 * @author João Berlese
 * @brief C++ wrapper around the ESP-IDF NimBLE stack for BLE beacon scanning.
 */
#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "BeaconTypes.hpp"
#include "IBeaconScanner.hpp"

struct ble_gap_event;  // Forward declaration of NimBLE C struct

namespace pet_access::bluetooth {

class NimbleScanner final : public IBeaconScanner {
public:
    // Dependency Injection: The scanner does not own the queue; it just uses it.
    explicit NimbleScanner(QueueHandle_t event_queue);

    // RAII: Destructor cleans up the NimBLE stack if the object goes out of scope.
    ~NimbleScanner() override;

    // Prevent copying and moving (Hardware interfaces should never be copied)
    NimbleScanner(const NimbleScanner&) = delete;             // No copy constructor
    NimbleScanner& operator=(const NimbleScanner&) = delete;  // No copy assignment
    NimbleScanner(NimbleScanner&&) = delete;                  // No move constructor
    NimbleScanner& operator=(NimbleScanner&&) = delete;       // No move assignment

    esp_err_t initialize();  // Initializes the NimBLE stack and configures the scanner

    void start() override;  // Starts the BLE scanning process
    void stop() override;   // Stops the BLE scanning process

private:
    QueueHandle_t event_queue_;         // Queue for sending BeaconEvent structs to the application task
    SemaphoreHandle_t sync_semaphore_;  // Binary semaphore to block until NimBLE is ready

    bool initialized_{false};

    // BLE math: NimBLE GAP units are in 0.625ms increments
    static constexpr uint32_t SCAN_INTERVAL_MS = 110;
    static constexpr uint32_t SCAN_WINDOW_MS = 70;
    static constexpr uint16_t NIMBLE_SCAN_INTERVAL = (SCAN_INTERVAL_MS * 1000) / 625;
    static constexpr uint16_t NIMBLE_SCAN_WINDOW = (SCAN_WINDOW_MS * 1000) / 625;

    // C-Style Callbacks required by NimBLE
    static void on_stack_sync(void);
    static void on_stack_reset(int reason);
    static void nimble_host_task(void* param);

    // The main GAP event router
    static int gap_event_callback(struct ble_gap_event* event, void* arg);

    // The C++ instance handler for parsed events
    void handle_discovery_event(const struct ble_gap_event* event);

    // Global pointer needed strictly for the argument-less on_stack_sync callback
    static NimbleScanner* instance_;
};

}  // namespace pet_access::bluetooth