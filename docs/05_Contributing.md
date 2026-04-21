# 05 · Contributing

**Role:** Adding a HAL driver, touching core logic, or submitting a PR.

---

## 1. Non-negotiable rules

1. **No `new` / `malloc` in application code.** FreeRTOS resources are static (see §3). Data buffers > 1 KB go in PSRAM via `EXT_RAM_BSS_ATTR` or `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` (never in DRAM, never for RTOS primitives).
2. **No exceptions, no RTTI.** Compiled with `-fno-exceptions -fno-rtti`. Errors return `esp_err_t` (HAL) or `std::optional<T>` (service API). No `throw`, no `dynamic_cast`.
3. **No global state.** All dependencies are injected through constructors. The wiring happens exactly once, in `main/main.cpp`.
4. **No raw `xSemaphoreTake` / `xSemaphoreGive`.** Use `rtos::LockGuard` + `rtos::StaticMutex` (see §3). Any raw usage will be rejected in review.
5. **Use `ESP_LOGx`, never `printf`.** With the correct component tag. `printf` bypasses log level filtering and floods the UART in tight loops.

---

## 2. Code style

- **Google style, 4-space indent, 120 columns.** Enforced by `.clang-format`; VS Code formats on save.
- **Header guards:** `#pragma once`.
- **Include order:** standard lib → ESP-IDF / framework → project. Clang-format sorts automatically; do not fight it.
- **Namespaces:** see **[02 · Architecture §6](02_Architecture.md#6-namespaces)**. New drivers pick the closest matching one.
- **`[[nodiscard]]`** on every function returning `esp_err_t` or `std::optional<T>`.
- **Rule of Five:** resource-owning classes (anything holding a `QueueHandle_t`, `TaskHandle_t`, `SemaphoreHandle_t`, or hardware channel) delete copy and move constructors.

---

## 3. The RAII / lock contract

`pet_access::rtos::StaticMutex` allocates the mutex storage in `.bss` via `xSemaphoreCreateMutexStatic`. `pet_access::rtos::LockGuard` takes the mutex in its constructor and releases it on scope exit — even on early `return` through `ESP_ERROR_CHECK` failures.

Use it like this:

```cpp
pet_access::rtos::LockGuard guard(bus_mutex_);
ESP_RETURN_ON_ERROR(i2c_master_transmit(...), TAG, "write failed");
// guard releases automatically when the function returns
```

This pattern is the *only* reason deadlocks cannot happen in this codebase. Break it — even temporarily — and the guarantee is gone.

---

## 4. Adding a new HAL component

All HAL drivers live under `components/`. Steps:

### 4.1 Scaffold the directory

From the Dev Container terminal:

```bash
mkdir -p components/my_driver/include
touch components/my_driver/CMakeLists.txt
touch components/my_driver/include/my_driver.hpp
touch components/my_driver/my_driver.cpp
```

### 4.2 Register with the build

`components/my_driver/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "my_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_log          # declare the IDF components you use
)
```

The root `CMakeLists.txt` already auto-discovers `components/`, so no extra wiring is needed.

### 4.3 Expose an abstract interface

Every driver exposes a pure-virtual interface in its header. Business logic depends on the interface, never the concrete type. This keeps `ApplicationManager` testable on host without real hardware.

```cpp
// components/my_driver/include/my_driver.hpp
#pragma once
#include <esp_err.h>
#include <optional>

namespace pet_access::sensors {

class IMyDriver {
public:
    virtual ~IMyDriver() = default;
    [[nodiscard]] virtual esp_err_t initialize() = 0;
    [[nodiscard]] virtual std::optional<float> read() = 0;
};

}  // namespace pet_access::sensors
```

### 4.4 Wire it in `main/main.cpp`

Inject dependencies through the constructor. Respect member declaration order — a component must be declared *after* everything it depends on.

### 4.5 Document thread-safety in the header

Public headers state, for each method, whether it is safe to call from:

- Any task (thread-safe, internally synchronized)
- A single owning task only
- ISR context (rare — state this explicitly)

---

## 5. Commit & PR conventions

- Commits are atomic — one logical change per commit. A commit that changes wiring *and* a driver's internals gets split.
- Commit message subject in imperative present tense, ≤ 72 chars. Body explains **why**, not what.
- PRs target `main`. Include:
  - What changed and why.
  - Any new GPIO or `sdkconfig` impact.
  - How you verified on hardware (log snippet, or "flashed and observed lid sequence").
- Do **not** bypass pre-commit hooks (`--no-verify`) or the clang-format check.

---

## 6. Before you open the PR

- `idf.py build` succeeds from a clean tree.
- `clang-format -i` on every file you touched (VS Code's format-on-save handles this).
- Flashed to hardware and observed the relevant FSM transition — unit of proof is "the lid opens when Frodo approaches", not "it compiles".
- New public functions are `[[nodiscard]]` where applicable and document thread-safety.
- No new files outside `components/`, `main/`, or `docs/`.
