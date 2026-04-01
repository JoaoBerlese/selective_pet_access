/**
 * @file BeaconTypes.hpp
 * @author João Berlese
 * @brief Core data types for BLE beacon scanning and event handling.
 *
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace pet_access::bluetooth {

// Standard BLE MAC Address is 6 bytes.
// std::array provides zero-overhead bounds safety and value semantics over raw uint8_t[6].
using MacAddress = std::array<uint8_t, 6>;

// Standard Legacy BLE Advertisement max payload is 31 bytes.
constexpr size_t MAX_ADV_PAYLOAD_SIZE = 31;

// This struct MUST be trivially copyable. It will be copied by value into a
// FreeRTOS Queue to cross the Core 0 (NimBLE) to Core 1 (Application) boundary.
struct BeaconEvent {
    MacAddress mac;
    int8_t rssi;
    uint8_t payload_length;
    std::array<uint8_t, MAX_ADV_PAYLOAD_SIZE> payload;

    // Zero-cost abstraction: Creates a lightweight view of the active payload.
    // The EddystoneParser will take this span, completely avoiding heap allocation
    // or deep copies during protocol decoding.
    [[nodiscard]] std::span<const uint8_t> get_payload_span() const {
        return std::span<const uint8_t>(payload.data(), payload_length);
    }
};

}  // namespace pet_access::bluetooth