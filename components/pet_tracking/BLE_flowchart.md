
### 1. **System Controller Start**

* `main.cpp` calls `SystemController.start()`.
* `SystemController` is the "brain" initializing and connecting everything: I2C, Sensors, LED, Servo, **BLE Queue, NimbleScanner, PetProximityTracker**.

### 2. **BLE Machine Setup & Initialization**

* **PetProximityTracker:** Started on Core 1 App CPU (`pet_tracker.start()`).
    * ⬇️ Immediately starts the scanner (`scanner.start()`).
* **NimbleScanner:** Started implicitly by the tracker.
    * ⬇️ Call `scanner.initialize()`.
    * ⬇️ **Core 0 Baseband Boot:** NimBLE stack begins booting on Core 0 Pro CPU. *Blocks main execution (semaphore)*, ensuring clean startup sequence.
    * ⬇️ **Synced!** C stack (`sync_cb`) signals the semaphore, unblocking `initialize()` and allowing `main` to continue, and the **Tracker App Task** (`pet_tracker_tsk`) to begin blocking on the queue. *Core separation prevents app logic from blocking baseband.*

### 3. **Live Operation Flow (THE LIFE OF A BLE MESSAGE)**

Imagine Frodo walks up to the feeder...

* 📡 **Radio Detection (Core 0 Baseband):** The HolyIoT beacon on Frodo's collar broadcasts a message. The ESP32-S3 radio detects it.
* 📡 **Interrupt Callback (Core 0 C context):** NimBLE stack triggers an interrupt callback (`on_discovery`), running briefly in Core 0 C context.
* 📡 **Core 0 C++ Bridge (`NimbleScanner`):** Callback uses **static routing** to call your C++ object instance (`handle_discovery`), still on Core 0.
* 📡 **Fire & Forget Queue (`NimbleScanner`):** `handle_discovery` makes a safe, **deep copy** of the payload bytes into a stack-allocated **`BeaconEvent`** struct. It immediately pushes this structured event across the RTOS boundary using `xQueueSend(0 timeout)` to the shared FreeRTOS queue. *Zero blocking on Core 0 ensures baseband timing isn't disrupted.* If the queue is full, the message is dropped, prioritizing system stability over guaranteed delivery.

_The message is now safely across the core boundary!_

* ⬇️ **App Task Processing (Core 1, `PetProximityTracker`):** The dedicated **Pet Tracker App Task** (`pet_tracker_tsk`), running on Core 1 App CPU, wakes up as `xQueueReceive` returns the `BeaconEvent`.
* ⬇️ **Zero-Copy Parsing (`EddystoneParser`):** App task calls `EddystoneParser::parse(event.get_payload_span())`. *Zero copies of the underlying payload bytes, safe bounds-checked decoding!* It returns an `optional` with the parsed data.
* ⬇️ **Business Logic & State (App Task, `PetProximityTracker`):**
    * If the parser successfully extracted valid Eddystone data...
        * ➡️ UID Frame + Frodo's ID: Associates the incoming MAC address as Frodo's collar (using the **Pragmatic Mask** logic if needed), then updates the **Exponential Moving Average (EMA)** filter and the **Schmitt Trigger state machine** logic for both UID *and* TLM pings. *Associating MAC ensures only the correct beacon updates state.*
        * ➡️ TLM Frame + *Related MAC* (from association): Updates the `battery_mv_` atomic variable.
    * ➡️ *Always updates EMA filter & Schmitt Trigger* for every related beacon message.
    * ➡️ *Times out gracefully:* If the task wakes up on queue receive timeout (e.g., 1000ms wake intervals) and 5 seconds have elapsed since last related ping, automatically transitions state to `Away` and resets EMA, ensuring door logic doesn't remain open indefinitely after pet moves away.
* ⬇️ **Atomic State Update:** Modifies your `std::atomic<ProximityState> current_state_`, `std::atomic<float> ema_rssi_`, etc., with correct `acquire`/`release` memory ordering. *Thread-safe, lock-free access means any other part of your system can read the state instantly without blocking or priority inversion!*
