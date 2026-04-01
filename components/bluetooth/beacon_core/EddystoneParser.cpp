#include "EddystoneParser.hpp"

#include <cstring>  // For std::memcpy (optimized internally by GCC's built-in functions)

namespace pet_access::bluetooth {

// =============================================================================
// Public Interface
// =============================================================================

std::optional<ParsedEddystoneData> EddystoneParser::parse(std::span<const uint8_t> payload) {
    // Look for the Service Data AD structure with the Eddystone UUID
    size_t index = 0;

    // LTV (Length-Type-Value) parser loop
    while (index < payload.size()) {
        uint8_t length = payload[index];

        // Bounds check: 0-length indicates end of valid data.
        // Also ensure the length doesn't exceed our remaining span.
        if ((length == 0) || ((index + length) >= payload.size())) {
            break;  // Malformed packet or no more data
        }

        uint8_t ad_type = payload[index + 1];

        // 0x16 = Service Data - 16-bit UUID
        if (ad_type == AD_TYPE_SERVICE_DATA_16BIT) {
            // Check for Eddystone UUID (0xFEAA) which is sent Little-Endian (0xAA, 0xFE)
            if ((payload[index + 2] == EDDYSTONE_UUID_LSB) && (payload[index + 3] == EDDYSTONE_UUID_MSB)) {
                // Eddystone Service Data found, now parse the frame type and contents
                std::span<const uint8_t> service_data = payload.subspan(index + 4, length - 3);

                ParsedEddystoneData parsed_data{};
                if (!service_data.empty()) {
                    uint8_t frame_type = service_data[0];

                    if (frame_type == FRAME_TYPE_UID) {
                        if (parse_uid(service_data, parsed_data)) {
                            return parsed_data;
                        }
                    } else if (frame_type == FRAME_TYPE_TLM) {
                        if (parse_tlm(service_data, parsed_data)) {
                            return parsed_data;
                        }
                    }
                }
            }
        }

        index += length + 1;  // Advance to the next LTV structure
    }

    return std::nullopt;  // No valid Eddystone frame found
}

// =============================================================================
// Private Helper Functions
// =============================================================================

bool EddystoneParser::parse_uid(std::span<const uint8_t> service_data, ParsedEddystoneData& out_data) {
    // UID Frame have a fixed length of 20 bytes after the 2-byte UUID
    // 1 (Frame Type) + 1 (Tx Power) + 10 (Namespace) + 6 (Instance) + 2 (RFU)
    if (service_data.size() < 20) {
        return false;  // Malformed UID frame
    }

    out_data.frame_type = EddystoneFrameType::UID;

    // Namespace starts at offset 2, Instance starts at offset 12
    std::memcpy(out_data.namespace_id.data(), &service_data[2], 10);  // Copy Namespace ID
    std::memcpy(out_data.instance_id.data(), &service_data[12], 6);   // Copy Instance ID

    return true;
}

bool EddystoneParser::parse_tlm(std::span<const uint8_t> service_data, ParsedEddystoneData& out_data) {
    // TLM Frame have a fixed length of 14 bytes after the 2-byte UUID
    // 1 (Frame Type) + 1 (Version) + 2 (Battery Voltage) + 2 (Temperature) + 4 (Advertising PDU count) + 4 (Time since
    // boot)
    if (service_data.size() < 14) {
        return false;  // Malformed TLM frame
    }

    // TLM version must be 0x00 for unencrypted
    if (service_data[1] != 0x00) {
        return false;  // Unsupported TLM version
    }

    out_data.frame_type = EddystoneFrameType::TLM;

    // Battery voltage is 2 bytes, Big-Endian (offset 2 and 3)
    out_data.battery_mv = static_cast<uint16_t>((service_data[2] << 8) | service_data[3]);

    // Temperature is an 8.8 fixed-point notation, Big-Endian (offset 4 and 5)
    // We convert it to a standard float to make Business Logic easier.
    int16_t temp_raw = static_cast<int16_t>((service_data[4] << 8) | service_data[5]);  // Big-endian signed
    out_data.temperature_c = temp_raw / 256.0f;                                         // Convert to Celsius

    return true;
}

}  // namespace pet_access::bluetooth