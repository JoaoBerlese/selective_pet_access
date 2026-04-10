// C++ Standard Library
#include <array>
#include <cstdio>
// ESP-IDF Framework & FreeRTOS
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
// Local Project Dependencies
#include "AHT25.hpp"
#include "I2CMasterBus.hpp"
#include "VL53L0X.hpp"
#include "board_mapping.hpp"
#include "ledc_servo.hpp"
#include "lid_controller.hpp"
#include "smart_led.hpp"
#include "sys_config.hpp"
#include "telemetry_service.hpp"
// BLE Subsystem Dependencies
#include "BeaconTypes.hpp"
#include "NimbleScanner.hpp"
#include "PetProximityTracker.hpp"
#include "nvs_flash.h"

static const char* TAG = "Main";

// =============================================================================
// Core Application Architecture
// =============================================================================
namespace pet_access::core {

// Predefined Instance ID for myCat's collar (Eddystone UID Frame)
// This is the "serial number" part of the UID frame that uniquely identifies the pet's collar.
constexpr std::array<uint8_t, 6> CAT_COLLAR_INSTANCE_ID = {0xFD, 0xA5, 0x06, 0x93, 0xA4, 0xE2};

class SystemController {
public:
    // The constructor initializes all subsystems via RAII
    SystemController()
        : ble_queue_()                     // 1. Initialize the BLE event queue before the scanner and tracker
        , ble_scanner_(ble_queue_.handle)  // 2. Inject the queue into the scanner
        // 3. Inject both the scanner and the queue into the tracker
        , pet_tracker_(ble_scanner_, ble_queue_.handle, CAT_COLLAR_INSTANCE_ID, sys::PRIORITY_PET_TRACKING)
        , i2c_bus_(board::I2C_PORT, board::I2C_SDA_PIN, board::I2C_SCL_PIN, 100'000)
        , status_led_(led_config_)
        , temp_humidity_sensor_(i2c_bus_)
        , distance_sensor_(i2c_bus_)  // Inject the shared I2C bus
        // Inject sensors and priority into TelemetryService
        , telemetry_service_(temp_humidity_sensor_, distance_sensor_, sys::PRIORITY_TELEMETRY)
        , lid_servo_(servo_config)
        , lid_controller_(lid_servo_, sys::PRIORITY_LID_CONTROLLER) {}

    // The single entry point to kick off the application logic
    void start() {
        ESP_LOGI(TAG, "Booting Selective Pet Access System...");

        // Signal a successful boot sequence (e.g., Solid green)
        status_led_.set_static(0, 30, 0);

        // Initialize NVS (Required for the Bluetooth Controller baseband)
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);

        // Initialize NimBLE Stack (Blocks until synced with baseband)
        ESP_LOGI(TAG, "Initializing BLE Subsystem...");
        if (ble_scanner_.initialize() == ESP_OK) {
            // Start the RTOS task for tracking. This will automatically start the scanner.
            pet_tracker_.start();
        } else {
            ESP_LOGE(TAG, "CRITICAL: Failed to initialize BLE stack. Tracker offline.");
        }

        // Hardware Initialization for Sensors
        if (distance_sensor_.initialize() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize VL53L0X distance sensor.");
        }
        // Start Telemetry Service (Polls every 2 seconds for testing, adjust as needed)
        telemetry_service_.start();

        esp_err_t ret = lid_servo_.initialize();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize servo: %s", esp_err_to_name(ret));
        }
        // Init lid_controller Service (Spawns its own RTOS task)
        ret = lid_controller_.initialize();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize LidController: %s", esp_err_to_name(ret));
        }
        /*
        // Example action:
        ret = lid_controller_.open();  // Will move smoothly over 2 seconds in the background
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send open command to lid controller: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
        // Example action:
        ret = lid_controller_.close();  // Will move smoothly over 2 seconds in the background
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send close command to lid controller: %s", esp_err_to_name(ret));
        }
        */
    }

private:
    // --- BLE Infrastructure ---
    // RAII Wrapper to guarantee the FreeRTOS Queue is created BEFORE
    // the scanner and tracker are instantiated in the constructor init-list.
    struct BleEventQueue {
        QueueHandle_t handle;
        BleEventQueue() : handle(xQueueCreate(16, sizeof(bluetooth::BeaconEvent))) {
            if (handle == nullptr) {
                ESP_LOGE(TAG, "FATAL: Failed to allocate BLE Event Queue");
                abort();
            }
        }
        ~BleEventQueue() {
            if (handle)
                vQueueDelete(handle);
        }
    } ble_queue_;
    bluetooth::NimbleScanner ble_scanner_;
    tracking::PetProximityTracker pet_tracker_;

    // Core Hardware Buses
    i2c::I2CMasterBus i2c_bus_;

    // Subsystem Configurations
    const ui::SmartLedConfig led_config_{
        .pin = board::PIN_LED_STRIP,
        .rmt_resolution_hz = board::LED_RMT_RES_HZ,
        .task_priority = sys::PRIORITY_UI_LED,
        .task_core = 1,
        .task_stack_size = 3072
    };
    const actuators::LedcServo::Config servo_config = {
        .pin = board::PIN_SERVO,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .min_pulse_us = 500,
        .max_pulse_us = 2400,
        .max_angle_deg = 180.0f
    };

    // Subsystem Objects
    ui::SmartLed status_led_;
    sensors::AHT25 temp_humidity_sensor_;
    sensors::VL53L0X distance_sensor_;
    services::TelemetryService telemetry_service_;
    actuators::LedcServo lid_servo_;
    services::LidController lid_controller_;
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
