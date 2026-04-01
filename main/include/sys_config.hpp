/**
 * @file sys_config.hpp
 * @author João Berlese
 * @brief System-wide configurations and constants for the Selective Pet Access System
 *
 * This header defines system-level constants, such as task priorities, that are used across multiple subsystems.
 * By centralizing these definitions, we ensure consistency and make it easier to manage changes to system-wide
 * settings.
 */

#pragma once

#include <freertos/FreeRTOS.h>

namespace pet_access::sys {

// --- Task Priorities (Higher number = Higher Priority) ---
constexpr UBaseType_t PRIORITY_PET_TRACKING = 7;  // Core business logic (high priority)
constexpr UBaseType_t PRIORITY_UI_LED = 5;        // Visual feedback (mid priority)

}  // namespace pet_access::sys