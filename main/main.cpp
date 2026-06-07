// C++ Standard Library
#include <array>
#include <cstdio>

// ESP-IDF Framework & FreeRTOS
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi_default.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

// Local Project Dependencies
#include "AHT25.hpp"
#include "DiagnosticsService.hpp"
#include "I2CMasterBus.hpp"
#include "VL53L0X.hpp"
#include "WiFiStationService.hpp"
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

// Core Business Logic
#include "ApplicationManager.hpp"

static const char* TAG = "Main";

// =============================================================================
// Core Application Architecture
// =============================================================================
namespace pet_access::core {

// Predefined Instance ID for the tracked collar (Eddystone UID Frame).
// This is the "serial number" part of the UID frame that uniquely identifies the beacon hardware.
// Inverted-logic branch: the same physical beacon is now worn by the BLOCKED cat — detection
// triggers the lid to CLOSE. The byte sequence is unchanged because the hardware ID does not move
// with the cat, only the collar does.
constexpr std::array<uint8_t, 6> CAT_COLLAR_INSTANCE_ID = {0xFD, 0xA5, 0x06, 0x93, 0xA4, 0xE2};

class SystemController {
public:
    // Dependency Injection Initialization
    // Warning: Initialization happens in the order members are DECLARED below.
    SystemController()
        : ble_queue_()                     // Initialize the BLE event queue before the scanner and tracker
        , ble_scanner_(ble_queue_.handle)  // Inject the queue into the scanner
        , i2c_bus_(board::I2C_PORT, board::I2C_SDA_PIN, board::I2C_SCL_PIN, 100'000)
        , status_led_(led_config_)
        , temp_humidity_sensor_(i2c_bus_)
        , distance_sensor_(i2c_bus_)  // Inject the shared I2C bus
        , lid_servo_(servo_config_)
        // diagnostics_ has no dependencies and must be constructed before any consumer.
        , diagnostics_()
        // Inject sensors, diagnostics, and priority into TelemetryService
        , telemetry_service_(temp_humidity_sensor_, distance_sensor_, diagnostics_, sys::PRIORITY_TELEMETRY)
        , lid_controller_(lid_servo_, diagnostics_, sys::PRIORITY_LID_CONTROLLER)
        , app_manager_(lid_controller_, telemetry_service_, diagnostics_, sys::PRIORITY_ORCHESTRATOR)
        , pet_tracker_(
              ble_scanner_, ble_queue_.handle, CAT_COLLAR_INSTANCE_ID, sys::PRIORITY_PET_TRACKING, &app_manager_
          )
        , wifi_service_(wifi_config_) {}

    // The single entry point to kick off the application logic
    void start() {
        ESP_LOGI(TAG, "Booting Selective Pet Access System...");

        // 1. Signal a successful boot sequence (e.g., Solid green)
        status_led_.set_static(0, 30, 0);

        // 2. Initialize NVS (Required for the Bluetooth Controller baseband and Wi-Fi calibration storage)
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);

        // 3. Network stack: lwIP + default event loop + the STA netif.
        // WiFiStationService deliberately does NOT call these — they are system-wide singletons.
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == nullptr) {
            ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta returned nullptr; aborting boot.");
            abort();
        }

        // 4. Hardware Initialization for Sensors & Actuators
        esp_err_t dist_init_err = distance_sensor_.initialize();
        if (dist_init_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize VL53L0X distance sensor: %s", esp_err_to_name(dist_init_err));
            diagnostics_.push_error_record(services::ErrorSource::DistanceSensor, dist_init_err);
        }
        esp_err_t servo_init_err = lid_servo_.initialize();
        if (servo_init_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize servo: %s", esp_err_to_name(servo_init_err));
            diagnostics_.push_error_record(services::ErrorSource::Servo, servo_init_err);
        }

        // 5. Start Services
        esp_err_t lid_init_err = lid_controller_.initialize();
        if (lid_init_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize LidController: %s", esp_err_to_name(lid_init_err));
            diagnostics_.push_error_record(services::ErrorSource::LidController, lid_init_err);
        }
        telemetry_service_.start();

        // 6. Start the Orchestrator FSM Task
        ESP_LOGI(TAG, "Starting Application Manager FSM...");
        app_manager_.start();

        // 7. Initialize NimBLE Stack & Start Tracking
        ESP_LOGI(TAG, "Initializing BLE Subsystem...");
        esp_err_t ble_init_err = ble_scanner_.initialize();
        if (ble_init_err == ESP_OK) {
            // This will start scanning and feeding events into the queue,
            // which the tracker parses, and eventually triggers the Orchestrator callback.
            pet_tracker_.start();
            ESP_LOGI(TAG, "System Fully Armed and Operational.");
        } else {
            ESP_LOGE(
                TAG, "CRITICAL: Failed to initialize BLE stack: %s. Tracker offline.", esp_err_to_name(ble_init_err)
            );
            diagnostics_.push_error_record(services::ErrorSource::BleScanner, ble_init_err);
            status_led_.set_static(50, 0, 0);  // Set LED to Red to indicate failure
        }

        // 8. Bring up the Wi-Fi station (non-blocking; FSM task drives the connection lifecycle).
        ESP_LOGI(TAG, "Starting Wi-Fi Station Service...");
        esp_err_t wifi_err = wifi_service_.start();
        if (wifi_err != ESP_OK) {
            ESP_LOGE(TAG, "WiFiStationService start failed: %s", esp_err_to_name(wifi_err));
            diagnostics_.push_error_record(services::ErrorSource::WiFi, wifi_err);
        }
    }

private:
    // --- 1. Infrastructure & Configs ---
    // RAII Wrapper to guarantee the FreeRTOS Queue is created BEFORE injection
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

    const ui::SmartLedConfig led_config_{
        .pin = board::PIN_LED_STRIP,
        .rmt_resolution_hz = board::LED_RMT_RES_HZ,
        .task_priority = sys::PRIORITY_UI_LED,
        .task_core = 1,
        .task_stack_size = 3072
    };

    const actuators::LedcServo::Config servo_config_ = {
        .pin = board::PIN_SERVO,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .min_pulse_us = 500,
        .max_pulse_us = 2400,
        .max_angle_deg = 180.0f
    };

    // SSID/password come from menuconfig; the component itself never #includes sdkconfig.h.
    const network::WiFiStationService::Config wifi_config_{
        .ssid = CONFIG_PET_WIFI_SSID,
        .password = CONFIG_PET_WIFI_PASSWORD,
        .initial_backoff_ms = 1000,
        .max_backoff_ms = 60'000,
        .task_priority = sys::PRIORITY_WIFI_STATION,
        .task_core = 1,
        .task_stack_size = 4096
    };

    // --- 2. Low-Level Hardware Buses & Drivers ---
    bluetooth::NimbleScanner ble_scanner_;
    i2c::I2CMasterBus i2c_bus_;
    ui::SmartLed status_led_;
    sensors::AHT25 temp_humidity_sensor_;
    sensors::VL53L0X distance_sensor_;
    actuators::LedcServo lid_servo_;

    // --- 3. Cross-Cutting Sink (declared first so middleware services can take it by ref) ---
    services::DiagnosticsService diagnostics_;

    // --- 4. Middleware Services ---
    services::TelemetryService telemetry_service_;
    services::LidController lid_controller_;

    // --- 5. Core Business Logic (Orchestrator) ---
    // Must be declared after services so they are fully constructed before injection
    ApplicationManager app_manager_;

    // --- 6. High-Level Tracker ---
    // Must be declared after app_manager_ so it can receive the IProximityObserver pointer safely
    tracking::PetProximityTracker pet_tracker_;

    // --- 7. Network ---
    // Wi-Fi station manager. Caller (start()) is responsible for esp_netif_init,
    // esp_event_loop_create_default, and esp_netif_create_default_wifi_sta before wifi_service_.start().
    network::WiFiStationService wifi_service_;
};

}  // namespace pet_access::core

// =============================================================================
// ESP-IDF Entry Point
// =============================================================================

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Hardware initialized. Booting C++ context...");

    // Static Allocation: Placed in .bss segment (SRAM), ZERO heap fragmentation.
    static pet_access::core::SystemController system_app;

    system_app.start();

    // Reclaim memory: The main task has done its job of organizing the system.
    ESP_LOGI(TAG, "Boot complete. Terminating app_main task to save RAM.");
    vTaskDelete(nullptr);
}