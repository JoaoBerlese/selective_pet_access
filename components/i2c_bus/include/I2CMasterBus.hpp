/**
 * @file I2CMasterBus.hpp
 * @author João Berlese
 * @brief Modern C++ RAII Wrapper for ESP-IDF I2C Master with FreeRTOS Mutex Protection
 */

#pragma once

#include <driver/i2c.h>
#include <esp_err.h>

#include <cstdint>
// Injecting your RTOS primitives
#include "rtos.hpp"

namespace pet_access::i2c {
class I2CMasterBus {
public:
    // RAII constructor ensures hardware is ready to use upon successful instantiation
    I2CMasterBus(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint32_t clk_speed_hz = 100000);
    ~I2CMasterBus();

    // Rule of Five: Hardware resources and static OS primitives cannot be copied or moved.
    I2CMasterBus(const I2CMasterBus&) = delete;
    I2CMasterBus& operator=(const I2CMasterBus&) = delete;
    I2CMasterBus(I2CMasterBus&&) = delete;
    I2CMasterBus& operator=(I2CMasterBus&&) = delete;

    // [[nodiscard]] forces the caller to handle I2C NACKs and timeouts
    [[nodiscard]] esp_err_t write(
        uint8_t device_addr, const uint8_t* data, size_t length, TickType_t timeout_ticks = portMAX_DELAY
    );
    [[nodiscard]] esp_err_t read(
        uint8_t device_addr, uint8_t* data, size_t length, TickType_t timeout_ticks = portMAX_DELAY
    );

    // Convenience method for combined write-then-read transactions (e.g., register access)
    [[nodiscard]] esp_err_t write_read(
        uint8_t device_addr,
        const uint8_t* write_data,
        size_t write_length,
        uint8_t* read_data,
        size_t read_length,
        TickType_t timeout_ticks = portMAX_DELAY
    );

private:
    i2c_port_t port_;
    rtos::StaticMutex bus_mutex_;  // Ensures thread-safe access to the I2C bus
};

}  // namespace pet_access::i2c