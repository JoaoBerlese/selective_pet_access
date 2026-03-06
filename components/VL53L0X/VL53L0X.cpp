#include "VL53L0X.hpp"

#include <esp_log.h>

namespace pet_access::sensors {

static const char* VL53L0X_TAG = "VL53L0X";

// =============================================================================
// Lifecycle & Hardware Initialization
// =============================================================================

VL53L0X::VL53L0X(i2c::I2CMasterBus& i2c_bus, uint8_t i2c_address) : i2c_bus_(i2c_bus), i2c_address_(i2c_address) {
    ESP_LOGI(VL53L0X_TAG, "VL53L0X constructed at I2C address 0x%02X", i2c_address_);
}

esp_err_t VL53L0X::initialize() {
    // Note: The VL53L0X requires a notoriously complex sequence of "magic number"
    // register writes derived from ST's official API to calibrate the SPADs.
    // For brevity in the single-shot proof-of-concept, we assume baseline defaults,
    // but a production driver needs the Pololu/ST init array loaded here.

    // Tightly pack the register commands
    struct InitCmd {
        uint8_t reg;
        uint8_t val;
    };

    // Array of InitCmd structs forced into Flash (.rodata)
    static constexpr InitCmd init_sequence[] = {
        // reg   val
        {0x88, 0x00},  // Set I2C standard mode (TCC)

        // --- ST's undocumented sequence to set the "Stop Variable" ---
        {0x80, 0x01},  // Enable access to hidden registers
        {0xFF, 0x01},  // Enable access to hidden registers
        {0x00, 0x00},  // Disable 2.8V MAC I2C behavior

        {0x91, 0x3C},  // Set Stop Variable (SPAD config). The actual target write.

        {0x00, 0x01},  // Restore 2.8V MAC I2C behavior
        {0xFF, 0x00},  // Disable access to hidden registers
        {0x80, 0x00}   // Disable access to hidden registers
    };

    // Data-Driven Loop
    for (const auto& cmd : init_sequence) {
        esp_err_t ret = write_reg(cmd.reg, cmd.val);
        if (ret != ESP_OK) {
            // Dynamic logging pinpointing exactly where the failure occurred
            ESP_LOGW(VL53L0X_TAG, "Init Failed at REG 0x%02X: %s", cmd.reg, esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t VL53L0X::read_single_shot(uint16_t& out_distance_mm) {
    esp_err_t ret;

    // 1. Trigger Single Shot Measurement (write 0x01 to SYSRANGE_START)
    ret = write_reg(0x00, 0x01);
    if (ret != ESP_OK) {
        ESP_LOGW(VL53L0X_TAG, "Failed to trigger single shot measurement: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Yield to RTOS while sensor fires laser and calculates (Timing Budget)
    // The I2C bus mutex is unlocked here, allowing other tasks to use the bus while we wait.
    vTaskDelay(pdMS_TO_TICKS(TIMING_BUDGET_MS));

    // 3. Read the 16-bit distance result (Register 0x14 + 10 bytes offset = 0x1E)
    // Note: ST stores the distance 10 bytes deep into the RESULT_RANGE_STATUS block.
    ret = read_reg16(REG_RESULT_RANGE_STATUS + 10, out_distance_mm);
    if (ret != ESP_OK) {
        ESP_LOGW(VL53L0X_TAG, "Failed to read distance result: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Clear the interrupt to prepare for the next measurement (write 0x01 to SYSTEM_INTERRUPT_CLEAR)
    ret = write_reg(REG_SYSTEM_INTERRUPT_CLEAR, 0x01);

    return ESP_OK;
}

// =============================================================================
// Private Helper Functions
// =============================================================================

esp_err_t VL53L0X::write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_bus_.write(i2c_address_, data, sizeof(data), pdMS_TO_TICKS(100));
}

esp_err_t VL53L0X::read_reg16(uint8_t reg, uint16_t& out_value) {
    uint8_t data[2];
    // Write register address first and read 2 bytes back in a single transaction to ensure atomicity
    esp_err_t ret = i2c_bus_.write_read(i2c_address_, &reg, 1, data, sizeof(data), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    // Correct Big-Endian to Little-Endian unpacking
    out_value = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    return ESP_OK;
}

}  // Namespace pet_access::sensors
