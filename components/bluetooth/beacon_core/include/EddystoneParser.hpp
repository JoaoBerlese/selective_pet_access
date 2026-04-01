/**
 * @file EddystoneParser.hpp
 * @author João Berlese
 * @brief Stateless utility class for parsing Eddystone frames from raw BLE advertisement data.
 * Zero-allocation protocol decoder for Eddystone UID and TLM beacons.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace pet_access::bluetooth {

enum class EddystoneFrameType : uint8_t {
    Unknown = 0x00,
    UID,  // Identity Frame (Who is this?)
    TLM   // Telemetry Frame (Battery/Health)
};

// A flat, trivially copyable struct representing the decoded business data
struct ParsedEddystoneData {
    EddystoneFrameType frame_type{EddystoneFrameType::Unknown};

    // Populated for UID frames
    std::array<uint8_t, 10> namespace_id{};
    std::array<uint8_t, 6> instance_id{};  // This is the "serial number" part of the UID frame (collar ID)

    // Populated for TLM frames
    uint16_t battery_mv{0};  // Battery voltage in millivolts
    float temperature_c{0};  // Temperature in Celsius
};

class EddystoneParser {
public:
    // Delete instantiation: This is a pure static utility class (stateless).
    EddystoneParser() = delete;

    /**
     * @brief Parses a raw BLE advertisement payload to extract Eddystone data.
     * @param payload A zero-cost bounds-safe view of the raw advertisement bytes.
     * @return std::optional containing the parsed data if an Eddystone frame was found,
     * or std::nullopt if the payload is malformed or not an Eddystone beacon.
     */
    [[nodiscard]] static std::optional<ParsedEddystoneData> parse(std::span<const uint8_t> payload);

private:
    // BLE constants mapped for Eddystone
    static constexpr uint8_t AD_TYPE_SERVICE_DATA_16BIT = 0x16;
    static constexpr uint8_t EDDYSTONE_UUID_LSB = 0xAA;
    static constexpr uint8_t EDDYSTONE_UUID_MSB = 0xFE;

    static constexpr uint8_t FRAME_TYPE_UID = 0x00;
    static constexpr uint8_t FRAME_TYPE_TLM = 0x20;

    // Sub-parsers for specific frame types
    static bool parse_uid(std::span<const uint8_t> service_data, ParsedEddystoneData& out_data);
    static bool parse_tlm(std::span<const uint8_t> service_data, ParsedEddystoneData& out_data);
};

}  // namespace pet_access::bluetooth