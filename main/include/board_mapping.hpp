/**
 * @file board_mapping.hpp
 * @author João Berlese
 * @brief Centralized GPIO and Hardware Mapping for the Selective Pet Access System
 *
 * This header serves as the single source of truth for all hardware pin assignments and related constants.
 * By centralizing this information, we ensure that any changes to the hardware layout only need to
 * be made in one place, reducing the risk of inconsistencies and making the codebase easier to maintain.
 */

#pragma once

#include <driver/i2c_types.h>  // Gives us access to i2c_port_num_t
#include <hal/gpio_types.h>    // Gives us access to gpio_num_t

#include <cstdint>

namespace pet_access::board {

// =============================================================================
// UI & Indicators
// =============================================================================
constexpr gpio_num_t PIN_LED_STRIP = GPIO_NUM_48;
constexpr uint32_t LED_RMT_RES_HZ = 10'000'000;

// =============================================================================
// Actuators (Motor / Door)
// =============================================================================
// constexpr gpio_num_t PIN_MOTOR_PWM  = GPIO_NUM_10;
// constexpr gpio_num_t PIN_RELAY_MAIN = GPIO_NUM_12;

// =============================================================================
// Sensors (I2C)
// =============================================================================
constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_1;
constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_2;

}  // namespace pet_access::board