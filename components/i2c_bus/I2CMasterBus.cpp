#include "I2CMasterBus.hpp"

#include <esp_log.h>

namespace pet_access::i2c {

static const char* TAG = "I2CMasterBus";

// =============================================================================
// Lifecycle & Hardware Initialization
// =============================================================================

I2CMasterBus::I2CMasterBus(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin, uint32_t clk_speed_hz)
    : port_(port) {
    // Note: bus_mutex_ is already constructed safely before we enter this block.

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda_pin;
    conf.scl_io_num = scl_pin;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = clk_speed_hz;

    esp_err_t err = i2c_param_config(port_, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C parameters: %s", esp_err_to_name(err));
        abort();
    }

    // =========================================================================
    // HARDWARE TIMEOUT FIX (ESP32-S3 SPECIFIC ARCHITECTURE)
    // =========================================================================
    // Configures the hardware peripheral timeout to prevent ISR lockups and
    // subsequent Watchdog panics if the I2C bus floats or gets stuck low.
    // This guarantees the ISR aborts gracefully returning ESP_ERR_TIMEOUT.
    //
    // WARNING: Unlike the legacy ESP32 which accepts absolute APB clock cycles,
    // the ESP32-S3 timeout register uses a logarithmic scale: 2^X APB cycles.
    // At an 80MHz APB clock: 19 = 2^19 = 524,288 cycles (~6.5ms).
    // This allows the slow AHT25 to stretch the clock safely while maintaining
    // a strict hardware safety net.
    err = i2c_set_timeout(port_, 19);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not set I2C hardware timeout: %s", esp_err_to_name(err));
    }
    // =========================================================================

    err = i2c_driver_install(port_, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(err));
        abort();
    }

    ESP_LOGI(TAG, "I2C Master initialized on port %d (SDA: GPIO%d, SCL: GPIO%d)", port_, sda_pin, scl_pin);
}

I2CMasterBus::~I2CMasterBus() {
    esp_err_t err = i2c_driver_delete(port_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete I2C driver: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "I2C Master on port %d deinitialized successfully", port_);
    }
}

// =============================================================================
// Public API (Thread-Safe I2C Transactions)
// =============================================================================

esp_err_t I2CMasterBus::write(uint8_t device_addr, const uint8_t* data, size_t length, TickType_t timeout_ticks) {
    // Zero-cost RAII abstraction locking the bus
    rtos::LockGuard lock(bus_mutex_, timeout_ticks);
    if (!lock.is_acquired()) {
        return ESP_ERR_TIMEOUT;  // Caller must handle this
    }

    return i2c_master_write_to_device(port_, device_addr, data, length, timeout_ticks);
}

esp_err_t I2CMasterBus::read(uint8_t device_addr, uint8_t* data, size_t length, TickType_t timeout_ticks) {
    rtos::LockGuard lock(bus_mutex_, timeout_ticks);
    if (!lock.is_acquired()) {
        return ESP_ERR_TIMEOUT;  // Caller must handle this
    }

    return i2c_master_read_from_device(port_, device_addr, data, length, timeout_ticks);
}

esp_err_t I2CMasterBus::write_read(
    uint8_t device_addr,
    const uint8_t* write_data,
    size_t write_length,
    uint8_t* read_data,
    size_t read_length,
    TickType_t timeout_ticks
) {
    rtos::LockGuard lock(bus_mutex_, timeout_ticks);
    if (!lock.is_acquired()) {
        return ESP_ERR_TIMEOUT;  // Caller must handle this
    }

    return i2c_master_write_read_device(
        port_, device_addr, write_data, write_length, read_data, read_length, timeout_ticks
    );
}

}  // namespace pet_access::i2c