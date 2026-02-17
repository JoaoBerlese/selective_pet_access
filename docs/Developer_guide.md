# Developer Guide: Selective Pet Access
**Role:** Firmware Contributor | **Target:** ESP32-S3-N16R8 | **System:** Dockerized ESP-IDF v5.3

This document is the "User Manual" for developing on this repository. It assumes the infrastructure has already been scaffolded.

---

## 1. The Philosophy
We use a **Hermetic Build System**.
* **No Local Tools:** You do *not* need to install Python, CMake, or the ESP-IDF on your machine.
* **Containerized:** Everything runs inside a Docker container.
* **Source of Truth:** If it compiles in the container, it compiles everywhere.

---

## 2. Host Prerequisites
You only need 3 things on your computer:

1.  **VS Code:** [Download here](https://code.visualstudio.com/).
2.  **Docker:**
    * *Windows/Mac:* Install [Docker Desktop](https://www.docker.com/products/docker-desktop/).
    * *Linux:* Install Docker Engine + [Post-install steps](https://docs.docker.com/engine/install/linux-postinstall/) (Non-root user).
3.  **Extension:** Install **Dev Containers** (`ms-vscode-remote.remote-containers`) in VS Code.

---

## 3. Quick Start (The "Happy Path")

### Step A: Connect Hardware
* **Device:** ESP32-S3 (N16R8).
* **Port:** Connect via the **USB** port (Native JTAG), *not* the UART/COM port.
* **Timing:** Connect the device **BEFORE** opening VS Code. (Docker maps the USB device only at startup).

### Step B: Launch the Environment
1.  **Clone** this repository.
2.  Open the folder in **VS Code**.
3.  You will see a pop-up: *"Folder contains a Dev Container configuration. Reopen to develop in a container."*
    * Click **Reopen in Container**.
    * *(Alternative: Press `F1` -> Type "Dev Containers: Reopen in Container")*.
4.  **Wait:** The first run will download the ESP-IDF image (~2GB). Grab a coffee.

### Step C: Configure the Kit (Crucial)
VS Code will ask to "Select a Kit" for CMake.
* **Action:** Select **`[Unspecified]`**.
* **Why:** We must let the ESP-IDF toolchain (`idf.py`) handle the compiler. If you select "GCC x86", the build will fail.

### Step D: Build & Flash
Open the VS Code Terminal (`Ctrl + J`) and runs commands **inside the container**:

```bash
# 1. Build the Firmware
idf.py build

# 2. Flash and Monitor (using Native USB)
idf.py -p /dev/ttyACM0 flash monitor
```

**Success:** You should see logs appearing in the terminal.

---

## 4. Development Workflow

### Coding Standards

* **Style:** Google Style + 4 Spaces + 120 Columns.
* **Enforcement:** The project has a `.clang-format` file. VS Code is configured to **Format on Save** automatically.
* **C++20:** We use Modern C++. No `new`/`malloc` in loops. Use `constexpr`, `std::expected`, and RAII.

### Debugging (Hardware)

We have configured **OpenOCD** for the built-in USB JTAG.

1. Set a breakpoint (click the red dot next to a line number).
2. Press **F5** (or Run -> Start Debugging).
3. The chip will halt, and you can inspect variables, call stacks, and registers.

### Configuration Changes (`sdkconfig`)

The hardware configuration (Flash size, Partition Table) is locked in `sdkconfig.defaults`.

* **Small Tweaks:** If you need to change a local setting (e.g., Log Level), run `idf.py menuconfig`.
* **Permanent Changes:** If you are adding a major feature (e.g., enabling WiFi stack), run:
```bash
idf.py menuconfig   # Make your changes
idf.py save-defconfig # Update the source of truth

```

---

## 5. Troubleshooting

### "CMake Error: Bad Executable" / Build Fails Immediately

* **Cause:** You likely selected the wrong "Kit" (e.g., `/usr/bin/gcc`).
* **Fix:** Press `F1` -> "CMake: Select a Kit" -> Choose **`[Unspecified]`**.

### "No such file or directory: /dev/ttyACM0"

* **Cause:** The ESP32 was not plugged in when Docker started.
* **Fix:**
1. Keep the ESP32 plugged in.
2. Press `F1` -> **Dev Containers: Rebuild Container**.

### "Permission Denied" on Linux

* **Cause:** Your user is not in the `docker` group.
* **Fix:** Run `sudo usermod -aG docker $USER` and restart your computer.

### "Flash Failed" (Timeout)

* **Cause:** USB Latency inside the container.
* **Fix:** Put the device in **Bootloader Mode**:
1. Hold `BOOT` button.
2. Press `RST` (Reset) button.
3. Release `BOOT`.
4. Run the flash command again.

---
