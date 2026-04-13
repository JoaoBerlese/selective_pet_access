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
// Note: ESP-IDF FreeRTOS configMAX_PRIORITIES defaults to 25.

// 1. Hardest Deadlines (Hardware / Baseband Interfacing)
constexpr UBaseType_t PRIORITY_PET_TRACKING = 7;  // BLE event parsing (Fast-path)

// 2. Decision Making (Business Logic)
constexpr UBaseType_t PRIORITY_ORCHESTRATOR = 6;  // FSM State transitions & Event Routing

// 3. Soft Real-Time (Actuation)
constexpr UBaseType_t PRIORITY_LID_CONTROLLER = 5;  // 50Hz Servo update loop

// 4. Background / UI
constexpr UBaseType_t PRIORITY_UI_LED = 4;     // Visual feedback
constexpr UBaseType_t PRIORITY_TELEMETRY = 3;  // Background I2C sensor polling

}  // namespace pet_access::sys