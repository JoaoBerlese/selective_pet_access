/**
 * @file WiFiStationService.hpp
 * @author João Berlese
 * @brief Non-blocking Wi-Fi station manager with exponential-backoff state machine.
 *
 * Architectural skeleton — see docs/06_Firmware_Design_Guidelines.md §12 for the
 * rationale behind every line. The state-machine body is deferred to a follow-up
 * implementation pass; this header pins the public surface.
 *
 *   - Concrete-first DI: WiFiStationService is final, no service-level interface (§6.1 YAGNI).
 *   - IWiFiObserver is the only abstract type — it is the extension point.
 *   - StaticMutex + LockGuard for the status snapshot; std::atomic for the publish barrier.
 *   - FreeRTOS trampoline task; statically-allocated event queue (zero-heap in steady state).
 *   - ESP-IDF Wi-Fi/IP event handler is non-blocking — it only posts to the static queue.
 *   - The component never calls esp_netif_init() / esp_event_loop_create_default(); the caller
 *     (SystemController) is responsible for the system-level network stack init.
 *   - SSID/password are injected via Config; the component never #includes sdkconfig.h.
 */
#pragma once

#include <esp_err.h>
#include <esp_event_base.h>
#include <esp_netif_ip_addr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "rtos.hpp"  // pet_access::rtos::StaticMutex, LockGuard

namespace pet_access::network {

// IEEE 802.11 limits — kept as raw chars so esp_wifi types stay out of the public header.
inline constexpr std::size_t kSsidMaxLen = 32;
inline constexpr std::size_t kPasswordMaxLen = 64;

/**
 * @brief Coarse station-mode lifecycle state, surfaced through WiFiStatus.
 */
enum class WiFiConnectionState : uint8_t {
    Idle = 0,      // Service constructed, start() not yet called
    Disconnected,  // Driver up, not associated, not currently retrying
    Connecting,    // esp_wifi_connect() issued, awaiting STA_CONNECTED
    Connected,     // Associated, awaiting GOT_IP
    Online,        // GOT_IP received — ip_addr in WiFiStatus is valid
    Backoff,       // Disconnected, sleeping current_backoff_ms before retry
    Fault          // Unrecoverable driver init failure
};

/**
 * @brief Snapshot returned by WiFiStationService::get_status().
 *
 * Trivially copyable so it can cross task boundaries by value.
 */
struct WiFiStatus {
    WiFiConnectionState state;
    esp_ip4_addr_t ip_addr;          // Zero unless state == Online
    uint32_t consecutive_failures;   // Reset on successful association
    uint32_t current_backoff_ms;     // Live backoff window when state == Backoff
    uint8_t last_disconnect_reason;  // wifi_err_reason_t cast; 0 if never disconnected
};

/**
 * @brief Observer callback for IP acquisition and loss.
 *
 * @warning Implementations MUST NOT block. Methods run in the WiFiStationService task
 * context. Forward the event to your own task via xQueueSend(timeout=0) or xTaskNotify
 * and return immediately.
 *
 * Observer interfaces are always abstract (§6.1) — they represent the extension point
 * external consumers implement. Orthogonal to the YAGNI rule for service interfaces.
 */
class IWiFiObserver {
public:
    virtual ~IWiFiObserver() = default;
    virtual void on_ip_acquired(const esp_ip4_addr_t& ip) = 0;
    virtual void on_ip_lost(uint8_t disconnect_reason) = 0;
};

class WiFiStationService final {
public:
    /**
     * @brief All tunables injected at construction. No #defines, no menuconfig
     * lookups inside the class.
     */
    struct Config {
        char ssid[kSsidMaxLen];
        char password[kPasswordMaxLen];

        // Backoff: doubles from initial up to max on each consecutive failure.
        uint32_t initial_backoff_ms;
        uint32_t max_backoff_ms;

        // FreeRTOS task placement — injected, never hard-coded.
        UBaseType_t task_priority;  // From sys::PRIORITY_*
        BaseType_t task_core;       // 0 or 1
        uint32_t task_stack_size;   // Bytes
    };

    /**
     * @brief Dependency injection.
     * @param config   Tunables; copied into the instance.
     * @param observer Optional non-owning pointer; nullptr disables push notifications.
     */
    explicit WiFiStationService(const Config& config, IWiFiObserver* observer = nullptr);

    /// RAII cleanup; tears down the FreeRTOS task and event queue if running.
    ~WiFiStationService();

    // Rule of Five — resource-owning type, copy and move both forbidden.
    WiFiStationService(const WiFiStationService&) = delete;
    WiFiStationService& operator=(const WiFiStationService&) = delete;
    WiFiStationService(WiFiStationService&&) = delete;
    WiFiStationService& operator=(WiFiStationService&&) = delete;

    /**
     * @brief Initialize the Wi-Fi driver and spawn the state-machine task.
     *
     * Caller MUST have already invoked, in order: nvs_flash_init(), esp_netif_init(),
     * esp_event_loop_create_default(), esp_netif_create_default_wifi_sta(). This service
     * owns only the Wi-Fi driver layer (esp_wifi_init / set_mode / set_config / start)
     * plus the connection lifecycle.
     *
     * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already started, or any
     *         esp_wifi_* error propagated upward.
     */
    [[nodiscard]] esp_err_t start();

    /**
     * @brief Thread-safe snapshot of the current station status.
     * @return std::nullopt before the first state transition publishes a snapshot.
     */
    [[nodiscard]] std::optional<WiFiStatus> get_status() const;

private:
    // Trampoline: FreeRTOS C ABI -> C++ instance method.
    static void task_entry(void* arg);
    void task_loop();

    // ESP-IDF event handler — runs on the default event loop task.
    // MUST be non-blocking; it only marshals events into event_queue_.
    static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data);

    /// Internal compact event code published from wifi_event_handler into event_queue_.
    enum class EventCode : uint8_t {
        StaStart,
        StaConnected,
        StaDisconnected,
        GotIp,
        LostIp,
    };
    struct Event {
        EventCode code;
        uint8_t disconnect_reason;  // populated for StaDisconnected
        esp_ip4_addr_t ip;          // populated for GotIp
    };

    // ---- Injected dependencies (non-owning) ---------------------------------
    const Config config_;
    IWiFiObserver* const observer_;

    // ---- RTOS handles ------------------------------------------------------
    TaskHandle_t task_handle_{nullptr};
    QueueHandle_t event_queue_{nullptr};
    StaticQueue_t event_queue_storage_{};
    static constexpr std::size_t kEventQueueDepth = 8;
    Event event_queue_buffer_[kEventQueueDepth]{};

    // ---- Shared state ------------------------------------------------------
    // The mutex protects the multi-field snapshot. Hot-path readers can also
    // poll status_valid_ without taking the mutex (acquire ordering).
    mutable rtos::StaticMutex status_mutex_;
    WiFiStatus status_{};
    std::atomic<bool> status_valid_{false};
};

}  // namespace pet_access::network
