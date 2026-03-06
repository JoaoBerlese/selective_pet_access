// C++ Standard Library
#include <cstdio>
// ESP-IDF Framework & FreeRTOS
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// Local Project Dependencies
#include "AHT25.hpp"
#include "I2CMasterBus.hpp"
#include "VL53L0X.hpp"
#include "board_mapping.hpp"
#include "smart_led.hpp"
#include "sys_config.hpp"

static const char* TAG = "Main";

// =============================================================================
// Core Application Architecture
// =============================================================================
namespace pet_access::core {

class SystemController {
public:
    // The constructor initializes all subsystems via RAII
    SystemController()
        : i2c_bus_(board::I2C_PORT, board::I2C_SDA_PIN, board::I2C_SCL_PIN, 100'000)
        , status_led_(led_config_)
        , temp_humidity_sensor_(i2c_bus_)
        , distance_sensor_(i2c_bus_) {}  // Inject the shared I2C bus

    // The single entry point to kick off the application logic
    void start() {
        ESP_LOGI(TAG, "Booting Selective Pet Access System...");

        // Signal a successful boot sequence (e.g., Solid green)
        status_led_.set_static(0, 30, 0);

        // Perform boot-time hardware diagnostics
        run_sensor_diagnostic();
    }

private:
    void run_sensor_diagnostic() {
        // --- 1. AHT25 Diagnostic ---
        sensors::AHT25::reading diag_reading;
        esp_err_t ret = temp_humidity_sensor_.read(diag_reading);
        if (ret == ESP_OK) {
            ESP_LOGI(
                TAG,
                "AHT25 Diagnostic - Temperature: %.2f C, Humidity: %.2f %%",
                diag_reading.temperature,
                diag_reading.humidity
            );
        } else {
            ESP_LOGW(TAG, "AHT25 Diagnostic Failed: %s", esp_err_to_name(ret));
        }

        // --- 2. VL53L0X Diagnostic ---
        ret = distance_sensor_.initialize();
        if (ret == ESP_OK) {
            uint16_t distance_mm = 0;
            ret = distance_sensor_.read_single_shot(distance_mm);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "VL53L0X Diagnostic - Distance: %u mm", distance_mm);
            } else {
                ESP_LOGW(TAG, "VL53L0X Read Failed: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGW(TAG, "VL53L0X Initialization Failed: %s", esp_err_to_name(ret));
        }
    }

    // Core Hardware Buses
    i2c::I2CMasterBus i2c_bus_;

    // Subsystem Configurations
    ui::SmartLedConfig led_config_{
        .pin = board::PIN_LED_STRIP,
        .rmt_resolution_hz = board::LED_RMT_RES_HZ,
        .task_priority = sys::PRIORITY_UI_LED,
        .task_core = 1,
        .task_stack_size = 3072
    };

    // Subsystem Objects
    ui::SmartLed status_led_;
    sensors::AHT25 temp_humidity_sensor_;
    sensors::VL53L0X distance_sensor_;
};

}  // namespace pet_access::core

// =============================================================================
// ESP-IDF Entry Point
// =============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Hardware initialized. Booting C++ context...");

    // 1. The Static Allocation Trick
    // By declaring this 'static', the object is placed in the ESP32's .bss/.data
    // memory segment (SRAM), NOT on the FreeRTOS stack, and NOT on the heap.
    // It is perfectly safe, permanently allocated, and doesn't pollute the global scope.
    static pet_access::core::SystemController system_app;

    // Start the logic
    system_app.start();

    // Reclaim memory
    ESP_LOGI(TAG, "Boot complete. Terminating app_main task to save RAM.");
    vTaskDelete(nullptr);
}
