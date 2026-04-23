# Infrastructure Setup: From Scratch
**Role:** Firmware Architect | **Target:** ESP32-S3-N16R8 | **System:** Dockerized ESP-IDF v5.3

This document defines the procedure to scaffold the **Selective Pet Access** firmware environment from an empty directory. It establishes the "Hermetic Build" system, ensuring that the host OS is irrelevant to the build process.

---

## 1. Host Prerequisites

Before scaffolding, the host machine must be configured to run containers without root privileges.

### **A. Linux (Debian/Ubuntu/Arch)**

*Critical:* You must install the Native Engine and configure user groups. **Do not use Docker Desktop for Linux** (it adds unnecessary virtualization overhead).

Run these commands strictly in order:

```bash
# 1. Update and install prerequisites
sudo apt update && sudo apt install -y ca-certificates curl gnupg

# 2. Install Docker Engine (Official Convenience Script)
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# 3. CRITICAL: Permission Setup
# Adds your current user to the 'docker' group. 
# This allows VS Code to talk to the daemon without 'sudo'.
sudo usermod -aG docker $USER

# 4. Activate Changes
# Apply the group change immediately to the current terminal
newgrp docker

# 5. Verification
# Must run WITHOUT 'sudo'. If this fails, restart your computer.
docker run hello-world

```

> **Architect's Warning:** If `docker run hello-world` fails with "permission denied", **restart your computer**. VS Code will not pick up the new group membership until the session restarts.

### **B. Windows / macOS**
* **Install:** [Docker Desktop](https://www.docker.com/products/docker-desktop/).
* **Configuration:**
* Start Docker Desktop.
* **Windows Users:** Ensure "Use WSL 2 based engine" is checked in Settings > General.

### **C. VS Code Extensions**
Install the **Dev Containers** extension (`ms-vscode-remote.remote-containers`).

### **D. Hardware Connection**
ESP32-S3 connected via **USB** (JTAG/Serial).
> *Note:* Regardless of your host OS (Windows `COMx` or Linux `ttyACM0`), the device will be mapped to `/dev/ttyACM0` **inside the container**.

---

## 2. Directory Scaffolding
The project follows a standard ESP-IDF component structure:

**Option A: Automated Generation (Recommended)**
> We have provided a shell script to generate all configuration files automatically.
> 1. Create the base folders:
>    ```bash
>    mkdir -p selective_pet_access/{.devcontainer,.vscode,components,main,test}
>    cd selective_pet_access
>    ```
> 2. **[Click here to open the Quickstart Scaffolding Script](./quickstart_scaffolding.md)**.
> 3. Copy the script commands, paste them into your terminal, and press Enter.
> 4. Skip to **Section 7 (Initialization)**.

**Option B: Manual Creation**
If you prefer to understand every file by creating it manually, continue below.

### Standard Structure Reference
```text
selective_pet_access/
├── .devcontainer/          # Docker Environment Definition
│   └── devcontainer.json
├── .vscode/                # IDE Settings (Debug/Linting)
│   ├── c_cpp_properties.json
│   ├── settings.json
│   └── launch.json
├── components/             # Custom Hardware Abstraction Layers (HAL)
├── main/                   # Application Entry Point
│   ├── CMakeLists.txt
│   └── main.cpp
├── test/                   # Dual-Target Tests (Host & Embedded)
├── .gitignore              # Git Exclusion Rules
├── CMakeLists.txt          # Project Root CMake
└── README.md
```

Create the standard ESP-IDF component structure. Run these commands on the Host:

Create a `.gitignore` to prevent artifact pollution:

```gitignore
# ==============================================================================
# ESP-IDF & Build Artifacts
# ==============================================================================
# The build directory is massive and machine-specific. Never commit it.
build/
**/build/

# SDK Configuration: We use 'sdkconfig.defaults' as the source of truth.
# 'sdkconfig' is the local instance and changes frequently.
sdkconfig
sdkconfig.old

# Dependency Management (IDF Component Manager)
managed_components/
dependencies.lock

# Binary & Map files (just in case they end up outside build/)
*.bin
*.elf
*.map
*.hex
flasher_args.json

# ==============================================================================
# Tooling & IDEs
# ==============================================================================
# VS Code: We commit settings/tasks/extensions, but NOT local user state.
.vscode/
!.vscode/settings.json
!.vscode/tasks.json
!.vscode/launch.json
!.vscode/extensions.json
!.vscode/c_cpp_properties.json

# Clangd / LSP caches (if you use clangd extension)
.clangd/
.cache/
compile_commands.json

# CMake (if generated outside build/)
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
install_manifest.txt
CTestTestfile.cmake

# ==============================================================================
# Python & Testing
# ==============================================================================
# Python bytecode and caches (generated by pytest/idf.py)
__pycache__/
*.py[cod]
*.pyd
.pytest_cache/
.coverage
htmlcov/

# Virtual Environments (if you create one locally)
venv/
.venv/
env/

# ==============================================================================
# OS & Filesystem Noise
# ==============================================================================
# macOS
.DS_Store
.AppleDouble
.LSOverride

# Windows
Thumbs.db
ehthumbs.db
Desktop.ini

# Linux / General
*~
*.swp
*.swo
*.bak
*.tmp
*.log

```

---

## 3. The Hermetic Container (`.devcontainer`)

This defines the "Source of Truth" for the build environment.

**File:** `.devcontainer/devcontainer.json`

```json
{
    "name": "ESP-IDF v5.3 Dev Env",
    "image": "espressif/idf:release-v5.3",
    "containerEnv": {
        "LC_ALL": "C.UTF-8",
        "LANG": "C.UTF-8"
    },
    "customizations": {
        "vscode": {
            "settings": {
                "terminal.integrated.defaultProfile.linux": "bash",
                "idf.espIdfPath": "/opt/esp/idf",
                "idf.toolsPath": "/opt/esp/tools",
                "idf.pythonBinPath": "/opt/esp/python_env/idf5.3_py3.10_env/bin/python"
            },
            "extensions": [
                "ms-vscode.cpptools",
                "ms-vscode.cmake-tools",
                "espressif.esp-idf-extension",
                "matepek.vscode-catch2-test-adapter" 
            ]
        }
    },
    "runArgs": [
        "--privileged",         // Required for USB JTAG access
        "--device=/dev/ttyACM0" // Map USB device
    ],
    "workspaceMount": "source=${localWorkspaceFolder},target=/workspace,type=bind",
    "workspaceFolder": "/workspace",
    "postCreateCommand": "echo 'source /opt/esp/idf/export.sh' >> ~/.bashrc"
}

/*
*  Why this works: It pulls the image directly from Espressif.
*  No manual installation of Python or GCC is required.
*  It guarantees that anyone opening this repo gets the exact same toolchain.
*/

```

> **Architect's Note:** The `postCreateCommand` automatically appends the IDF export script to `.bashrc`. This ensures that every new terminal session inside Docker has access to `idf.py` without manual sourcing.

---

## 4. Build System Configuration (Modern C++)

We must enforce C++20 at the root level.

**File:** `CMakeLists.txt` (Project Root)

```cmake
cmake_minimum_required(VERSION 3.16)

# Enforce Modern C++ Standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Forces standard C++ (no gnu++20)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(selective_pet_access)

```

**File:** `main/CMakeLists.txt`

```cmake
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")

```

**File:** `main/main.cpp` (Smoke Test)

```cpp
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "Main";

extern "C" void app_main(void)
{
    // C++20 Lambda verification
    auto print_status = []() {
        ESP_LOGI(TAG, "System Running: FreeRTOS Scheduler Active (C++20)");
    };

    while (true) {
        print_status();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

```

---

## 5. Hardware & Memory Architecture

We strictly define the hardware requirements using `sdkconfig.defaults`. This avoids the fragility of manual `menuconfig` steps.

**File:** `sdkconfig.defaults`

```ini
# This file was generated using idf.py save-defconfig. It can be edited manually.
# Espressif IoT Development Framework (ESP-IDF) 5.3.4 Project Minimal Configuration
#
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
```

**Maintenance:** For all future changes (e.g., enabling WiFi, changing log levels), do **not** edit this file manually. Run `idf.py menuconfig`, make your changes, and then run `idf.py save-defconfig`.

**File:** `partitions.csv`

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,        0x6000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,
# --- Dual Bank Logic (3MB each) ---
ota_0,    app,  ota_0,   ,        3M,
ota_1,    app,  ota_1,   ,        3M,
# --- Data Storage ---
storage,  data, spiffs,  ,        9M,
coredump, data, coredump,,        128K,

```

#### **Partition Strategy Explained**

**System & Config (`nvs`, `otadata`, `phy_init`)**:
* **NVS (24KB):** Non-Volatile Storage for small key-value pairs (WiFi credentials, Device ID, operational flags).
* **OTA Data (8KB):** Critical bootloader state machine. Tells the ESP32 which app slot (`ota_0` or `ota_1`) is currently active/bootable.
* **PHY Init (4KB):** RF Calibration data. Stores hardware tuning parameters for WiFi/BLE radio performance.

**Application Slots (`ota_0`, `ota_1`)**:
* **Dual Bank Logic:** We allocate two identical **3MB** slots.
* **Why 3MB?** Standard ESP32 apps are often restricted to 1MB. We expand this to support the heavier binary size typical of **Modern C++** (templates, vtables, exceptions) and feature-rich RTOS logic.
* **Mechanism:** `ota_0` is the Factory/Active App. `ota_1` is the "Downloaded Update" slot. If an update fails, the bootloader safely rolls back to `ota_0`.

**Local Storage (`storage`)**:
* **9MB LittleFS:** Acts as the device's "Hard Drive."
* **Purpose:** Stores the offline database (`cats.json`), system event logs, and potentially future assets.

**Diagnostics (`coredump`)**:
* **128KB Core Dump:** A dedicated safety net.
* **Function:** If the firmware crashes (panics), the entire stack trace and register state are written here *before* the reboot. This allows post-mortem debugging of field failures without a JTAG connection.

---

## 6. IDE Tooling (Quality of Life)

**File:** `.clang-format` (Google Style + some changes for embedded logic):

```yaml
# ==============================================================================
#  Selective Pet Access - Firmware Code Style
#  Based on Google Style but optimized for Modern C++ and 4-space indentation.
# ==============================================================================
Language: Cpp
BasedOnStyle: Google

# --- Indentation & Spacing ---
AccessModifierOffset: -4      # public/private flush with class
IndentWidth: 4                # 4 spaces is standard for C/C++ readability
TabWidth: 4
UseTab: Never                 # Tabs are evil. Always use spaces.
ColumnLimit: 120              # 80 is too narrow for verbose HAL/Templates

# --- Pointers & References ---
# Modern C++ prefers the type to carry the pointer (int* p), not the variable.
PointerAlignment: Left

# --- Breaking & Wrapping ---
# Keep empty functions on one line (good for mocked interfaces)
AllowShortFunctionsOnASingleLine: Empty
# Keep simple 'if (x) return;' on one line? No. Force explicit logic.
AllowShortIfStatementsOnASingleLine: Never
# Break before inheritance lists (cleaner constructors)
BreakConstructorInitializers: BeforeComma

# --- Sorting ---
# Automatically sort #include headers (System -> Library -> Local)
SortIncludes: true

# --- C++20 Specifics ---
# Align concepts and requires clauses beautifully
IndentRequiresClause: true
BreakBeforeConceptDeclarations: true

```

**File:** `.vscode/settings.json`

```json
{
    // ========================================================================
    //  Selective Pet Access - Workspace Settings
    //  Target: ESP32-S3 (N16R8) | Tools: ESP-IDF v5.3 (Docker)
    // ========================================================================

    // --- Build & Format ---
    // Explicit path to avoid "No such file" errors. 
    // Verify inside container with: find /opt/esp/tools -name cmake -type f | grep bin/cmake
    "cmake.cmakePath": "/opt/esp/tools/cmake/3.30.2/bin/cmake",
    "editor.formatOnSave": true,
    "editor.defaultFormatter": "ms-vscode.cpptools",
    
    // Enforce strict Clang-Format usage
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.formatting": "clangFormat",

    // --- Critical: ESP32-S3 Flashing & Debugging ---
    // Explicitly define the target so the IDE doesn't guess
    "idf.adapterTargetName": "esp32s3",
    
    // OpenOCD Config for Native USB JTAG (Built-in)
    // This enables the "F5" Debug button to work without external dongles
    "idf.openOcdConfigs": [
        "board/esp32s3-builtin.cfg"
    ],
    
    // Force the port to the standard Docker mapping
    "idf.port": "/dev/ttyACM0",
    "idf.flashType": "UART" 
}
```

**File:** `.vscode/launch.json`

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "ESP-IDF: OpenOCD Debug",
            "type": "espidf",
            "request": "launch",
            "debugPort": 3333,
            "mode": "auto",
            "verifyAppBinBeforeDebug": true,
            // Critical for Docker: Doubles timeouts to handle USB passthrough latency
            "tmoScaleFactor": 2, 
            "initCommands": [
                "mon reset halt",
                "flushregs"
            ]
        }
    ]
}
```

**File:** `.vscode/c_cpp_properties.json`
*(Critical: This permanently links VS Code's IntelliSense to the CMake build tree, preventing red squiggles and desyncs when the DevContainer restarts).*

```json
{
    "configurations": [
        {
            "name": "ESP-IDF",
            "compilerPath": "",
            "compileCommands": "${workspaceFolder}/build/compile_commands.json",
            "cStandard": "c11",
            "cppStandard": "c++20"
        }
    ],
    "version": 4
}
```

---

## 7. Initialization & Verification

1. **Launch:** Open the folder in VS Code. Press `F1` -> **Dev Containers: Reopen in Container**.
2. **Wait:** Allow Docker to pull the image and install extensions (approx. 5 mins on first run).
3. **Kit Selection (Critical):**
    * VS Code will ask to select a "Kit" (Compiler).
    * **Select `[Unspecified]`**.
    * *Reason:* We must allow `idf.py` to manage the cross-compiler toolchain. Do not let CMake Tools override this with the host GCC.
4. **USB Check & IntelliSense Sync:**
    Before compiling, ensure the container and the IDE can see the hardware.
    * Open Terminal (inside container) and run: `ls /dev/ttyACM*`.
    * **Success:** Returns `/dev/ttyACM0`. Proceed to Build.
    * **Failure:** Returns `No such file or directory` or IntelliSense fails to clear red squiggles.
        * *Cause:* The device was not plugged in when the container started, or the IDE lost the CMake mapping.
        * *The Fix:* **Do not rebuild the container!** Plug in the device, ensure Linux sees it (`ls /dev/ttyACM*`), then press `F1` -> **Developer: Reload Window**. This takes 2 seconds and restores the USB mapping and IntelliSense caches.
5. **Build & Flash:**
```bash
# Apply defaults and build
idf.py build

# Flash and Monitor
idf.py -p /dev/ttyACM0 flash monitor

```

**Success Criteria:**
The serial monitor should display: `System Running: FreeRTOS Scheduler Active (C++20)`.

