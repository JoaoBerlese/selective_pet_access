#include "AHT25.hpp"

#include <esp_log.h>

namespace pet_access::sensors {

static const char* AHT25_TAG = "AHT25";

// =============================================================================
// Lifecycle & Hardware Initialization
// =============================================================================

AHT25::AHT25(i2c::I2CMasterBus& i2c_bus, uint8_t i2c_address) : i2c_bus_(i2c_bus), i2c_address_(i2c_address) {
    ESP_LOGI(AHT25_TAG, "sensor initialized at I2C address 0x%02X", i2c_address_);
}

// =============================================================================
// Public API (Reading & Resetting the Sensor)
// =============================================================================

// --- Read Method ---
esp_err_t AHT25::read(reading& out_reading) {
    uint8_t cmd[3] = {CMD_TRIGGER, CMD_DATA_0, CMD_DATA_1};

    // 1. Send Trigger (Protected by I2CMasterBus internal mutex)
    esp_err_t ret = i2c_bus_.write(i2c_address_, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(AHT25_TAG, "Trigger Failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Wait for measurement
    // (This delays the task, allowing the RTOS to run other tasks. The bus is FREE
    // during this time because your i2c_bus_.write() released the mutex!)
    vTaskDelay(pdMS_TO_TICKS(DELAY_MS));

    // 3. Read Data
    uint8_t data[6];
    ret = i2c_bus_.read(i2c_address_, data, sizeof(data), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(AHT25_TAG, "Read Failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Parse (Bit Magic)
    uint32_t raw_hum =
        (static_cast<uint32_t>(data[1]) << 12) | (static_cast<uint32_t>(data[2]) << 4) | ((data[3] & 0xF0) >> 4);

    uint32_t raw_temp = (static_cast<uint32_t>(data[3] & 0x0F) << 16) | (static_cast<uint32_t>(data[4]) << 8) |
                        static_cast<uint32_t>(data[5]);

    // 5. Convert using constexpr-friendly math
    out_reading.humidity = (static_cast<float>(raw_hum) / 1048576.0f) * 100.0f;
    out_reading.temperature = (static_cast<float>(raw_temp) / 1048576.0f) * 200.0f - 50.0f;

    return ESP_OK;
}

// --- Reset Method ---
esp_err_t AHT25::reset() {
    uint8_t cmd = 0xBA;
    return i2c_bus_.write(i2c_address_, &cmd, 1, pdMS_TO_TICKS(100));
}

}  // namespace pet_access::sensors
