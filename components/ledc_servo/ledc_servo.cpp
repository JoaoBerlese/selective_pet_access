#include "ledc_servo.hpp"

#include <esp_log.h>

namespace pet_access::actuators {

static const char* TAG = "LedcServo";

// =============================================================================
// Lifecycle & Hardware Initialization
// =============================================================================

LedcServo::LedcServo(const Config& config) : config_(config), initialized_(false) {
    ESP_LOGI(TAG, "LedcServo constructed on GPIO %d (Timer %d, Channel %d)", config.pin, config.timer, config.channel);
}

LedcServo::~LedcServo() {
    if (initialized_) {
        // RAII Cleanup: Stop the PWM signal and leave the pin floating/low
        // ESP32-S3 only uses LEDC_LOW_SPEED_MODE.
        ESP_LOGI(TAG, "Releasing LEDC channel %d on GPIO %d", config_.channel, config_.pin);
        ledc_stop(LEDC_LOW_SPEED_MODE, config_.channel, 0);
    }
}

esp_err_t LedcServo::initialize() {
    if (initialized_) {
        ESP_LOGE(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Configure the LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,  // Required for S3
        .duty_resolution = PWM_RESOLUTION,
        .timer_num = config_.timer,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timer Config Failed: %s", esp_err_to_name(ret));
        return ret;  // Early return on failure
    }

    // Configure the LEDC channel
    ledc_channel_config_t channel_conf = {
        .gpio_num = config_.pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,  // Required for S3
        .channel = config_.channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = config_.timer,
        .duty = 0,  // Start with 0% duty cycle (servo at 0 degrees)
        .hpoint = 0,
        .flags = {.output_invert = 0}
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Channel Config Failed: %s", esp_err_to_name(ret));
        return ret;  // Early return on failure
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized Servo on GPIO %d (Timer %d, Channel %d)", config_.pin, config_.timer, config_.channel);

    return ESP_OK;
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t LedcServo::set_angle(uint8_t angle_deg) {
    // Check if initialized before allowing angle setting
    if (!initialized_) {
        ESP_LOGE(TAG, "Set Angle Failed: Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check for valid angle range
    if (angle_deg > config_.max_angle_deg) {
        ESP_LOGE(TAG, "Invalid Angle: %.d degrees (must be between 0 and %d)", angle_deg, config_.max_angle_deg);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t target_duty = calculate_duty(angle_deg);

    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, config_.channel, target_duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, config_.channel);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t LedcServo::sleep() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Sleep Failed: Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Stop the PWM generation and hold the pin LOW (0).
    // This removes the reference signal; the servo goes limp (0 torque).
    esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, config_.channel, 0);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter sleep mode: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Servo entered sleep mode (0 holding torque).");
    }

    return ret;
}

// =============================================================================
// Private Helper Functions
// =============================================================================

uint32_t LedcServo::calculate_duty(uint8_t angle_deg) const {
    // Linear mapping from angle (0 - max_angle) to pulse width (min_pulse - max_pulse)
    uint32_t pulse_range_us = config_.max_pulse_us - config_.min_pulse_us;
    uint32_t pulse_us = config_.min_pulse_us + ((pulse_range_us * angle_deg) / config_.max_angle_deg);

    // Map pulse width (us) to LEDC duty ticks
    uint32_t duty_ticks = (pulse_us * MAX_DUTY_TICKS) / PERIOD_US;

    return duty_ticks;
}

}  // Namespace pet_access::actuators