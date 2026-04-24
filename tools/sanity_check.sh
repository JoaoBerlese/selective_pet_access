#!/usr/bin/env bash
# ==============================================================================
#  Selective Pet Access — Host Environment Sanity Check
#  Run this on your HOST machine before opening VS Code.
#
#  Usage:
#    1. Open your host terminal.
#    2. Navigate to the project root:
#       cd path/to/selective_pet_access
#    3. Execute the script:
#       ./tools/sanity_check.sh
#
#  What it checks:
#    1. Docker is installed and the daemon is running.
#    2. Current user is in the 'docker' group (rootless container start).
#    3. Current user is in the 'dialout' group (Linux serial port access).
#    4. ESP32-S3 device is detected on USB (warning only — not a hard failure).
#
#  Exit codes:
#    0  All required checks passed (USB warning does not affect exit code).
#    1  One or more required checks failed.
# ==============================================================================
set -euo pipefail

# --- Colours (disabled automatically on non-TTY, e.g. CI logs) ---
if [ -t 1 ]; then
    RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; YELLOW=''; GREEN=''; BOLD=''; RESET=''
fi

PASS="${GREEN}[PASS]${RESET}"
FAIL="${RED}[FAIL]${RESET}"
WARN="${YELLOW}[WARN]${RESET}"

errors=0

print_header() {
    echo ""
    echo -e "${BOLD}============================================================${RESET}"
    echo -e "${BOLD}  Selective Pet Access — Host Sanity Check${RESET}"
    echo -e "${BOLD}  Target: ESP32-S3-N16R8 | Build: Docker / ESP-IDF v5.3${RESET}"
    echo -e "${BOLD}============================================================${RESET}"
    echo ""
}

# ------------------------------------------------------------------------------
# Check 1: Docker is installed
# ------------------------------------------------------------------------------
check_docker_installed() {
    echo -e "${BOLD}[1/4] Docker installation${RESET}"
    if command -v docker &>/dev/null; then
        local version
        version=$(docker --version 2>/dev/null || echo "unknown")
        echo -e "  ${PASS} Docker found: ${version}"
    else
        echo -e "  ${FAIL} Docker not found in PATH."
        echo "       Install: https://docs.docker.com/engine/install/"
        echo "       Linux: curl -fsSL https://get.docker.com | sh"
        (( errors++ )) || true
    fi
}

# ------------------------------------------------------------------------------
# Check 2: Docker daemon is running and reachable
# ------------------------------------------------------------------------------
check_docker_running() {
    echo -e "${BOLD}[2/4] Docker daemon${RESET}"
    if docker info &>/dev/null; then
        echo -e "  ${PASS} Docker daemon is running and reachable without sudo."
    else
        # Distinguish between "daemon not running" and "permission denied"
        local docker_err
        docker_err=$(docker info 2>&1 || true)
        if echo "${docker_err}" | grep -qi "permission denied"; then
            echo -e "  ${FAIL} Docker daemon is running but access is denied."
            echo "       Your user is not in the 'docker' group (or group change"
            echo "       has not been applied). Fix:"
            echo "         sudo usermod -aG docker \$USER"
            echo "         newgrp docker   # or log out and back in"
        else
            echo -e "  ${FAIL} Docker daemon is not running."
            echo "       Linux:  sudo systemctl start docker"
            echo "       macOS/Windows: Start Docker Desktop."
        fi
        (( errors++ )) || true
    fi
}

# ------------------------------------------------------------------------------
# Check 3: User group membership (Linux only)
# ------------------------------------------------------------------------------
check_user_groups() {
    echo -e "${BOLD}[3/4] User group membership${RESET}"

    # Skip on macOS / Windows (WSL2 handles this differently)
    local os_type
    os_type=$(uname -s)
    if [[ "${os_type}" != "Linux" ]]; then
        echo "  [SKIP] Group checks are Linux-only (detected: ${os_type})."
        return
    fi

    local current_user="${USER:-$(id -un)}"
    local user_groups
    user_groups=$(id -Gn "${current_user}" 2>/dev/null || true)

    # docker group
    if echo "${user_groups}" | grep -qw "docker"; then
        echo -e "  ${PASS} User '${current_user}' is in the 'docker' group."
    else
        echo -e "  ${FAIL} User '${current_user}' is NOT in the 'docker' group."
        echo "         Fix: sudo usermod -aG docker \$USER  (then log out/in)"
        (( errors++ )) || true
    fi

    # dialout group (required for /dev/ttyACM0 serial port access)
    if echo "${user_groups}" | grep -qw "dialout"; then
        echo -e "  ${PASS} User '${current_user}' is in the 'dialout' group."
    else
        echo -e "  ${FAIL} User '${current_user}' is NOT in the 'dialout' group."
        echo "         /dev/ttyACM0 is owned by root:dialout. Without this"
        echo "         group, the host OS (and Docker device passthrough) may"
        echo "         deny serial port access even with --privileged."
        echo "         Fix: sudo usermod -aG dialout \$USER  (then log out/in)"
        (( errors++ )) || true
    fi
}

# ------------------------------------------------------------------------------
# Check 4: ESP32-S3 USB device detection (warning only)
# ------------------------------------------------------------------------------
check_esp32_usb() {
    echo -e "${BOLD}[4/4] ESP32-S3 USB device detection${RESET}"

    local os_type
    os_type=$(uname -s)

    if [[ "${os_type}" == "Linux" ]]; then
        # Espressif USB VID: 303a (used by ESP32-S3 built-in USB-JTAG/Serial)
        local found_device=""
        if command -v lsusb &>/dev/null; then
            found_device=$(lsusb | grep -i "303a" || true)
        fi

        if [[ -n "${found_device}" ]]; then
            echo -e "  ${PASS} Espressif device detected via lsusb:"
            echo "         ${found_device}"
        elif ls /dev/ttyACM* &>/dev/null 2>&1; then
            echo -e "  ${WARN} /dev/ttyACM* found but Espressif VID (303a) not confirmed."
            echo "         The device may be another USB-serial adapter. Verify manually."
        else
            echo -e "  ${WARN} No ESP32-S3 detected on USB."
            echo "         This is not a hard failure — you can still open VS Code and"
            echo "         build firmware. Plug in the ESP32 before flashing."
            echo "         If the device IS plugged in: check the USB cable (data, not"
            echo "         charge-only) and try: sudo dmesg | tail -20"
        fi

    elif [[ "${os_type}" == "Darwin" ]]; then
        if command -v system_profiler &>/dev/null; then
            local found_device=""
            found_device=$(system_profiler SPUSBDataType 2>/dev/null | grep -i "303a" || true)
            if [[ -n "${found_device}" ]]; then
                echo -e "  ${PASS} Espressif device detected via system_profiler."
            else
                echo -e "  ${WARN} No ESP32-S3 detected on USB (macOS)."
                echo "         Plug in the ESP32 before flashing."
            fi
        else
            echo "  [SKIP] system_profiler not available. Cannot check USB device."
        fi

    else
        echo "  [SKIP] USB detection not supported on this OS (${os_type})."
        echo "         On Windows: check Device Manager for 'Silicon Labs CP210x' or"
        echo "         'USB Serial Device (COMx)'."
    fi
}

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
print_summary() {
    echo ""
    echo -e "${BOLD}============================================================${RESET}"
    if [[ "${errors}" -eq 0 ]]; then
        echo -e "${GREEN}${BOLD}  All required checks passed. Open VS Code and run:${RESET}"
        echo -e "${GREEN}${BOLD}  Dev Containers: Reopen in Container${RESET}"
    else
        echo -e "${RED}${BOLD}  ${errors} check(s) failed. Fix the issues above before${RESET}"
        echo -e "${RED}${BOLD}  opening the Dev Container.${RESET}"
    fi
    echo -e "${BOLD}============================================================${RESET}"
    echo ""
}

# --- Main ---
print_header
check_docker_installed
echo ""
check_docker_running
echo ""
check_user_groups
echo ""
check_esp32_usb
print_summary

exit "${errors}"
