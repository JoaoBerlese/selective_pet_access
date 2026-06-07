# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Selective Pet Access** is safety-critical firmware for an automated pet door/feeder running on an **ESP32-S3-N16R8** microcontroller. It uses C++20, FreeRTOS SMP, and ESP-IDF v5.3.4. Key behaviors: BLE beacon scanning for pet proximity detection, servo-based lid control, I2C sensor telemetry (temperature, humidity, feed level), and Wi-Fi STA connectivity (with exponential-backoff retry).

> **Branch policy — `inverted_logic`:** the access policy is flipped vs. `main`. The beacon is worn by the **blocked** cat (not the authorized one), the lid is **open by default**, and the FSM **closes** the lid while the beacon is at the feeder, reopening it once the cat departs. The `SystemState` enum identifiers are unchanged for minimal diff (`Standby`/`Approaching` = lid open; `MealInProgress` = lid closed / blocking). The meal-record ring buffer was renamed to `BlockRecord` / `block_buffer_` / `push_block_record` to reflect that it now records block durations, not meal durations. When working on this branch, preserve this inversion — do not "fix" log strings or lid calls back to the `main` semantics.

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
5. **Network** (`wifi_station/`): `WiFiStationService` provides non-blocking Wi-Fi STA connection with an exponential-backoff state machine. Lives in `pet_access::network`.
6. **Entry Point** (`main/main.cpp`): `SystemController` — constructs all components and wires them together.

### FSM States
```
Standby ──> Approaching ──> MealInProgress
  ▲               └──────────────┘
  └─── (5s inactivity timeout from any state)
Fault  (hardware jam reserved state)
```
On the `inverted_logic` branch the structure is unchanged but the lid mapping is flipped: `Standby`/`Approaching` keep the lid **open**, and `MealInProgress` drives the lid **closed** to block the beacon-wearing cat. The 5s inactivity timeout therefore reopens the lid (returning to default permissive) instead of closing it.

### FreeRTOS Task Priorities (higher = harder deadline)
| Priority | Task | Core |
|----------|------|------|
| 7 | `PetProximityTracker` (BLE event parsing) | 0 |
| 6 | `ApplicationManager` (FSM transitions) | 1 |
| 5 | `LidController` (servo PWM loop @ 50Hz) | 1 |
| 4 | `SmartLed` (visual feedback) | 1 |
| 3 | `TelemetryService` (sensor poll @ 60s) | 1 |
| 2 | `WiFiStationService` (Wi-Fi STA + exponential backoff) | 1 |

Task priorities are defined in `main/include/sys_config.hpp`. GPIO pin assignments are the single source of truth in `main/include/board_mapping.hpp`.

### Cross-Component Communication
- **BLE events** flow from `NimBleScanner` → `PetProximityTracker` via a statically-allocated `BleEventQueue` (zero-heap, cross-core safe).
- **Proximity state changes** flow from `PetProximityTracker` → `ApplicationManager` via the `IProximityObserver` interface.
- **I2C bus** is shared between `AHT25` and `VL53L0X` via a thread-safe `i2c_bus` component (mutex-protected).

## Coding Standards

> **AUTHORITATIVE RULEBOOK:** All C++ code in this project MUST conform to **`docs/06_Firmware_Design_Guidelines.md`** ("The Berlese Standard"). The bullets below are a quick-reference summary; whenever a question is not answered here, defer to Document 06 — it is the source of truth, and its self-audit checklist (§13.2) is the merge gate.

The five non-negotiable pillars (full detail in Document 06 §2):

- **RAII is Law.** Use `pet_access::rtos::StaticMutex` + `pet_access::rtos::LockGuard` for every mutex. **Never** call raw `xSemaphoreTake` / `xSemaphoreGive` in application code. Resource-owning types delete copy and move (Rule of Five). `StaticMutex` lives in DRAM, never in PSRAM (cache-miss latency on context switch). See Document 06 §4.
- **Zero-heap in steady state.** No `new` / `malloc` / `make_unique` / `make_shared` after boot. FreeRTOS primitives (queues, tasks) may be heap-allocated **only** during `SystemController` construction; after `app_main` exits, allocation must stop. Static/stack allocation is mandatory in task loops. See Document 06 §3.
- **`esp_err_t` at HAL → `std::optional<T>` at the service boundary.** Every error-returning function carries `[[nodiscard]]`. Exceptions and RTTI are disabled (`-fno-exceptions`, `-fno-rtti`); zero `try` / `catch` / `throw` / `dynamic_cast`. `ESP_ERROR_CHECK` is reserved for boot-time fatal init only. See Document 06 §5.
- **Concrete-first DI; abstract only when justified.** Concrete classes are the default for dependency injection — no `I*` interface unless §6 exception conditions apply (multiple runtime implementations, or a concrete test-mock requirement). Concrete classes are `final`; if an interface exists, the impl is `final : public IFoo`. References for mandatory dependencies, raw pointers for optional observer callbacks. No globals, no singletons — `SystemController` owns the world. See Document 06 §6.
- **`ESP_LOGx` only, with a per-file `static const char* TAG`.** Never `printf` / `std::cout` / `fprintf`. Severity matrix: `LOGE` for hard failure, `LOGW` for recoverable degradation, `LOGI` for lifecycle, `LOGD` for per-iteration debug. See Document 06 §7.

Standing rules (full detail in the cited sections):

- **PSRAM placement** — OS primitives stay in DRAM; large (≥ 1 KB) static buffers use `EXT_RAM_BSS_ATTR` only when justified. DMA targets stay in DRAM unless the driver explicitly supports `MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`. See Document 06 §3.2 and §3.5.
- **Inter-core state** — `std::atomic<T>` with explicit `memory_order_acquire` / `release`. Default `seq_cst` is too strong on the ESP32-S3. See Document 06 §4.7.
- **Task priorities** — declared once in `main/include/sys_config.hpp`; never hard-coded at the `xTaskCreatePinnedToCore` site. All application tasks pin to **Core 1**; Core 0 is reserved for the radio stack. See Document 06 §4.5.
- **Namespaces** — `pet_access::{board, sys, core, i2c, sensors, actuators, ui, services, bluetooth, tracking, rtos, network}`. Future: `pet_access::{cloud}`.
- **menuconfig workflow** — edit through `idf.py menuconfig`, then `idf.py save-defconfig`, then commit only the diff in `sdkconfig.defaults`. **Never** edit `sdkconfig` directly. See Document 06 §10.
- **Style** — `.clang-format` (Google base, 4-space, 120 col, `PointerAlignment: Left`, `BinPackParameters: false`) is enforced on save in VS Code. See Document 06 §11.

Before opening a PR, run through the self-audit checklist in **Document 06 §13.2** — every "no" is a merge blocker.

## Adding New Components

**The canonical template is `components/example_service/`.** Every new HAL driver, service, tracker, or orchestrator subsystem MUST start as a copy of these four files. This applies equally to human and agentic code generation — no other starting point is acceptable:

- `components/example_service/include/ExampleService.hpp` — standalone concrete `final` class with `ExampleSample`, `IExampleObserver`, `Config` struct, Rule-of-Five deletion, `StaticMutex`, atomics, trampoline declarations. **No service-level `I*` interface** — YAGNI default (Document 06 §6.1). Add `IExampleService.hpp` only when §6.1 exception conditions apply.
- `components/example_service/ExampleService.cpp` — implementation embodying the trampoline task pattern, `rtos::LockGuard` usage, atomic acquire/release publishing, and observer notification.
- `components/example_service/CMakeLists.txt` — `REQUIRES` (public types) vs `PRIV_REQUIRES log` split.

Workflow:

1. Copy the three files above into `components/<your_name>/` and rename. Read them top to bottom before editing — every line is load-bearing per **Document 06 §12**.
2. Add a priority constant to `main/include/sys_config.hpp` (**Document 06 §4.5**). Never hard-code priorities at task-creation sites.
3. Wire the new component into `SystemController` in `main/main.cpp`, respecting member declaration order (**Document 06 §6.5**). The wiring recipe — `Config` struct, member declaration position, MIL entry, `start()` call — is spelled out in **Document 06 §12.5**.
4. If your component lives in a sibling directory (like `components/bluetooth/`), append it to `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`.
5. Self-audit against the checklist in **Document 06 §13.2** before opening the PR.

The high-level walkthrough in `docs/05_Contributing.md` §4 remains a useful onboarding read, but **Document 06 §12 supersedes it** for any disagreement.

## Key Files

- `main/main.cpp` — Assembly point; read this first to understand all dependencies.
- `main/include/sys_config.hpp` — Task priorities.
- `main/include/board_mapping.hpp` — All GPIO pin assignments.
- `sdkconfig.defaults` — Hardware configuration source of truth (not `sdkconfig`, which is generated).
- `partitions.csv` — Custom partition table optimized for 16MB Flash: Bootloader, Factory App, Dual OTA partitions, Large LittleFS (strictly replacing deprecated SPIFFS), and a dedicated Core Dump partition.

