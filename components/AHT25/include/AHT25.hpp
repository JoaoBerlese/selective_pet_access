/**
 * @file AHT25.hpp
 * @author João Berlese
 * @brief C++20 RAII Wrapper for AHT25 Temperature and Humidity Sensor using ESP-IDF I2C Master Bus
 */
#pragma once

#include <esp_err.h>

#include <cstdint>

#include "I2CMasterBus.hpp"

namespace pet_access::sensors {

class AHT25 {
public:
    struct reading {
        float temperature;  // Celsius
        float humidity;     // RH
    };

    /**
     * @brief Construct a new AHT25 object via Dependency Injection
     * @param bus     Reference to the shared, mutex-protected I2C Master Bus
     * @param address The I2C address (Default is 0x38)
     */
    AHT25(i2c::I2CMasterBus& i2c_bus, uint8_t i2c_address = 0x38);
    ~AHT25() = default;

    // Rule of Five: Delete Copy/Move to prevent hardware state duplication
    AHT25(const AHT25&) = delete;
    AHT25& operator=(const AHT25&) = delete;
    AHT25(AHT25&&) = delete;
    AHT25& operator=(AHT25&&) = delete;

    /**
     * @brief Read the latest sensor values
     * @param out_reading Reference to store the read values
     * @return ESP_OK if successful, otherwise an error code
     */
    [[nodiscard]] esp_err_t read(reading& out_reading);

    /**
     * @brief Reset the sensor
     * @return ESP_OK if successful, otherwise an error code
     */
    [[nodiscard]] esp_err_t reset();

private:
    // Dependency Injection of the I2C Master Bus and sensor address
    i2c::I2CMasterBus& i2c_bus_;
    uint8_t i2c_address_;

    // Hardware constants
    static constexpr uint8_t CMD_TRIGGER = 0xAC;
    static constexpr uint8_t CMD_DATA_0 = 0x33;
    static constexpr uint8_t CMD_DATA_1 = 0x00;
    static constexpr uint32_t DELAY_MS = 80;
};
}  // namespace pet_access::sensors