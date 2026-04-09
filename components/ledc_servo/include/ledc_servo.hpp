/**
 * @file ledc_servo.hpp
 * @author João Berlese
 * @brief Wrapper class for controlling a servo motor using the ESP-IDF LEDC PWM driver.
 */

#pragma once

#include <driver/ledc.h>
#include <esp_err.h>

#include <cstdint>

namespace pet_access::actuators {

class LedcServo {
public:
    struct Config {
        gpio_num_t pin;
        ledc_timer_t timer;
        ledc_channel_t channel;
        // TowerPro 9g (SG90) standard is typically 1000us to 2000us,
        // though some cheap clones stretch from 500us to 2400us.
        // These are parameterizable so you can tune them per physical unit.
        uint32_t min_pulse_us = 1000;  // Minimum pulse width in microseconds
        uint32_t max_pulse_us = 2000;  // Maximum pulse width in microseconds
        float max_angle_deg = 180.0f;  // Maximum angle corresponding to max_pulse_us
    };

    /**
     * @brief Construct a new LedcServo object in a dormant state. The caller must call initialize() before use.
     * @param config The configuration parameters for the servo, including GPIO pin, timer, channel, and pulse widths.
     * The constructor sets up the LEDC timer and channel configuration but does not start the PWM
     */
    explicit LedcServo(const Config& config);

    /**
     * @brief Destructor acts as the RAII cleanup. If initialized, it stops
     * the PWM output to prevent erratic servo behavior on restart.
     */
    ~LedcServo();

    // Rule of Five: Delete Copy/Move to prevent hardware state duplication
    LedcServo(const LedcServo&) = delete;
    LedcServo& operator=(const LedcServo&) = delete;
    LedcServo(LedcServo&&) = delete;
    LedcServo& operator=(LedcServo&&) = delete;

    /**
     * @brief Initializes the LEDC timer and channel based on the provided configuration.
     * This method must be called before set_angle() to start the PWM output.
     * @return ESP_OK if initialization is successful, otherwise an error code
     */
    [[nodiscard]] esp_err_t initialize();

    /**
     * @brief Sets the angle of the servo.
     * @param angle_degrees Angle between 0.0f and config.max_angle_deg.
     * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if not init.
     */
    [[nodiscard]] esp_err_t set_angle(float angle_deg);

    /**
     * @brief Stops the PWM signal, putting the servo into a low-power standby state.
     * @warning The servo will lose all holding torque while in this state!
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t sleep();

private:
    Config config_;
    bool initialized_;  // Tracks whether initialize() has been successfully called

    static constexpr uint32_t PWM_FREQ_HZ = 50;
    static constexpr ledc_timer_bit_t PWM_RESOLUTION = LEDC_TIMER_14_BIT;

    // 2^14 = 16384. This represents the total number of ticks in a 20ms period.
    static constexpr uint32_t MAX_DUTY_TICKS = (1 << PWM_RESOLUTION);
    static constexpr uint32_t PERIOD_US = 1000000 / PWM_FREQ_HZ;  // 20000 us

    [[nodiscard]] uint32_t calculate_duty(float angle_deg) const;
};

}  // Namespace pet_access::actuators