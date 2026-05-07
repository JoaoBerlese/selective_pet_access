# Claude Code Payload Template

```bash
# 1. Run Claude Code (adjust path if necessary)
claude

# 2. Inside Claude Code, set the configuration:
/model [MODEL]
/effort [EFFORT]
# [Action Required]: Press Shift+Tab and toggle [Plan Mode / Accept Edits / None] ON.

# 3. Paste the following execution prompt:
```

> **Context:** Review `./CLAUDE.md` and `./docs/06_Firmware_Design_Guidelines.md` before proceeding.
> **Task:** [TASK_DESCRIPTION]
> **Constraints:**
> - Mimic the structure of `components/example_service/` (including `CMakeLists.txt` `REQUIRES` vs `PRIV_REQUIRES`).
> - Enforce concrete-first DI, `rtos::StaticMutex`, and zero dynamic allocation.
> - [ARCHITECTURAL_WIRING]
> [KCONFIG_CHANGES]
