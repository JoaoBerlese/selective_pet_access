# 01 · Developer Guide

**Role:** Firmware Contributor  **Target:** ESP32-S3-N16R8  **System:** Dockerized ESP-IDF v5.3

This is the day-to-day user manual. It assumes the Dev Container is already running on your machine. If it is not, follow **[04 · Environment Setup](04_Environment_setup.md)** first.

---

## 1. Philosophy — Hermetic Build

- **No local tools.** You do *not* install Python, CMake, or ESP-IDF on your host.
- **Containerized.** Everything runs inside the ESP-IDF Dev Container.
- **If it compiles in the container, it compiles everywhere.**

---

## 2. Connect the Hardware

| Item | Value |
|---|---|
| Device | ESP32-S3 (N16R8) |
| Port | **USB** (Native JTAG), *not* the UART/COM port |
| Timing | Plug in **before** opening VS Code — Docker maps the device only at container start |

Inside the container, the device always appears as `/dev/ttyACM0` regardless of host OS.

---

## 3. Build & Flash

From the VS Code terminal **inside the container**:

```bash
idf.py reconfigure
idf.py build                              # Compile
idf.py -p /dev/ttyACM0 flash monitor      # Flash + live logs (Ctrl+] to exit)
idf.py fullclean && idf.py build          # Full rebuild
```

Success looks like logs streaming from `ESP_LOGI` calls in the monitor.

---

## 4. Debugging — JTAG / OpenOCD

OpenOCD is pre-configured for the ESP32-S3 built-in USB-JTAG. No external probe required.

1. Click the gutter next to a line to set a breakpoint.
2. Press **F5** (or *Run → Start Debugging*).
3. The chip halts — inspect variables, call stack, registers.

For post-mortem analysis, fatal panics write to the `coredump` partition (see **[03 · Hardware](03_Hardware.md)**).

---

## 5. Changing `sdkconfig`

The hardware config is locked in `sdkconfig.defaults` (the source of truth). `sdkconfig` itself is generated and git-ignored.

- **Local tweak** (log level, dev-only flag): `idf.py menuconfig` — change stays local.
- **Permanent change** (enable WiFi stack, partition tweak):
  ```bash
  idf.py menuconfig
  idf.py save-defconfig    # Persists to sdkconfig.defaults
  ```

---

## 6. Code Style

Google style, 4-space indent, 120 columns, enforced by `.clang-format`. VS Code formats on save. Manual: `clang-format -i <file>`.

Detailed conventions (namespaces, RAII contract, memory rules) live in **[05 · Contributing](05_Contributing.md)**.

---

## Where to go next

| Need | Doc |
|---|---|
| Understand the FSM, task priorities, and component graph | [02 · Architecture](02_Architecture.md) |
| Wiring, BOM, sensor placement, partition layout | [03 · Hardware](03_Hardware.md) |
| Full env setup (including from scratch) or troubleshooting | [04 · Environment Setup](04_Environment_setup.md) |
| Add a new HAL component or submit a change | [05 · Contributing](05_Contributing.md) |
