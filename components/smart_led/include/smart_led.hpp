/**
 * @file smart_led.hpp
 * @author João Berlese
 * @brief Modern C++ RAII Wrapper for WS2812B RGB LED + FreeRTOS Task
 */

#pragma once

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>
// Remember to include the ESP-IDF component for LED strip control
// idf.py add-dependency --component smart_led "espressif/led_strip"
#include "led_strip.h"

namespace pet_access::ui {

struct SmartLedConfig {
    gpio_num_t pin;
    uint32_t rmt_resolution_hz{10'000'000};  // Default 10MHz

    // Injected RTOS parameters
    UBaseType_t task_priority;
    BaseType_t task_core;      // 0, 1, or tskNO_AFFINITY
    uint32_t task_stack_size;  // Good to inject this to avoid hardcoding in the class
};

class SmartLed {
public:
    /**
     * @brief Constructs the SmartLed, allocating RMT, Queue, and spawning the
     * Task.
     * @param config Hardware configuration for the LED.
     */
    explicit SmartLed(const SmartLedConfig& config);

    /**
     * @brief Destructor strictly enforces teardown order: Task -> Queue -> RMT.
     */
    ~SmartLed();

    // --- Architectural Constraint ---
    // The FreeRTOS task captures the 'this' pointer. If this object is copied
    // or moved, the task will hold a dangling pointer. We must forbid this.
    SmartLed(const SmartLed&) = delete;
    SmartLed& operator=(const SmartLed&) = delete;
    SmartLed(SmartLed&&) = delete;
    SmartLed& operator=(SmartLed&&) = delete;

    // --- Thread-Safe Public API ---
    void set_static(uint8_t r, uint8_t g, uint8_t b) noexcept;
    void set_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms) noexcept;
    void set_breath(uint8_t r, uint8_t g, uint8_t b) noexcept;
    void set_off() noexcept;

private:
    enum class Mode : uint8_t { Off, Static, Blink, Breath };

    struct Message {
        Mode mode;
        uint8_t r, g, b;
        uint32_t blink_period_ms;
    };

    // Internal Configuration
    static constexpr UBaseType_t QUEUE_LENGTH = 10;

    // Hardware & OS Handles
    led_strip_handle_t strip_handle_{nullptr};
    QueueHandle_t cmd_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};

    // FreeRTOS C-linkage entry point
    static void task_entry(void* arg);

    // The actual state machine loop
    void task_loop();

    // Zero-cost abstraction for gamma correction
    static constexpr uint8_t gamma_correct(uint8_t input) noexcept;
};

}  // namespace pet_access::ui