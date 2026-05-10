# 06 · Firmware Design Guidelines — *The Berlese Standard*

**Role:** Reference manual for every contributor (human or agentic) writing C++ for the Selective Pet Access firmware. This document codifies the implicit standards already embodied in `components/rtos/`, `components/i2c_bus/`, `components/telemetry_service/`, `components/pet_tracking/`, and `components/application_manager/`. Future modules — including the upcoming WiFi and Cloud subsystems — must conform.

**Status:** Authoritative. Where this document and `05_Contributing.md` overlap, `05_Contributing.md` is the quick-reference; this file is the in-depth standard.

---

## 1 · Purpose & Audience

### 1.1 Why "The Berlese Standard"

The firmware runs on an ESP32-S3-N16R8 controlling safety-critical mechanical actuation (a lid that must open for the right pet and stay shut for the wrong one). A jam, a deadlock, or a heap-fragmentation panic at 3 a.m. is a real-world reliability problem, not a unit-test failure. Every rule in this document exists to prevent a class of run-time defect that is hard to reproduce, hard to debug under JTAG, and unacceptable in a deployed device.

The standard is built on five mutually-reinforcing tenets, each detailed in §2.

### 1.2 How to use this document

| Audience | How to read it |
|---|---|
| New human contributor | Read §2 once. Skim §3-§7. Bookmark §12 (the canonical example) and §13 (pre-commit checklist). |
| Senior contributor adding a subsystem | Read the full document. Use §12 as a copy-paste starting point. |
| Agentic coding assistant | Treat §2-§11 as constraints. Treat §12 as the reference output shape. Use §13 as the self-check before declaring "done". |
| Code reviewer | §13 is the audit checklist. Each numbered item maps back to a section. |

### 1.3 Glossary

- **HAL** — Hardware Abstraction Layer. Concrete C++ wrappers around ESP-IDF peripherals. Live under `components/AHT25/`, `components/VL53L0X/`, `components/i2c_bus/`, `components/ledc_servo/`, `components/smart_led/`, `components/bluetooth/nimble_scanner/`.
- **Service** — Stateful business logic above the HAL. Owns a FreeRTOS task. Examples: `TelemetryService`, `LidController`.
- **Tracker** — Stateful protocol decoder that turns raw HAL events into domain events. Example: `PetProximityTracker`.
- **Orchestrator** — The single FSM that ties everything together. Currently `ApplicationManager`.
- **Observer** — Single, optional, pointer-based callback. The Tracker pushes events; the Orchestrator consumes them. See `IProximityObserver` (`components/pet_tracking/include/PetProximityTracker.hpp:36-45`).

---

## 2 · Architectural Tenets at a Glance

### 2.1 The Five Pillars

```
┌─────────────────────────────────────────────────────────────────────┐
│ 1. RAII for every OS resource         (§4)  rtos::LockGuard,        │
│                                              rtos::StaticMutex      │
│ 2. Zero-heap in steady state          (§3)  Static/stack only.      │
│                                              Boot-time heap is OK.  │
│ 3. esp_err_t at HAL → optional<T>     (§5)  No exceptions, no RTTI. │
│    at the service boundary                                          │
│ 4. Concrete-first DI; abstract only   (§6)  No globals. Every dep   │
│    when §6.1 exception applies               flows through main.cpp │
│ 5. ESP_LOGx with a per-file TAG       (§7)  No printf. Ever.        │
└─────────────────────────────────────────────────────────────────────┘
```

Each pillar is tested in §13 by a single yes/no question. A PR that fails any of those questions is not mergeable.

### 2.2 Layered hexagonal map

```
┌──────────────────────────────────────────────────────────────────────┐
│                       app_main()  (main/main.cpp)                    │
│                                 │                                    │
│                                 ▼                                    │
│         SystemController  (composition root, MIL-driven DI)          │
│                                 │                                    │
│   ┌─────────────────────────────┼─────────────────────────────┐      │
│   ▼                             ▼                             ▼      │
│  Trackers                  Orchestrator                   Services   │
│  (BLE, WiFi)               (ApplicationManager FSM)   (Telemetry,   │
│       │                         │                       LidController│
│       │                         │                       SmartLed)    │
│       └─────────► IObserver ◄───┘                            │       │
│                                                              ▼       │
│                                                            HAL       │
│                                              (AHT25, VL53L0X,        │
│                                               LedcServo, I2C bus,    │
│                                               NimbleScanner)         │
└──────────────────────────────────────────────────────────────────────┘
```

Dependencies flow strictly downward. Higher layers never reach into lower layers except via the references handed to them at construction. The lowest layer (HAL) never knows the upper layers exist.

### 2.3 Threading model & core pinning policy

Task priorities are the single source of truth in `main/include/sys_config.hpp:15-34`:

| Priority | Symbol | Owner | Core | Notes |
|---|---|---|---|---|
| 7 | `PRIORITY_PET_TRACKING` | `PetProximityTracker` | 1 | Highest because it consumes the BLE event queue. |
| 6 | `PRIORITY_ORCHESTRATOR` | `ApplicationManager` | 1 | FSM transitions. |
| 5 | `PRIORITY_LID_CONTROLLER` | `LidController` | 1 | 50 Hz servo loop. |
| 4 | `PRIORITY_UI_LED` | `SmartLed` | 1 | Visual feedback. |
| 3 | `PRIORITY_TELEMETRY` | `TelemetryService` | 1 | 60 s poll cadence. |
| 2 | `PRIORITY_WIFI_STATION` | `WiFiStationService` | 1 | Event-driven Wi-Fi state machine with exponential backoff. |

**Core pinning rule:** All application tasks pin to **Core 1 (APP_CPU)**. Core 0 (PRO_CPU) is reserved for the NimBLE host task, the WiFi stack, and other ESP-IDF baseband work. **Do not** pin application tasks to Core 0 — you will starve the radio.

**Adding a new task?** Add the priority constant to `sys_config.hpp` first, then inject it via the constructor. Never hard-code priorities at task-creation sites.

---

## 3 · Memory & Allocation Discipline

### 3.1 Static allocation is the default

Every long-lived object is either:

1. A direct member of `SystemController`, which itself lives at `static pet_access::core::SystemController system_app;` in `main/main.cpp:173`. This places the entire object graph in `.bss` — DRAM, not PSRAM.
2. A `static constexpr` constant, almost always declared inside a class or in `sys_config.hpp` / `board_mapping.hpp`.
3. A stack variable inside a task loop. Stacks are bounded by the task's stack size (see §4.4).

There is exactly one `static` instance in the entire firmware: the `SystemController` itself (`main/main.cpp:169-180`). Everything else is reachable from it through composition.

### 3.2 When (and how) to use PSRAM — the `EXT_RAM_BSS_ATTR` rule

The board has 8 MB Octal PSRAM, but **as of this document no application buffer is mapped to PSRAM** — `grep -r EXT_RAM_BSS_ATTR components/ main/` returns zero hits. The current `sdkconfig` lets the FreeRTOS heap auto-spill allocations larger than `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (16 KB) into PSRAM, but no static buffer crosses that threshold today.

When you do need PSRAM (typical triggers: OTA chunk buffers, large JSON payloads, image frames), the rules are:

**DO**

- Use `EXT_RAM_BSS_ATTR` on **statically allocated** large buffers (≥ 1 KB long-lived):
  ```cpp
  static EXT_RAM_BSS_ATTR uint8_t ota_chunk_buffer[8192];
  ```
- Use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` only when dynamic allocation is unavoidable, and only outside the steady-state hot path.
- Document the placement in the header where the buffer is declared.

**DON'T**

- **Never** put OS primitives (`StaticSemaphore_t`, `StaticTask_t`, `StaticQueue_t`) in PSRAM. Cache misses on the context-switch path will destroy scheduler latency. This is the load-bearing reason `rtos::StaticMutex` is documented to live in DRAM (`components/rtos/include/rtos.hpp:18-22`).
- **Never** put DMA targets in PSRAM unless you've manually validated cache coherence and alignment. Most ESP-IDF DMA peripherals require DRAM.
- **Never** edit `sdkconfig` to change PSRAM thresholds without going through the menuconfig workflow in §10.

### 3.3 Forbidden: `new` / `malloc` / `make_unique` / `make_shared` in steady state

Search results across `components/` and `main/`:

```
components/bluetooth/nimble_scanner/NimbleScanner.cpp:178: // Rule #2: Do not use dynamic memory allocation (new/malloc).
```

That is the **only** mention, and it is a comment forbidding the practice. There is zero application use of `new`, `malloc`, `make_unique`, or `make_shared`.

**DO**

- Pre-allocate ring buffers as `std::array<T, N>` members. Reference: `ApplicationManager`'s offline meal buffer at `components/application_manager/include/ApplicationManager.hpp:92-96`:
  ```cpp
  static constexpr size_t MAX_OFFLINE_MEALS = 50;
  std::array<MealRecord, MAX_OFFLINE_MEALS> meal_buffer_{};
  ```
- Pass trivially-copyable structs by value through FreeRTOS queues. Reference: `BeaconEvent` at `components/bluetooth/beacon_core/include/BeaconTypes.hpp:25-37`.

**DON'T**

- Allocate inside a task loop, an ISR, or a NimBLE host callback.
- Reach for `std::vector`, `std::string`, `std::map`, or `std::function` on the hot path. They allocate by default.

### 3.4 The single permitted heap window

The "zero-heap" tenet has one explicit, codified exception: **boot-time RTOS primitive creation**. The following dynamic allocations happen exactly once, in the construction path executed from `app_main()`, and are deliberately allowed:

| Where | What | Why we accept it |
|---|---|---|
| `main/main.cpp:113` | `xQueueCreate(16, sizeof(BeaconEvent))` | One-shot at boot. Static variant would force ~600 B of additional `.bss` whether the queue is used or not. |
| `components/application_manager/ApplicationManager.cpp:19` | `xQueueCreate(10, sizeof(SystemEvent))` | Same rationale. |
| `components/smart_led/smart_led.cpp:31` | `xQueueCreate(QUEUE_LENGTH, sizeof(Message))` | Same rationale. |
| Every `xTaskCreatePinnedToCore` call site (5 currently) | Task stack | ESP-IDF allocates the stack from the FreeRTOS heap. Static stacks would inflate `.bss` by ~18 KB system-wide (see §4.4). |

**The contract is one-way:** these allocations happen during `SystemController::start()` and never again. After `app_main` calls `vTaskDelete(nullptr)` (`main/main.cpp:179`), no further heap allocation is permitted from application code. If you need a queue or task at runtime, you have a design problem — pre-create it at boot.

### 3.5 Cache-coherence reminders for DMA buffers

If you pass a buffer to a DMA-backed peripheral (SPI master, I2S, RMT, ADC continuous mode):

- The buffer must be in DRAM, not PSRAM (unless the driver explicitly supports `MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`).
- The buffer must be aligned to 4 bytes for most ESP32-S3 DMA paths; some peripherals require 16 or 32 bytes — check the peripheral driver header.
- After a write that the DMA engine will read, no manual cache flush is needed for DRAM buffers; the cache is coherent. For PSRAM-resident DMA buffers, you must call `Cache_WriteBack_Addr()` and friends — easier to just keep DMA buffers in DRAM.

---

## 4 · RTOS Primitives — The Only Acceptable APIs

### 4.1 `pet_access::rtos::StaticMutex`

Defined in `components/rtos/include/rtos.hpp:24-59`. Wraps `xSemaphoreCreateMutexStatic`; the `StaticSemaphore_t` control block lives inside the wrapper's footprint, so where the wrapper is placed determines where the primitive lives. **Owners must be placed in DRAM** — see §3.2.

Key API:

```cpp
class StaticMutex {
public:
    StaticMutex();                                                        // creates + checks
    ~StaticMutex();                                                       // releases
    [[nodiscard]] bool lock(TickType_t timeout_ticks = portMAX_DELAY);
    void unlock();
    // copy/move all = delete
};
```

You **never** call `lock()` / `unlock()` directly. Always wrap with `LockGuard` (§4.2).

### 4.2 `pet_access::rtos::LockGuard`

Defined in `components/rtos/include/rtos.hpp:61-88`. Guarantees mutex release on every code path — including early `return`s on `esp_err_t` checks.

Idiomatic usage from `components/i2c_bus/I2CMasterBus.cpp:71-79`:

```cpp
esp_err_t I2CMasterBus::write(uint8_t device_addr, const uint8_t* data, size_t length, TickType_t timeout_ticks) {
    rtos::LockGuard lock(bus_mutex_, timeout_ticks);
    if (!lock.is_acquired()) {
        return ESP_ERR_TIMEOUT;
    }
    return i2c_master_write_to_device(port_, device_addr, data, length, timeout_ticks);
}
```

The `is_acquired()` check is `[[nodiscard]]` and `noexcept`. Skip it only when you used `portMAX_DELAY` and have no other failure path to honour.

### 4.3 Forbidden: raw `xSemaphoreTake` / `xSemaphoreGive` / `vSemaphoreDelete`

These calls do not appear in any application file other than the wrapper itself (`components/rtos/include/rtos.hpp`). The only legitimate exception is integrating a third-party C library that hands you a raw `SemaphoreHandle_t` and you need to take it once during initialization (e.g. `NimbleScanner`'s `sync_semaphore_` at `components/bluetooth/nimble_scanner/NimbleScanner.cpp:25-31`). When this happens:

- Document why the wrapper isn't used in a comment above the raw call.
- Confine the raw call to the initialization path. Steady-state code never sees it.

### 4.4 Tasks: trampoline pattern + `xTaskCreatePinnedToCore` conventions

Every task in the codebase follows the same shape. Reference: `TelemetryService` at `components/telemetry_service/telemetry_service.cpp:28-83`.

```cpp
// In the .hpp:
private:
    static void task_entry(void* arg);
    void task_loop();
    TaskHandle_t task_handle_{nullptr};
    UBaseType_t task_priority_;
    BaseType_t task_core_;

// In the .cpp:
esp_err_t MyService::start() {
    if (task_handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,        // C-style entry, must be `static`
        "my_service",      // <= 16 chars (FreeRTOS truncates beyond)
        4096,              // stack size in bytes
        this,              // context pointer
        task_priority_,    // injected, not hard-coded
        &task_handle_,
        task_core_
    );
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void MyService::task_entry(void* arg) {
    auto* instance = static_cast<MyService*>(arg);
    instance->task_loop();   // hands off to the C++ instance method
}

void MyService::task_loop() {
    TickType_t last_wake_time = xTaskGetTickCount();
    while (true) {
        // ... do work ...
        xTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(period_ms));
    }
}
```

**Stack sizing:** Pick the smallest stack that survives Logging+ESP_LOG (which pushes `vsnprintf` on the stack — ~1 KB worst case). Current sizes: `lid_ctrl_task` 2048 B (no heavy logging), all others 4096 B. If you call into NimBLE, lwIP, or mbedTLS callbacks, plan for ≥ 6144 B.

**Naming:** Lowercase, ≤ 16 chars, ends in `_tsk` / `_task` for symmetry with FreeRTOS conventions in the wider ESP-IDF.

### 4.5 Task priorities — single source of truth in `sys_config.hpp`

All six existing task priorities live in `main/include/sys_config.hpp:21-34`. **Do not** hard-code priority literals in components. Add a new constant to `sys_config.hpp`, then inject it through the constructor.

Why centralization matters: the priority hierarchy is a global property — Core 1 has only one runnable task at a time, and the wrong relative priority can deadlock the FSM (e.g., if the orchestrator runs at higher priority than the tracker, the tracker can never publish events). Reviewing all six priorities in one file makes that hierarchy auditable.

### 4.6 Inter-task signaling: queues vs. `xTaskNotify` (when to pick which)

| Pattern | When to use | Example |
|---|---|---|
| `QueueHandle_t` + `xQueueSend(timeout=0)` | Multi-producer or multi-event-type. You need to buffer N events. | `BleEventQueue` (16 events of `BeaconEvent`) — `main/main.cpp:111-123` |
| `QueueHandle_t` + `xQueueSend(timeout=0)` from observer | Observer-to-orchestrator bridging. The observer must not block. | `ApplicationManager::on_proximity_changed` — `components/application_manager/ApplicationManager.cpp:57-66` |
| `xTaskNotify` + `xTaskNotifyWait` | Single-producer, single-consumer, one or two distinct command codes. Avoids queue overhead. | `LidController::open()` / `close()` — `components/lid_controller/lid_controller.cpp` |

**Drop policy:** When the queue is full, drop with a `LOGW` and continue. Never block on a queue send from an event callback or an ISR. Reference: `components/application_manager/ApplicationManager.cpp:62-65`.

### 4.7 Inter-core state: `std::atomic<T>` with explicit `memory_order`

Cross-core shared variables use `std::atomic<T>` with explicit memory orders. Default `memory_order_seq_cst` is too strong for the ESP32-S3 (it generates extra DSB barriers on every access).

Pattern from `components/pet_tracking/PetProximityTracker.cpp:294-300`:

```cpp
// Writer (publisher) side — release pairs with the reader's acquire.
proximity_state_.store(new_state, std::memory_order_release);
state_changed_ticks_.store(xTaskGetTickCount(), std::memory_order_release);
```

```cpp
// Reader (consumer) side — acquire ensures we see all writes that happened
// before the corresponding release.
return {
    proximity_state_.load(std::memory_order_acquire),
    current_rssi_.load(std::memory_order_acquire),
    /* ... */
};
```

Use `memory_order_relaxed` only for counters that don't synchronize with other state (e.g., the EMA filter at `components/pet_tracking/PetProximityTracker.cpp:209`).

### 4.8 Boot vs. steady-state: where heap is tolerated

Mirrors §3.4. Restated as a check the agent can apply at code-review time:

- **Boot path** (anything reachable from `app_main` → `SystemController::SystemController` → `SystemController::start`): heap allocation is allowed for FreeRTOS primitives (queues, tasks, semaphores).
- **Steady state** (anything reachable from a task's `task_loop` after the first `xTaskDelayUntil`): zero heap allocation. No exceptions.

If you cannot tell which side of the line you're on, you're on the wrong side. Pre-allocate it at boot.

---

## 5 · Error Handling

### 5.1 HAL Layer: `[[nodiscard]] esp_err_t` + early return

Every HAL method that touches hardware returns `esp_err_t` and is marked `[[nodiscard]]`. Reference: `components/i2c_bus/include/I2CMasterBus.hpp:30-38`, `components/AHT25/include/AHT25.hpp:42-48`, `components/ledc_servo/include/ledc_servo.hpp:54-68`, `components/VL53L0X/include/VL53L0X.hpp:35-42`.

Idiomatic call site (from `components/ledc_servo/ledc_servo.cpp:30-49`):

```cpp
esp_err_t LedcServo::initialize() {
    if (initialized_) {
        ESP_LOGE(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timer Config Failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // ... more checked HAL calls ...
    return ESP_OK;
}
```

**Always log with `esp_err_to_name(ret)`** — a numeric code in the logs is useless when JTAG isn't attached.

### 5.2 Service Layer: `std::optional<T>` — eliminating C-style out-parameters

Service-layer APIs return `std::optional<T>` instead of writing to an out-parameter. This forces the caller to decide what to do when the value is absent.

Reference: `TelemetryService::get_latest_data()` at `components/telemetry_service/telemetry_service.cpp:51-59`:

```cpp
std::optional<TelemetryData> TelemetryService::get_latest_data() {
    rtos::LockGuard lock(data_mutex_);
    if (!current_data_.is_valid) {
        return std::nullopt;
    }
    return current_data_;
}
```

Caller side (`components/application_manager/ApplicationManager.cpp:159-172`):

```cpp
auto maybe_data = telemetry_service_.get_latest_data();
if (maybe_data.has_value()) {
    latest_telemetry_ = maybe_data.value();
    // ...
}
```

Use `std::optional<T>` at every layer above HAL. Use `esp_err_t` at the HAL boundary only.

### 5.3 Initialization-only escape hatch: `ESP_ERROR_CHECK`

`ESP_ERROR_CHECK` panics on failure. It is acceptable **only** for:

1. Boot-time NVS initialization (`main/main.cpp:72, 75`).
2. Boot-time peripheral handle creation when there is no recovery strategy (`components/smart_led/smart_led.cpp:28` for RMT device creation).

Total occurrences in the codebase: 3. Adding more requires reviewer sign-off.

`ESP_RETURN_ON_ERROR` and `ESP_GOTO_ON_ERROR` are technically available but not currently used. The project prefers explicit `if (ret != ESP_OK)` blocks because they let you log a context-specific message before propagating.

### 5.4 Forbidden: exceptions, RTTI, `dynamic_cast`

The toolchain is configured with `-fno-exceptions -fno-rtti`. There are zero `try`, `catch`, `throw`, or `dynamic_cast` keywords in the codebase. If you find a third-party C++ library that requires exceptions, isolate it behind a C-shaped wrapper that converts thrown errors to `esp_err_t`.

### 5.5 Logging on the failure path

| Severity | When | Example |
|---|---|---|
| `ESP_LOGE` | Hardware failure that the system cannot recover from in this call. | `components/ledc_servo/ledc_servo.cpp:36` |
| `ESP_LOGW` | Recoverable degradation. The system continues with reduced fidelity (stale data, dropped event, retry). | `components/AHT25/AHT25.cpp:31` ("Trigger Failed") |
| `ESP_LOGI` | Lifecycle / FSM transitions. Not failure. | "System Fully Armed and Operational." (`main/main.cpp:101`) |
| `ESP_LOGD` | Per-iteration debug detail. Disabled in release builds. | Telemetry "State Updated" lines (`components/telemetry_service/telemetry_service.cpp:137`) |
| `ESP_LOGV` | Reserved. Not currently used. |

`fatal` is not a log level — it is `ESP_LOGE` followed by `abort()` (see `components/rtos/include/rtos.hpp:31-33` for the canonical pattern).

---

## 6 · Decoupling & Dependency Injection

### 6.1 Concrete-first DI — the YAGNI default

**Concrete classes are the default for dependency injection.** Do not create an abstract interface unless one of the two conditions in the exception rule below applies. Pass a concrete class by reference at the construction site; consumers reference the concrete type directly. This eliminates vtable overhead and avoids "interface-for-every-service" boilerplate that adds no value when exactly one implementation will ever exist.

**Exception — use an `I`-prefixed abstract interface only when:**

1. **Multiple implementations coexist at runtime.** The canonical example is `IBeaconScanner` / `NimbleScanner` (Strategy Pattern): the scanning algorithm can be swapped without changing the tracker. A second concrete class must be plausible and planned, not merely hypothetical.
2. **Unit-test isolation strictly requires a fake.** If a consumer cannot be tested without replacing the dependency at link time, an interface is justified. Document the test file that uses the mock; without a real test, the interface is YAGNI.

**Observer callbacks are always abstract.** If a component pushes events to an upstream consumer (the Observer pattern), the callback contract is expressed as an `IFooObserver` class with a pure-virtual method. This rule is orthogonal to the service-interface rule above.

When an interface **is** justified, it follows these rules:

- Uses the `I` prefix (`IBeaconScanner`, `IProximityObserver`).
- Declares `virtual ~IFoo() = default;` (mandatory).
- Does **not** delete copy/move on the interface itself — the concrete `final` class does that.
- Pure-virtual methods carry `[[nodiscard]]` if they return `esp_err_t` or `std::optional<T>`.

Reference interfaces in the codebase:

- `components/bluetooth/beacon_core/include/IBeaconScanner.hpp:11-22` (Strategy Pattern — multiple scanner implementations)
- `components/pet_tracking/include/PetProximityTracker.hpp:36-45` (`IProximityObserver` — observer callback)

### 6.2 Concrete classes are `final`

Concrete implementations:

- Are marked `final` to enable devirtualization and signal to readers that the type is a leaf.
- Inherit `: public IFoo` **only when** an abstract interface is justified per §6.1.
- Delete copy and move (Rule of Five) — they own a `TaskHandle_t`.

Reference (with interface — justified by Strategy Pattern): `class NimbleScanner final : public IBeaconScanner` at `components/bluetooth/nimble_scanner/include/NimbleScanner.hpp:20`.

Reference (without interface — YAGNI default): `class ExampleService final` at `components/example_service/include/ExampleService.hpp`.

### 6.3 Constructor injection: references for mandatory, pointers for optional

| Dependency type | Pass as | Rationale |
|---|---|---|
| Mandatory, non-null, lifetime-managed by `SystemController` | `T&` (reference) | Non-nullable by construction. Compiler enforces. |
| Optional callback (e.g. observer) | `T*` (raw pointer, may be `nullptr`) | The "I don't care about this event" case is meaningful. Null-check at the invocation site. |
| Owned (sole ownership) | Member by value | We don't have any. SystemController owns everything. |

References and pointers are **never** owning. There is no `std::unique_ptr` or `std::shared_ptr` in the codebase.

### 6.4 No singletons, no globals — `SystemController` owns the world

The only objects with static storage duration are:

1. The `SystemController` itself (`main/main.cpp:173`).
2. Per-file `static const char* TAG = "..."` for logging.
3. `static constexpr` constants in `sys_config.hpp` and `board_mapping.hpp`.

Anything else with `static` storage is a bug. Especially: `static` mutable state inside a class method, `static` instances of services, `static` mutexes outside the wrapper. If you think you need one, you need to inject something instead.

The one Sanctioned Static Pointer is `NimbleScanner::instance_` (`components/bluetooth/nimble_scanner/NimbleScanner.cpp:31`). It exists because the NimBLE stack's `on_stack_sync` callback has no `void*` argument. Document any new instance of this pattern with the same rationale.

### 6.5 Construction order is dependency order — the MIL is the truth

C++ guarantees that members are constructed in **declaration order**, not member-initializer-list order. The MIL is reordered silently by the compiler. The fix: write the declaration list and the MIL in the same order, and add a comment at the top of the constructor reminding the next reader.

Reference: `main/main.cpp:46-60`. The constructor begins with:

```cpp
// Warning: Initialization happens in the order members are DECLARED below.
SystemController()
    : ble_queue_()
    , ble_scanner_(ble_queue_.handle)   // depends on ble_queue_, declared later — works because of declaration order
    /* ... */ {}
```

A new component goes into both lists in the right place. If member A depends on member B, B must be declared first.

---

## 7 · Logging

### 7.1 `static const char* TAG` declaration

Every `.cpp` file that logs declares its TAG at file scope, at the top of the implementation namespace:

```cpp
namespace pet_access::services {

static const char* TAG = "ExampleService";

// ... rest of the file ...
}
```

The TAG is the class name, abbreviated to fit ESP-IDF's log column (typically ≤ 20 chars). Existing TAGs: `I2CMasterBus`, `LedcServo`, `VL53L0X`, `AHT25`, `SmartLED`, `TelemetryService`, `LidController`, `NimbleScanner`, `PetProximityTracker`, `AppManager`, `Main`.

Header-only components (e.g. `rtos.hpp`) use `inline constexpr const char* TAG = "...";` inside the namespace.

### 7.2 Severity matrix

See §5.5. Repeated here for the agent self-check:

```
ESP_LOGE  →  hard failure, no recovery this call
ESP_LOGW  →  recoverable failure, degraded mode
ESP_LOGI  →  lifecycle / state transition
ESP_LOGD  →  per-iteration debug; release build strips these
ESP_LOGV  →  unused; reserved
```

### 7.3 Forbidden: `printf`, `std::cout`, `std::cerr`, `fprintf`

Zero occurrences in `components/` and `main/`. `printf` bypasses the per-tag log level filter, floods the UART in tight loops, and cannot be redirected to a file in production. Reviewers reject any PR that introduces `printf`.

### 7.4 Per-tag log level via menuconfig

Per-tag log levels are controlled at build time. There are zero calls to `esp_log_level_set` in the codebase, by design. To raise a single tag to debug:

```
idf.py menuconfig
  /  → search "Default log verbosity"     (CONFIG_LOG_DEFAULT_LEVEL)
  /  → search "Use ANSI terminal colors"  (CONFIG_LOG_COLORS)
  /  → search "Maximum log verbosity"     (CONFIG_LOG_MAXIMUM_LEVEL)
```

To configure per-tag levels at boot, add the build flag through menuconfig and **then run `idf.py save-defconfig`** to persist the change. See §10.2 for the workflow.

---

## 8 · Namespacing & File Layout

### 8.1 Namespace hierarchy

```
pet_access
├── board       constants (pins, clocks, RMT resolution)
├── sys         system-wide constants (task priorities)
├── core        SystemController
├── i2c         I2C bus abstraction
├── sensors     AHT25, VL53L0X, future BME280, future battery monitor
├── actuators   LedcServo, future stepper drivers
├── ui          SmartLed, future display
├── services    TelemetryService, LidController, ExampleService, future cloud reporter
├── bluetooth   BeaconTypes, EddystoneParser, NimbleScanner, IBeaconScanner
├── tracking    PetProximityTracker, IProximityObserver
├── network     WiFiStationService, WiFiStatus, IWiFiObserver
└── rtos        StaticMutex, LockGuard
```

Future namespaces for upcoming work:

- `pet_access::cloud` — MQTT/HTTPS clients, telemetry uploaders, OTA bridge.

### 8.2 Component layout

```
components/<name>/
├── CMakeLists.txt
├── include/
│   └── <name>.hpp      (or multiple if you have a separate I*.hpp)
└── <name>.cpp
```

The `include/` subdirectory is the only public surface. Anything outside it is implementation detail. Other components depending on this one see only `include/` headers — see the CMake `INCLUDE_DIRS` / `PRIV_INCLUDE_DIRS` rules in §9.

### 8.3 Public vs. private headers

If you need to share a private helper header within a component but **not** expose it to dependents:

```cmake
idf_component_register(
    SRCS "MyDriver.cpp" "internal_helper.cpp"
    INCLUDE_DIRS "include"               # public
    PRIV_INCLUDE_DIRS "private_include"  # only visible inside this component
    REQUIRES driver
    PRIV_REQUIRES log
)
```

`PRIV_INCLUDE_DIRS` is currently unused but is the right tool when you start to need it.

---

## 9 · CMake & Component Encapsulation

### 9.1 `REQUIRES` vs `PRIV_REQUIRES` — the public-header rule

A dependency belongs in `REQUIRES` **only** if its types appear in your public header (`include/*.hpp`). Everything else goes in `PRIV_REQUIRES`.

Reference: `components/i2c_bus/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "I2CMasterBus.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos rtos    # public: gpio_num_t, TickType_t, StaticMutex appear in I2CMasterBus.hpp
    PRIV_REQUIRES log                # private: ESP_LOG only used in .cpp
)
```

### 9.2 Always `PRIV_REQUIRES log`

Logging is always implementation-only. Putting `log` in `REQUIRES` leaks it to dependents and inflates their compile graph. Every component with `ESP_LOGx` calls in its `.cpp` adds `PRIV_REQUIRES log`.

### 9.3 Adding a new component (3-step recipe)

```bash
# 1. Scaffold (from inside the dev container)
mkdir -p components/<name>/include
touch components/<name>/CMakeLists.txt
touch components/<name>/include/<Name>.hpp   # concrete header; see §12.2
touch components/<name>/<Name>.cpp
# Add I<Name>.hpp only if §6.1 exception conditions are met.

# 2. Copy the canonical CMakeLists.txt template (§12.4)
# 3. Wire it into SystemController in main/main.cpp (§12.5)
```

The root `CMakeLists.txt` auto-discovers `components/`. Sibling subdirectories (like `components/bluetooth/`) need a one-line addition to `EXTRA_COMPONENT_DIRS` — see the existing entry for `components/bluetooth` at `CMakeLists.txt:8`.

### 9.4 Root CMakeLists.txt: C++20, no GNU extensions

`CMakeLists.txt:1-11` enforces:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)        # forbids gnu++20
```

This is non-negotiable. C++20 enables `std::span`, `std::atomic<T>::wait`, designated initializers, and concepts — all of which are used or planned.

---

## 10 · Configuration & menuconfig Workflow

### 10.1 `sdkconfig` is generated; `sdkconfig.defaults` is the source of truth

`sdkconfig` is a build artifact. Editing it by hand is silently destructive — the next `idf.py reconfigure` will regenerate it from `sdkconfig.defaults` plus any menuconfig overrides. **Never** commit `sdkconfig`. **Always** commit `sdkconfig.defaults`.

The current `sdkconfig.defaults` (`/workspace/sdkconfig.defaults`):

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
```

### 10.2 The recipe

Whenever a feature requires a Kconfig change:

```bash
# 1. Open menuconfig
idf.py menuconfig

# 2. Find the option (use '/' to search, paste the CONFIG_* symbol)
#    e.g. /CONFIG_BT_NIMBLE_MAX_CONNECTIONS

# 3. Toggle / set the value, save, exit

# 4. Persist the change to sdkconfig.defaults
idf.py save-defconfig

# 5. Commit the diff in sdkconfig.defaults — NEVER the diff in sdkconfig
git add sdkconfig.defaults
```

Step 4 is the load-bearing step. Skipping it means your local build works but CI and your teammates' builds don't.

### 10.3 Per-feature menuconfig string reference (existing flags only)

This table covers only the flags currently in use. Future features (WiFi, MQTT, TLS) will be added in a separate update — see Appendix B.

| Symbol | Menuconfig path | Search string | Why we set it |
|---|---|---|---|
| `CONFIG_IDF_TARGET` | (set by `idf.py set-target esp32s3`) | n/a | Selects the ESP32-S3 toolchain. |
| `CONFIG_ESPTOOLPY_FLASHMODE_QIO` | `Serial flasher config → Flash SPI mode` | `Flash SPI mode` | Quad I/O — fastest flash read for the N16R8. |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` | `Serial flasher config → Flash size` | `Flash size` | The board has 16 MB. |
| `CONFIG_PARTITION_TABLE_CUSTOM` | `Partition Table → Partition Table → Custom partition table CSV` | `Partition Table` | We use `partitions.csv` (dual-OTA + 9 MB SPIFFS + 128 KB coredump). |
| `CONFIG_BT_ENABLED` | `Component config → Bluetooth → Bluetooth` | `Bluetooth` | Enables the BLE controller. |
| `CONFIG_BT_NIMBLE_ENABLED` | `Component config → Bluetooth → Host → NimBLE - BLE only` | `NimBLE - BLE only` | Selects NimBLE host (smaller than Bluedroid). |
| `CONFIG_BT_NIMBLE_ROLE_BROADCASTER` | `Component config → Bluetooth → NimBLE Options → BLE Role Configuration → Broadcaster Role` | `Broadcaster Role` | We are observer-only; broadcaster off saves flash and RAM. |
| `CONFIG_SPIRAM` | `Component config → ESP PSRAM → Support for external, SPI-connected RAM` | `Support for external` | Enables PSRAM driver. |
| `CONFIG_SPIRAM_MODE_OCT` | `Component config → ESP PSRAM → SPI RAM config → Mode (QUAD/OCT) of SPI RAM chip` | `Mode (QUAD/OCT)` | The N16R8 has Octal PSRAM. |

### 10.4 Forbidden: editing `sdkconfig` directly

Any PR that modifies `sdkconfig` without a corresponding `sdkconfig.defaults` change is rejected. The diff in `sdkconfig.defaults` is what your reviewer reads — `sdkconfig` is noise.

If you see drift between `sdkconfig` and `sdkconfig.defaults`, run `idf.py save-defconfig` and commit the result. If `sdkconfig` somehow contains a setting you don't want, delete it and `idf.py reconfigure`.

---

## 11 · Coding Style & Tooling

### 11.1 `.clang-format` is law

The full file lives at `/workspace/.clang-format`. Key choices:

| Setting | Value | Why |
|---|---|---|
| `BasedOnStyle` | Google | Reasonable default; widely understood. |
| `IndentWidth` | 4 | More readable in 120-col code than 2. |
| `ColumnLimit` | 120 | 80 is too narrow for verbose template/HAL signatures. |
| `PointerAlignment` | Left (`int* p`) | Type-carries-the-pointer; modern C++ convention. |
| `AlignAfterOpenBracket` | BlockIndent | Avoids excessive indent when wrapping long parameter lists. |
| `BinPackArguments` / `BinPackParameters` | false | If wrapping is needed, every argument goes on its own line. |
| `SortIncludes` | true | Standard → ESP-IDF → project, automatically. |
| `IndentRequiresClause` | true | C++20 concepts ready. |

VS Code in the dev container runs `clang-format -i` on save. Manually: `clang-format -i path/to/file.{cpp,hpp}`.

### 11.2 Modern C++20 features in scope

**In scope** (use freely):

- `std::optional<T>` for service-layer returns.
- `std::span<const uint8_t>` for buffer parameters (zero-copy view).
- `std::array<T, N>` for fixed-size buffers.
- `std::atomic<T>` with explicit memory orders for inter-core state.
- `[[nodiscard]]` on every error-returning function.
- `noexcept` on every function that genuinely cannot fail (queue posts, atomic loads).
- `constexpr` on all compile-time constants and pure functions.
- `static_cast<T>` always — never C-style casts.
- Designated initializers (`{.foo = 1, .bar = 2}`) for config structs.
- Structured bindings.

**Out of scope** (do not use):

- Exceptions (`try`/`catch`/`throw`).
- RTTI (`dynamic_cast`, `typeid`).
- `std::function` on the hot path (it allocates).
- `std::vector`, `std::string`, `std::map`, `std::set` — they allocate by default.
- `std::shared_ptr`, `std::unique_ptr` — we don't have heap-owning pointers.
- `iostream` (`cout`, `cerr`, `<<` for output).

### 11.3 Comment policy — the "why", not the "what"

Comments answer *why*, never *what*. The code already says what.

**DO**

```cpp
// Pinned to Core 1 (App Core) to avoid stealing cycles from the NimBLE stack on Core 0.
xTaskCreatePinnedToCore(/* ... */);
```

**DON'T**

```cpp
// Create a task pinned to core 1
xTaskCreatePinnedToCore(/* ... */);
```

Public APIs get doxygen comments (`@brief`, `@param`, `@return`, `@warning`). See `components/rtos/include/rtos.hpp` for the canonical style.

---

## 12 · The Standard Example Component

This component is the canonical reference. It exists for two reasons:

1. To give human contributors a copy-paste starting point.
2. To give agentic coding assistants a target shape — when generating a new service, the output should look like this.

It implements every tenet in §2 in one place: DI by reference, optional observer, `StaticMutex` + `LockGuard`, `std::atomic` with explicit memory order, `[[nodiscard]] esp_err_t` at HAL boundary, `std::optional<T>` at the service API, Rule of Five deletion, trampoline task pattern, injected priority/core, and zero heap in steady state.

### 12.1 Why no `IExampleService.hpp`

The canonical template deliberately omits a service-level abstract interface, applying the YAGNI default from §6.1: exactly one implementation of `ExampleService` will ever run on the device, and no unit test currently requires a fake. Adding `IExampleService` would:

- Introduce a vtable on every `start()` / `get_latest_sample()` call with no benefit.
- Force every consumer to include an extra header whose only purpose is describing a class that already exists.
- Create the false impression that the interface is a meaningful seam when it maps 1:1 to a single concrete class.

The observer callback (`IExampleObserver`) **is** kept abstract because it IS the extension point — consumers implement it to receive events. That is the observer pattern, which §6.1 explicitly permits.

**When you later need a service interface** (e.g., a second hardware variant appears, or a test demands a fake), the migration is four lines:

1. Create `include/IExampleService.hpp` with the pure-virtual surface (`start()`, `get_latest_sample()`).
2. Change `class ExampleService final` → `class ExampleService final : public IExampleService`.
3. Add `override` to the two public methods.
4. Update `SystemController` to hold `IExampleService&` instead of `ExampleService&`.

### 12.2 `components/example_service/include/ExampleService.hpp`

```cpp
#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <optional>

#include "I2CMasterBus.hpp"
#include "rtos.hpp"

namespace pet_access::services {

// ExampleSample and IExampleObserver live here (not in a separate I*.hpp)
// because there is no service-level interface — see §12.1.

struct ExampleSample {
    uint32_t sequence;
    int32_t value;
};

class IExampleObserver {
public:
    virtual ~IExampleObserver() = default;
    virtual void on_sample(const ExampleSample& sample) = 0;
};

// No `: public IExampleService` — YAGNI (§6.1). One implementation exists; no test mock needed.
class ExampleService final {
public:
    struct Config {
        uint8_t i2c_address;
        uint32_t sample_interval_ms;
        UBaseType_t task_priority;
        BaseType_t task_core;
        uint32_t task_stack_size;
    };

    ExampleService(i2c::I2CMasterBus& bus, const Config& config, IExampleObserver* observer = nullptr);
    ~ExampleService();

    ExampleService(const ExampleService&) = delete;
    ExampleService& operator=(const ExampleService&) = delete;
    ExampleService(ExampleService&&) = delete;
    ExampleService& operator=(ExampleService&&) = delete;

    [[nodiscard]] esp_err_t start();
    [[nodiscard]] std::optional<ExampleSample> get_latest_sample() const;

private:
    static void task_entry(void* arg);
    void task_loop();
    [[nodiscard]] esp_err_t sample_once(ExampleSample& out_sample);

    i2c::I2CMasterBus& bus_;
    IExampleObserver* observer_;
    const Config config_;

    TaskHandle_t task_handle_{nullptr};

    mutable rtos::StaticMutex sample_mutex_;
    ExampleSample latest_sample_{};
    std::atomic<bool> sample_valid_{false};
    std::atomic<uint32_t> sequence_{0};
};

}  // namespace pet_access::services
```

### 12.3 `components/example_service/ExampleService.cpp`

```cpp
#include "ExampleService.hpp"

#include <esp_log.h>

namespace pet_access::services {

static const char* TAG = "ExampleService";

ExampleService::ExampleService(i2c::I2CMasterBus& bus, const Config& config, IExampleObserver* observer)
    : bus_(bus), observer_(observer), config_(config) {}

ExampleService::~ExampleService() {
    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Terminating ExampleService background task.");
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

esp_err_t ExampleService::start() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "start() called twice; ignoring.");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "example_svc",
        config_.task_stack_size,
        this,
        config_.task_priority,
        &task_handle_,
        config_.task_core
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn ExampleService task (FreeRTOS heap exhausted?).");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Started on core %d at priority %u (interval %lu ms, addr 0x%02X)",
        static_cast<int>(config_.task_core),
        static_cast<unsigned>(config_.task_priority),
        static_cast<unsigned long>(config_.sample_interval_ms),
        config_.i2c_address
    );
    return ESP_OK;
}

std::optional<ExampleSample> ExampleService::get_latest_sample() const {
    if (!sample_valid_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    rtos::LockGuard lock(sample_mutex_);
    return latest_sample_;
}

esp_err_t ExampleService::sample_once(ExampleSample& out_sample) {
    uint8_t raw = 0;
    esp_err_t err = bus_.read(config_.i2c_address, &raw, 1, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        return err;
    }
    out_sample.sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    out_sample.value = static_cast<int32_t>(raw);
    return ESP_OK;
}

void ExampleService::task_entry(void* arg) {
    auto* instance = static_cast<ExampleService*>(arg);
    instance->task_loop();
}

void ExampleService::task_loop() {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(config_.sample_interval_ms);

    while (true) {
        ExampleSample sample{};
        esp_err_t err = sample_once(sample);

        if (err == ESP_OK) {
            {
                rtos::LockGuard lock(sample_mutex_);
                latest_sample_ = sample;
            }
            sample_valid_.store(true, std::memory_order_release);

            if (observer_ != nullptr) {
                observer_->on_sample(sample);
            }
        } else {
            ESP_LOGW(TAG, "HAL read failed: %s", esp_err_to_name(err));
        }

        xTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

}  // namespace pet_access::services
```

### 12.4 `components/example_service/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "ExampleService.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos i2c_bus rtos
    PRIV_REQUIRES log
)
```

`freertos`, `i2c_bus`, and `rtos` are public because their types appear in `ExampleService.hpp`. `log` is private because `ESP_LOGx` is only used in the `.cpp`.

### 12.5 Wiring in `main/main.cpp`

Add a priority constant, declare the config, declare the service member after its dependencies, and inject everything via the MIL.

In `main/include/sys_config.hpp`:

```cpp
constexpr UBaseType_t PRIORITY_EXAMPLE_SERVICE = 3;
```

In `main/main.cpp` `SystemController`:

```cpp
// In the MIL, after i2c_bus_ is constructed:
, example_service_(i2c_bus_, example_config_)

// In the private declarations, in dependency order:
const services::ExampleService::Config example_config_{
    .i2c_address       = 0x42,
    .sample_interval_ms = 5000,
    .task_priority     = sys::PRIORITY_EXAMPLE_SERVICE,
    .task_core         = 1,
    .task_stack_size   = 4096,
};
services::ExampleService example_service_;

// In start(), after other services are initialized:
if (example_service_.start() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start ExampleService.");
}
```

The component is then visible to the orchestrator (or any other consumer) via a `ExampleService&` reference to the concrete type. If a service-level interface is later justified per §6.1, replace the member type with `IExampleService&` at that point — no other call sites change.

---

## 13 · Pre-Commit Checklist (Agent + Human)

### 13.1 The four gates

Before a PR is mergeable, all four must pass:

| Gate | Command | Pass condition |
|---|---|---|
| Build | `idf.py build` | Clean tree, zero warnings, zero errors. |
| Format | `clang-format -i <touched files>` | Diff is empty after running. |
| Flash | `idf.py -p /dev/ttyACM0 flash monitor` | Boots cleanly, no LOGE during init. |
| Observe | Trigger the feature on hardware | The actual FSM transition or sensor read happens. |

The observe gate is **mandatory**. "It compiles" and "tests pass" are not proof that a lid opens for the right pet. Use the JTAG GDB session if you need to step through the FSM.

### 13.2 Self-audit table — does this PR satisfy each tenet?

For each row, the answer must be "yes" or "n/a (with reason)". A "no" blocks the PR.

| # | Question | Reference |
|---|---|---|
| 1 | All new shared state is protected by `rtos::StaticMutex` + `rtos::LockGuard`, or is `std::atomic<T>` with explicit `memory_order`. | §4.1, §4.2, §4.7 |
| 2 | No `xSemaphoreTake`, `xSemaphoreGive`, or `vSemaphoreDelete` outside `components/rtos/`. | §4.3 |
| 3 | No `new`, `malloc`, `make_unique`, or `make_shared` outside the boot path. | §3.3, §3.4 |
| 4 | Buffers ≥ 1 KB that are statically allocated either justify staying in DRAM or carry `EXT_RAM_BSS_ATTR`. | §3.2 |
| 5 | Every HAL method returning `esp_err_t` is `[[nodiscard]]`. | §5.1 |
| 6 | Every service-layer query returns `std::optional<T>` (no out-parameters). | §5.2 |
| 7 | No `try`, `catch`, `throw`, `dynamic_cast`. | §5.4 |
| 8 | If an `I*` interface was added, its presence is justified by §6.1 (multiple runtime impls or a concrete test-mock requirement). Concrete classes are the default — no interface needed without that justification. | §6.1, §6.2 |
| 9 | Construction-time DI: references for mandatory deps, pointers for optional callbacks. No globals. | §6.3, §6.4 |
| 10 | New tasks use the trampoline pattern, pin to Core 1, and inject priority/core via constructor. | §4.4, §4.5 |
| 11 | New task priority added to `sys_config.hpp` (not hard-coded at the call site). | §4.5 |
| 12 | All logging uses `ESP_LOGx` with a file-scope `static const char* TAG`. No `printf`. | §7.1, §7.3 |
| 13 | Component `CMakeLists.txt` distinguishes `REQUIRES` (public) from `PRIV_REQUIRES log` (private). | §9.1, §9.2 |
| 14 | If the change requires a Kconfig flag, `sdkconfig.defaults` was updated via `idf.py save-defconfig`. | §10.2 |
| 15 | `clang-format -i` ran on every touched file. | §11.1 |

---

## Appendix A — Migration notes for existing components

The standard above is enforced for **new** code. The following existing components predate parts of the standard and are flagged for opportunistic refactor when next touched. Do **not** open standalone refactor PRs — fold the migration into a PR that's already touching the file.

| Component | Gap | Suggested fix |
|---|---|---|
| `components/AHT25/` | Not a `final` class. | Mark `AHT25 final`. An abstract interface is **not** needed under §6.1 — one implementation exists and no test mock is currently required. Add `IAHT25` only if a second sensor variant or a test fake materializes. |
| `components/VL53L0X/` | Same as AHT25. | Same fix — mark `final`, defer interface until §6.1 conditions are met. |
| `components/ledc_servo/` | Not a `final` class. | Mark `LedcServo final`. An `IServo` interface is warranted only when a test for `LidController` with a software fake is actually written. |
| `components/i2c_bus/` | Not a `final` class. | Mark `I2CMasterBus final`. No interface needed — the bus is so thin that mocking it rarely adds value. |
| `components/telemetry_service/` | Not yet a `final` class. | Mark `TelemetryService final`. No abstract interface needed — `ApplicationManager` holds a concrete `TelemetryService&`; add `ITelemetryService` only if a second implementation or test fake is introduced. |
| `components/lid_controller/` | Same as telemetry. | Mark `LidController final`. Same rationale — defer interface until §6.1 forces it. |
| All task-spawning components | Use `xTaskCreatePinnedToCore` (heap stack) instead of `xTaskCreateStaticPinnedToCore`. | **Intentional.** Codified in §3.4 / §4.8 — boot-time heap is allowed. No migration required. |
| `components/bluetooth/nimble_scanner/` | Uses `xSemaphoreCreateBinary` (dynamic) instead of a static variant. | Justified by the NimBLE callback ordering — see the comment at `NimbleScanner.cpp:25-31`. No migration required. |
| `sdkconfig.defaults` PSRAM lines | `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` is unset. | Correct. Leave unset until a buffer needs `EXT_RAM_BSS_ATTR`; only then enable it via menuconfig. |
| `partitions.csv` | Uses `subtype=spiffs` for the storage partition; the architecture intent is LittleFS. | Convert to LittleFS in a dedicated PR with a one-time format step and OTA migration plan. Out of scope for this document. |

---

## Appendix B — Recommended menuconfig flags for upcoming WiFi / Cloud work

**Reserved.** Will be populated in a separate update once the WiFi/Cloud module designs are finalized. Anticipated coverage:

- WiFi station mode (`CONFIG_ESP_WIFI_*`). Includes custom wrapper flags: `CONFIG_PET_WIFI_SSID` and `CONFIG_PET_WIFI_PASSWORD` (kept in `.local`).
- WPA2/WPA3 enterprise selection.
- mbedTLS configuration (`CONFIG_MBEDTLS_*`) — enabling only the cipher suites our cloud endpoint requires, to keep the binary small.
- HTTP client + HTTPS (`CONFIG_ESP_HTTP_CLIENT_*`).
- MQTT (`CONFIG_MQTT_PROTOCOL_*`).
- OTA partition selection (already partially covered by `partitions.csv`).
- NTP / SNTP (`CONFIG_LWIP_SNTP_*`).

Until this appendix is filled in, refer to the vendor docs and follow §10.2 (the menuconfig recipe) for any additions. Any flag added must follow the same workflow: menuconfig → `save-defconfig` → commit only the `sdkconfig.defaults` diff.

---

*End of document. See `05_Contributing.md` for the quick-reference summary, and `02_Architecture.md` for the broader system overview.*
