# Claude Code Integration: DevContainer Setup
**Role:** Firmware Architect | **Target:** ESP32-S3-N16R8 | **System:** Dockerized ESP-IDF v5.3 + Claude Code

This document defines the procedure to integrate the Claude Code agent directly into our hermetic ESP-IDF Docker environment. Running the agent inside the container ensures it has native access to the cross-compiler (`idf.py`), the CMake build tree, and all project dependencies without polluting the host OS.

---

## 1. Injecting Node.js into the DevContainer

The official Espressif Docker image (`espressif/idf`) is optimized for C/C++ and Python, but Claude Code is distributed via `npm` and requires Node.js. To maintain the "Hermetic Build" principle, we inject Node.js using VS Code DevContainer features rather than relying on manual installation scripts.

**File:** `.devcontainer/devcontainer.json`

Modify your configuration to include the `features` block requesting the Node.js LTS version. Your updated file should look like this:

```json
{
    "name": "ESP-IDF v5.3 Dev Env",
    "image": "espressif/idf:release-v5.3",
    
    // -> ADD THIS FEATURES BLOCK <-
    "features": {
        "ghcr.io/devcontainers/features/node:1": {
            "version": "lts"
        }
    },
    
    "containerEnv": {
        "LC_ALL": "C.UTF-8",
        "LANG": "C.UTF-8"
    },
    // ... [Keep all existing customizations, runArgs, and workspace settings intact]
}
```

> **Architect's Warning:** After modifying this file, you MUST rebuild the container to apply the changes. In VS Code, press `F1` and select **Dev Containers: Rebuild Container**. Wait for the container to restart. Once finished, `npm` will be available in your integrated bash terminal.

---

## 2. Installation and Authentication

Once the DevContainer has been successfully rebuilt and you have an active terminal session **inside the container**, proceed with the installation of the Claude Code CLI.

Run the following commands strictly within the DevContainer terminal:

```bash
# 1. Install Claude Code globally using the newly injected npm
npm install -g @anthropic-ai/claude-code

# 2. Authenticate the CLI with your Anthropic account
claude login
```

### **Authentication Flow Details:**
* When you execute `claude login`, the terminal will generate a secure authentication link.
* **Action:** `Ctrl+Click` (or copy) this link and open it in your **Host OS** web browser.
* Complete the login/authorization process in your browser. 
* Once approved, the authentication token will automatically be injected back into your DevContainer session, securely linking the agent to your workspace.
