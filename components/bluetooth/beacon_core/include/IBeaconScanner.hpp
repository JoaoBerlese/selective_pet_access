/**
 * @file IBeaconScanner.hpp
 * @author João Berlese
 * @brief Abstract interface for BLE beacon scanning, decoupling business logic from hardware-specific implementations.
 */
#pragma once

namespace pet_access::bluetooth {

// Abstract Base Class decoupling Business Logic from the ESP-IDF NimBLE HAL.
class IBeaconScanner {
public:
    // Virtual destructor is mandatory for abstract interfaces (Rule of Zero/Five).
    virtual ~IBeaconScanner() = default;

    // Starts the scanner. The underlying implementation will handle
    // the TDM windowing and coexistence intervals defined in sdkconfig.
    virtual void start() = 0;

    // Stops the scanner, releasing the 2.4GHz radio fully back to Wi-Fi.
    virtual void stop() = 0;
};

}  // namespace pet_access::bluetooth