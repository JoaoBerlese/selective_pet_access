/**
 * @file VL53L0X.hpp
 * @author João Berlese
 * @brief C++20 RAII Minimal Wrapper for VL53L0X ToF Sensor using I2C Master Bus
 */
#pragma once

#include <esp_err.h>

#include <cstdint>

#include "I2CMasterBus.hpp"

namespace pet_access::sensors {

class VL53L0X {
public:
    /**
     * @brief Construct a new VL53L0X object
     * @param i2c_bus     Reference to the shared, mutex-protected I2C Master Bus
     * @param i2c_address The I2C address (Default is 0x29)
     */
    VL53L0X(i2c::I2CMasterBus& i2c_bus, uint8_t i2c_address = 0x29);
    ~VL53L0X() = default;

    // Rule of Five: Delete Copy/Move to prevent hardware state duplication
    VL53L0X(const VL53L0X&) = delete;
    VL53L0X& operator=(const VL53L0X&) = delete;
    VL53L0X(VL53L0X&&) = delete;
    VL53L0X& operator=(VL53L0X&&) = delete;

    /**
     * @brief Performs the minimal ST "magic number" initialization
     */
    [[nodiscard]] esp_err_t initialize();

    /**
     * @brief Performs a Single-Shot distance measurement
     * @param out_distance_mm Reference to store the read distance in millimeters
     * @return ESP_OK if successful, otherwise an error code
     */
    [[nodiscard]] esp_err_t read_single_shot(uint16_t& out_distance_mm);

private:
    i2c::I2CMasterBus& i2c_bus_;
    uint8_t i2c_address_;

    // Essential Registers for Single Shot Read
    static constexpr uint8_t REG_SYSRANGE_START = 0x00;
    static constexpr uint8_t REG_RESULT_RANGE_STATUS = 0x14;
    static constexpr uint8_t REG_SYSTEM_INTERRUPT_CLEAR = 0x0B;

    // Default timing budget for high speed single shot (in milliseconds)
    static constexpr uint32_t TIMING_BUDGET_MS = 33;

    // Helper functions to reduce I2C boilerplate
    esp_err_t write_reg(uint8_t reg, uint8_t value);
    esp_err_t read_reg16(uint8_t reg, uint16_t& out_value);
};
}  // Namespace pet_access::sensors