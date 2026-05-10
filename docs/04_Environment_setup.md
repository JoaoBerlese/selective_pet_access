# 04 · Environment Setup

**Role:** Anyone preparing a machine to build this firmware.

This doc merges what was previously spread across four files: the infrastructure deep-dive, the scaffolding script, the Git/SSH procedure, and the Claude Code integration. **The canonical source of truth for every configuration file is the file itself in this repository**, not a copy in this doc. Rationale, not replication.

---

## §1 · Fast path (contributor, env already working)

If the Dev Container has opened successfully and `ls /dev/ttyACM*` returns `/dev/ttyACM0` inside the container, skip to **[01 · Developer Guide](01_Developer_guide.md)**.

Minimum host prerequisites:

1. **VS Code** — <https://code.visualstudio.com/>
2. **Docker** — Docker Desktop (Windows/macOS) or Docker Engine (Linux, with `usermod -aG docker $USER`).
3. **Dev Containers extension** — `ms-vscode-remote.remote-containers`.
4. ESP32-S3 plugged into USB **before** opening VS Code (the container maps the device only at startup).

Reopen the folder in the container (`F1 → Dev Containers: Reopen in Container`). When CMake Tools asks to "Select a Kit", choose **`[Unspecified]`** — we must let `idf.py` manage the cross-compiler.

---

## §2 · From scratch (architect)

Use this section when standing up a new machine, a new CI runner, or a fresh fork. The actual file contents are committed to this repo; this section only explains **why** each piece is shaped the way it is.

### 2.1 Host install (Linux)

Do **not** use Docker Desktop on Linux — it adds a VM layer that costs USB-passthrough latency on JTAG flashes. Use the native engine:

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER    # rootless Docker: lets VS Code start containers without sudo
sudo usermod -aG dialout $USER   # serial port access: /dev/ttyACM0 is owned by root:dialout;
                                  # without this group, the host (and Docker passthrough) will
                                  # deny access to the ESP32 serial port even with --privileged
newgrp docker                    # apply now; restart if 'docker run hello-world' still fails
```

### 2.2 `.devcontainer/devcontainer.json` — why it looks like that

Canonical file: [`.devcontainer/devcontainer.json`](../.devcontainer/devcontainer.json).

- **Base image `espressif/idf:v5.3.4`** — pinned to a specific patch release for reproducible builds. The `release-v5.3` floating tag is avoided: it changes silently when Espressif pushes patches, breaking reproducibility.
- **`--privileged` only (no `--device`)** — `--privileged` is required for OpenOCD to claim the USB-JTAG endpoint via libusb. The `--device=/dev/ttyACM0` line is intentionally absent: specifying it causes Docker to refuse container startup if the ESP32 is not plugged in at that moment. `--privileged` already grants access to all host devices when the ESP32 is present.
- **Idempotent `postCreateCommand`** — uses a `grep` guard before appending to `.bashrc`, so container rebuilds do not accumulate duplicate `source` lines. The Espressif image ENTRYPOINT already sets `IDF_PATH`; the `.bashrc` line is only needed for manually-opened interactive shells.
- **Pinned VS Code extensions** — C/C++, CMake Tools, ESP-IDF, Catch2 test adapter. Pinning in `devcontainer.json` means new contributors get the same IDE on first open.

### 2.3 Build system — why root CMake is 6 lines

Canonical file: [`CMakeLists.txt`](../CMakeLists.txt).

- `CMAKE_CXX_STANDARD 20` + `CMAKE_CXX_EXTENSIONS OFF` — strict C++20, no GNU extensions. Keeps the code portable and forces `std::` over `__builtin_`.
- All real work lives in ESP-IDF's `project.cmake` include; we only impose the C++ standard on top.

### 2.4 `sdkconfig.defaults` — never edit `sdkconfig` directly

Canonical file: [`sdkconfig.defaults`](../sdkconfig.defaults).

`sdkconfig` is generated and git-ignored. `sdkconfig.defaults` is the source of truth. Key entries:

- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` — matches the N16 part.
- `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_MODE_OCT=y` — enables the 8 MB Octal PSRAM at the right speed.
- `CONFIG_PARTITION_TABLE_CUSTOM=y` — points at our 16 MB layout (see **[03 · Hardware §6](03_Hardware.md#6-flash-partition-layout)**).
- `CONFIG_COMPILER_CXX_EXCEPTIONS=n` + `CONFIG_COMPILER_CXX_RTTI=n` — explicitly disables exceptions and RTTI at the Kconfig level, enforcing the `-fno-exceptions`/`-fno-rtti` architectural constraint. Without these entries, ESP-IDF defaults may silently enable them.

To change config: `idf.py menuconfig` to edit, then `idf.py save-defconfig` to persist.

### 2.5 `.vscode/*.json` — what each file enforces

Canonical files: [`.vscode/settings.json`](../.vscode/settings.json), [`launch.json`](../.vscode/launch.json), [`c_cpp_properties.json`](../.vscode/c_cpp_properties.json).

- **`settings.json`** — explicit CMake path (prevents "no such file" errors), format-on-save with clang-format, `idf.openOcdConfigs = ["board/esp32s3-builtin.cfg"]` so F5 debugging works with no external probe.
- **`launch.json`** — `tmoScaleFactor: 2` doubles OpenOCD timeouts to absorb USB-passthrough latency. Without this, debug sessions fail intermittently inside Docker.
- **`c_cpp_properties.json`** — binds IntelliSense to `build/compile_commands.json` so symbols resolve after the first build.

### 2.6 `.clang-format` — Google style, modified

Canonical file: [`.clang-format`](../.clang-format). Two deliberate deviations from stock Google:

- **4-space indent**, not 2 — easier to follow in the dense HAL/template code.
- **`ColumnLimit: 120`** — 80 is too narrow for the C++20 template and `esp_err_t` error-check patterns we use.

### 2.7 `.clang-tidy` — static analysis enforcing the Berlese Standard

Canonical file: [`.clang-tidy`](../.clang-tidy). Activates checks that mechanically enforce the project's architectural rules: `cppcoreguidelines-no-malloc` (no raw heap allocation), Rule of Five compliance, `modernize-use-nodiscard` (flags unchecked `esp_err_t` returns), and `concurrency-mt-unsafe`. The ESP-IDF extension runs it automatically when `compile_commands.json` is present (generated by `idf.py build`).

### 2.8 · Environment health check

After completing the host setup in §2.1, run the health check script from the project root to verify your environment before opening VS Code:

```bash
cd selective_pet_access
./tools/sanity_check.sh
```

The script checks four things and exits non-zero if any required check fails:

1. Docker is installed and in `PATH`.
2. The Docker daemon is running and reachable without `sudo`.
3. Your user is in the `docker` and `dialout` groups *(Linux only)*.
4. An ESP32-S3 (Espressif USB VID `303a`) is detected on USB *(warning only — does not fail the script)*.

A clean run looks like:

```
[PASS] Docker found: Docker version 27.x.x, build ...
[PASS] Docker daemon is running and reachable without sudo.
[PASS] User 'you' is in the 'docker' group.
[PASS] User 'you' is in the 'dialout' group.
[WARN] No ESP32-S3 detected on USB.   ← safe to ignore until you're ready to flash
```

### 2.9 · `sdkconfig.defaults.local` — local-only Wi-Fi credentials

Canonical files:

- [`components/wifi_station/Kconfig.projbuild`](../components/wifi_station/Kconfig.projbuild) — declares `CONFIG_PET_WIFI_SSID` and `CONFIG_PET_WIFI_PASSWORD` with **placeholder defaults** (`"YOUR_SSID_HERE"` / `"YOUR_PASSWORD_HERE"`). These are the values used by any fresh clone.
- [`sdkconfig.defaults`](../sdkconfig.defaults) — committed, deliberately does **not** carry the Wi-Fi keys. `idf.py save-defconfig` only writes a key when its value differs from the Kconfig default; leaving the placeholders unmodified keeps `sdkconfig.defaults` clean of credentials by construction.
- `sdkconfig.defaults.local` — per-developer, gitignored. Holds your real SSID/password.

ESP-IDF's build system **automatically merges `sdkconfig.defaults.local` on top of `sdkconfig.defaults`** at configure time — no build-system changes are needed. Real credentials never enter version control.

**First-time setup:**

1. From the project root, create `sdkconfig.defaults.local`:
   ```
   CONFIG_PET_WIFI_SSID="my-home-2.4"
   CONFIG_PET_WIFI_PASSWORD="my-wifi-password"
   ```
2. Rerun configure: `idf.py reconfigure && idf.py build`.
3. On boot, the `WiFiStationService` log line should show your SSID and the device should reach the `Online` state.

**Verify the file is ignored:**

```bash
git check-ignore -v sdkconfig.defaults.local
# Expected: .gitignore:NN:sdkconfig.defaults.local   sdkconfig.defaults.local
```

**Hard rules:**

- **Never** commit `sdkconfig.defaults.local`. The `.gitignore` rule prevents it, but `git add -f` would override.
- **Never** run `idf.py save-defconfig` while your local SSID/password are loaded — it would write the real credentials into `sdkconfig.defaults` (because they now differ from the Kconfig placeholder defaults). If you genuinely need to capture an unrelated config tweak, temporarily blank out `CONFIG_PET_WIFI_SSID` / `CONFIG_PET_WIFI_PASSWORD` back to the Kconfig placeholders first, run `save-defconfig`, then restore your local file.
- **Never** edit `sdkconfig` directly — same rule as §2.4. The local file is the only acceptable channel for personal credentials.

---

## §3 · Git & SSH (host → container)

Generate SSH keys on the **host OS** (not inside the container). The container inherits `SSH_AUTH_SOCK` from the host via agent forwarding, which means keys survive container rebuilds and are never written into the image.

```bash
# --- On the host ---
ssh-keygen -t ed25519 -C "you@example.com"
cat ~/.ssh/id_ed25519.pub    # paste into GitHub → SSH keys
```

Add this to host `~/.bashrc` so the agent starts automatically. The `ssh-add -l` check handles both "no agent running" and "stale socket from a previous session" — two cases that the simpler `[ -z "$SSH_AUTH_SOCK" ]` check misses:

```bash
if ! ssh-add -l &>/dev/null; then
    eval "$(ssh-agent -s)" > /dev/null
    ssh-add ~/.ssh/id_ed25519
fi
```

**Preferred alternative** — add to `~/.ssh/config` instead of `.bashrc`. This delegates agent management to `ssh` on first use and works correctly across zsh, non-interactive shells, and WSL2:

```
Host github.com
    AddKeysToAgent yes
    IdentityFile ~/.ssh/id_ed25519
```

Then rebuild the container and verify from **inside** it:

```bash
ssh -T git@github.com
# Expected: "Hi <user>! You've successfully authenticated..."
```

Set the remote with the SSH URL (not HTTPS) so you never need to type a token:

```bash
git remote add origin git@github.com:<you>/selective_pet_access.git
git push -u origin main
```

---

## §4 · Optional — AI Assistants (Claude & Gemini) CLI inside the container

The Espressif image ships without Node.js. The Node feature in `.devcontainer/devcontainer.json` adds it:

```jsonc
"features": {
    "ghcr.io/devcontainers/features/node:1": { "version": "lts" }
}
```

And the `postCreateCommand` installs both AI CLIs automatically on container create or rebuild — no manual install step needed:

```json
"install-claude-code": "npm install -g @anthropic-ai/claude-code",
"install-gemini-cli":  "npm install -g @google/gemini-cli"
```

### First-time authentication

Run these once inside a container terminal. Auth tokens are persisted in named Docker volumes (`claude-code-global-config` → `/root/.claude`, `gemini-cli-global-config` → `/root/.gemini`) and survive container rebuilds.

**Claude Code:**
```bash
claude login      # Ctrl+Click the printed URL; complete in host browser
```

**Gemini CLI:**
```bash
gemini login
```

**Gemini — port-forwarding required:** the container has no display server, so `gemini login` tries to invoke `xdg-open` and fails with `ENOENT`. The OAuth redirect URL it prints contains a **random ephemeral port** (e.g., `http://127.0.0.1:35331/...`) that only exists inside the container. Simply pasting the URL into the host browser will fail because that port is not exposed.

**Step-by-step:**
1. Run `gemini login` in the container terminal. It will print a URL and hang waiting for the callback.
2. Note the port number in the printed URL (e.g., `35331`).
3. Open the VS Code **Ports** tab (bottom panel → *Ports*) and click **Forward a Port**. Enter the exact port number from step 2.
4. Once the port is forwarded, copy the full URL from the terminal and open it in your **host** browser. Complete the Google OAuth flow normally.
5. The terminal will unblock and confirm authentication.
>
**Noise suppression:** set `NO_BROWSER=true` in the terminal before running `gemini login` to prevent the `xdg-open` error from printing — it does not affect the OAuth flow itself:
```bash
NO_BROWSER=true gemini login
```

---

## §5 · Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `CMake Error: Bad Executable` on first build | Wrong CMake Kit selected | `F1 → CMake: Select a Kit → [Unspecified]` |
| `No such file or directory: /dev/ttyACM0` | Device was not plugged in when container started | Plug in device, then `F1 → Developer: Reload Window` (not rebuild — faster) |
| `Permission Denied` on Docker (Linux) | User not in `docker` group | `sudo usermod -aG docker $USER` then log out / restart |
| `Permission Denied: /dev/ttyACM0` inside container | User not in `dialout` group | `sudo usermod -aG dialout $USER` then log out / restart |
| Flash timeout | USB-passthrough latency | Bootloader mode: hold `BOOT`, tap `RST`, release `BOOT`, retry |
| IntelliSense red squiggles after rebuild | CMake cache lost | `F1 → Developer: Reload Window` |
| `ssh -T git@github.com` → permission denied inside container | Host agent not forwarding | Confirm `SSH_AUTH_SOCK` is set on host; restart host terminal so the agent re-initializes |
