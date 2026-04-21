# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Selective Pet Access** is safety-critical firmware for an automated pet door/feeder running on an **ESP32-S3-N16R8** microcontroller. It uses C++20, FreeRTOS SMP, and ESP-IDF v5.3.4. Key behaviors: BLE beacon scanning for pet proximity detection, servo-based lid control, and I2C sensor telemetry (temperature, humidity, feed level).

## Build & Flash Commands

All builds run inside the Docker Dev Container (VS Code opens it automatically via `.devcontainer/`). No local ESP-IDF installation is required.

```bash
idf.py build                          # Build firmware
idf.py fullclean && idf.py build      # Full rebuild from scratch
idf.py -p /dev/ttyACM0 flash         # Flash to device
idf.py -p /dev/ttyACM0 flash monitor # Flash and view live logs (Ctrl+] to stop)
idf.py menuconfig                     # Interactive hardware config UI
idf.py save-defconfig                 # Persist config changes to sdkconfig.defaults
```

Debugging uses GDB + OpenOCD via hardware JTAG breakpoints — press **F5** in VS Code.
Do not use `printf()` for debugging. Use ESP-IDF's `ESP_LOGI`, `ESP_LOGE`, etc., with appropriate component tags. For fatal state machine errors, rely on `ESP_ERROR_CHECK` to trigger the Core Dump partition for post-mortem GDB analysis.

Code style is enforced by `.clang-format` (Google style, 4-space indent, 120 columns). It runs automatically on save in VS Code. Manual: `clang-format -i <file>`.

## Architecture

The system follows a **layered hexagonal architecture** assembled in `main/main.cpp` via dependency injection.

### Component Layers

1. **HAL Drivers** (`components/`): Thin wrappers around ESP-IDF peripherals — `ledc_servo`, `i2c_bus`, `AHT25`, `VL53L0X`, `smart_led`, `nimble_scanner`.
2. **Services** (`telemetry_service/`, `lid_controller/`): Stateful business logic operating on top of HAL drivers.
3. **Tracking** (`pet_tracking/`): `PetProximityTracker` translates raw BLE RSSI/beacon events into proximity state changes and notifies observers via `IProximityObserver`.
4. **Orchestrator** (`application_manager/`): `ApplicationManager` is the core FSM. It receives proximity events and drives the lid and LED based on state.
5. **Entry Point** (`main/main.cpp`): `SystemController` — constructs all components and wires them together.

### FSM States
```
Standby ──> Approaching ──> MealInProgress
  ▲               └──────────────┘
  └─── (5s inactivity timeout from any state)
Fault  (hardware jam reserved state)
```

### FreeRTOS Task Priorities (higher = harder deadline)
| Priority | Task | Core |
|----------|------|------|
| 7 | `PetProximityTracker` (BLE event parsing) | 0 |
| 6 | `ApplicationManager` (FSM transitions) | 1 |
| 5 | `LidController` (servo PWM loop @ 50Hz) | 1 |
| 4 | `SmartLed` (visual feedback) | 1 |
| 3 | `TelemetryService` (sensor poll @ 60s) | 1 |

Task priorities are defined in `main/include/sys_config.hpp`. GPIO pin assignments are the single source of truth in `main/include/board_mapping.hpp`.

### Cross-Component Communication
- **BLE events** flow from `NimBleScanner` → `PetProximityTracker` via a statically-allocated `BleEventQueue` (zero-heap, cross-core safe).
- **Proximity state changes** flow from `PetProximityTracker` → `ApplicationManager` via the `IProximityObserver` interface.
- **I2C bus** is shared between `AHT25` and `VL53L0X` via a thread-safe `i2c_bus` component (mutex-protected).

## Coding Standards

- **Modern C++20**: Use `constexpr`, `std::optional`, `std::span`. No `new`/`malloc` in application code.
- **Static allocation**: FreeRTOS resources (tasks, mutexes, queues) allocated in `.bss` using static variants to avoid heap fragmentation.
- **RAII is Law:** Manage all RTOS handles, hardware state, and Mutexes via constructors/destructors. **Never** use raw `xSemaphoreTake` or `xSemaphoreGive` in application logic. Instead, strictly use our custom `pet_access::rtos::LockGuard` in conjunction with `pet_access::rtos::StaticMutex`. 
  - *Architectural Justification:* The `LockGuard` guarantees mutexes are released when a stack frame unwinds (e.g., early returns on `esp_err_t` checks), entirely eliminating the deadlocks common in C-style ESP-IDF code. 
  - *Memory Constraints:* `StaticMutex` forces zero-heap allocation (`xSemaphoreCreateMutexStatic`). You **must** ensure these synchronization primitives are instantiated in internal SRAM (DRAM), NOT in the 8MB Octal PSRAM. Placing OS primitives in PSRAM causes severe scheduler latency during context switches due to cache misses. 
  - Delete copy/move constructors for all resource-owning types (Rule of Five).
- **No global state**: All dependencies injected via constructor parameters.
- **Namespaces**: `pet_access::{bluetooth, sensors, actuators, services, tracking, core}`.
- **Thread safety**: Document thread-safety guarantees in public headers. Use `std::atomic` for shared state; `[[nodiscard]]` on error-returning functions.
- **Error Handling (Strict):** Exceptions and RTTI are **DISABLED** (`-fno-exceptions`, `-fno-rtti`). 
  - **HAL/IDF Layer:** Default to returning and checking `esp_err_t` for all hardware interactions. Do not build massive C++ wrapper objects just to hide ESP-IDF return codes. 
  - **Business Logic:** Use `std::optional` strictly for high-level APIs to eliminate C-style out-parameters (e.g., `std::optional<Temperature> get_reading()`).
- **Memory Architecture & PSRAM:** The ESP32-S3 has 8MB Octal PSRAM. While static/stack allocation is preferred, any large data buffers (e.g., OTA chunks, large BLE payload arrays) must be explicitly mapped to PSRAM using `EXT_RAM_BSS_ATTR` for static allocations, or `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` if dynamic allocation is absolutely unavoidable. Be mindful of cache coherence and data alignment when passing PSRAM buffers to DMA-backed peripherals (like SPI/I2C).

## Adding New Components

Follow the pattern described in `docs/05_Contributing.md` §4. New hardware drivers belong in `components/`, must expose an abstract interface, and are wired up in `main/main.cpp`. Register the new component's directory in the root `CMakeLists.txt`.

## Key Files

- `main/main.cpp` — Assembly point; read this first to understand all dependencies.
- `main/include/sys_config.hpp` — Task priorities.
- `main/include/board_mapping.hpp` — All GPIO pin assignments.
- `sdkconfig.defaults` — Hardware configuration source of truth (not `sdkconfig`, which is generated).
- `partitions.csv` — Custom partition table optimized for 16MB Flash: Bootloader, Factory App, Dual OTA partitions, Large LittleFS (strictly replacing deprecated SPIFFS), and a dedicated Core Dump partition.

