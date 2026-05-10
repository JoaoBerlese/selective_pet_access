/**
 * @file WiFiStationService.cpp
 * @author João Berlese
 * @brief Non-blocking Wi-Fi station FSM with exponential-backoff retry.
 *
 * Threading model:
 *   - wifi_event_handler() runs on the default ESP-IDF event-loop task. It does
 *     no work beyond translating ESP-IDF events into the compact internal `Event`
 *     and posting them onto event_queue_ with a zero-tick timeout.
 *   - task_loop() runs on the dedicated WiFiStationService task (Core 1). It owns
 *     all FSM transitions, all esp_wifi_connect() calls, and all observer
 *     notifications. Readers consult get_status() concurrently.
 *
 * Backoff: first failure waits initial_backoff_ms; each subsequent consecutive
 * failure doubles the window, capped at max_backoff_ms. Successful association
 * (STA_CONNECTED) resets both the failure counter and the window.
 */
#include "WiFiStationService.hpp"

#include <cstring>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>

namespace pet_access::network {

static const char* TAG = "WiFiStationService";

// =============================================================================
// Lifecycle
// =============================================================================

WiFiStationService::WiFiStationService(const Config& config, IWiFiObserver* observer)
    : config_(config), observer_(observer) {}

WiFiStationService::~WiFiStationService() {
    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Terminating WiFiStationService background task.");
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    if (event_queue_ != nullptr) {
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
    }
}

esp_err_t WiFiStationService::start() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "start() called twice; ignoring.");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Static event queue — zero-heap, sized to absorb any plausible event burst.
    event_queue_ = xQueueCreateStatic(
        kEventQueueDepth, sizeof(Event), reinterpret_cast<uint8_t*>(event_queue_buffer_), &event_queue_storage_
    );
    if (event_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create static event queue.");
        return ESP_ERR_NO_MEM;
    }

    // 2. Wi-Fi driver init (caller must have already called esp_netif_init,
    //    esp_event_loop_create_default, esp_netif_create_default_wifi_sta).
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Register event handlers — `this` is passed via arg so the static handler
    //    can resolve back to the instance.
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        return err;
    }

    // 4. Set station mode and credentials.
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_cfg{};
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid), config_.ssid, sizeof(wifi_cfg.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password), config_.password, sizeof(wifi_cfg.sta.password));
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    // 5. Spawn the FSM task BEFORE esp_wifi_start() — the start triggers
    //    WIFI_EVENT_STA_START, and we need a task ready to consume it.
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry, "wifi_station", config_.task_stack_size, this, config_.task_priority, &task_handle_, config_.task_core
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn WiFiStationService task (FreeRTOS heap exhausted?).");
        return ESP_ERR_NO_MEM;
    }

    // 6. Bring the radio up. STA_START arrives on the event handler shortly.
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(
        TAG,
        "Started on core %d at priority %u (ssid=\"%s\", initial backoff %lu ms, max %lu ms)",
        static_cast<int>(config_.task_core),
        static_cast<unsigned>(config_.task_priority),
        config_.ssid,
        static_cast<unsigned long>(config_.initial_backoff_ms),
        static_cast<unsigned long>(config_.max_backoff_ms)
    );
    return ESP_OK;
}

// =============================================================================
// Public API (Non-Blocking)
// =============================================================================

std::optional<WiFiStatus> WiFiStationService::get_status() const {
    if (!status_valid_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    rtos::LockGuard lock(status_mutex_);
    return status_;
}

// =============================================================================
// Event Handler (runs on the default event-loop task — must be non-blocking)
// =============================================================================

void WiFiStationService::wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto* self = static_cast<WiFiStationService*>(arg);
    if (self == nullptr || self->event_queue_ == nullptr) {
        return;
    }

    Event evt{};
    bool should_post = false;

    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                evt.code = EventCode::StaStart;
                should_post = true;
                break;
            case WIFI_EVENT_STA_CONNECTED:
                evt.code = EventCode::StaConnected;
                should_post = true;
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                auto* d = static_cast<wifi_event_sta_disconnected_t*>(data);
                evt.code = EventCode::StaDisconnected;
                evt.disconnect_reason = (d != nullptr) ? static_cast<uint8_t>(d->reason) : 0;
                should_post = true;
                break;
            }
            default:
                break;
        }
    } else if (base == IP_EVENT) {
        switch (id) {
            case IP_EVENT_STA_GOT_IP: {
                auto* g = static_cast<ip_event_got_ip_t*>(data);
                evt.code = EventCode::GotIp;
                if (g != nullptr) {
                    evt.ip = g->ip_info.ip;
                }
                should_post = true;
                break;
            }
            case IP_EVENT_STA_LOST_IP:
                evt.code = EventCode::LostIp;
                should_post = true;
                break;
            default:
                break;
        }
    }

    if (should_post) {
        BaseType_t ok = xQueueSend(self->event_queue_, &evt, 0);
        if (ok != pdTRUE) {
            // Static queue is sized for any plausible event burst; a full queue means
            // the FSM task is starved. Drop and log — never block in this context.
            ESP_LOGW(TAG, "Event queue full; dropping event (base=%s, id=%ld).", base, id);
        }
    }
}

// =============================================================================
// Background RTOS Task — FSM
// =============================================================================

void WiFiStationService::task_entry(void* arg) {
    auto* instance = static_cast<WiFiStationService*>(arg);
    instance->task_loop();
}

void WiFiStationService::task_loop() {
    // FSM scratch state — owned exclusively by this task.
    WiFiConnectionState state = WiFiConnectionState::Disconnected;
    uint32_t consecutive_failures = 0;
    uint32_t current_backoff_ms = config_.initial_backoff_ms;
    esp_ip4_addr_t current_ip{};
    uint8_t last_disc_reason = 0;

    auto publish_status = [&]() {
        WiFiStatus snapshot{state, current_ip, consecutive_failures, current_backoff_ms, last_disc_reason};
        {
            rtos::LockGuard lock(status_mutex_);
            status_ = snapshot;
        }
        // Release pairs with the acquire load in get_status().
        status_valid_.store(true, std::memory_order_release);
    };

    publish_status();

    while (true) {
        // Block forever in non-Backoff states; in Backoff, block only until the
        // retry window expires. A timeout return = "time to reconnect."
        TickType_t timeout_ticks =
            (state == WiFiConnectionState::Backoff) ? pdMS_TO_TICKS(current_backoff_ms) : portMAX_DELAY;

        Event evt{};
        BaseType_t got = xQueueReceive(event_queue_, &evt, timeout_ticks);

        if (got != pdTRUE) {
            // Backoff window elapsed — issue another connect attempt.
            ESP_LOGI(
                TAG,
                "Backoff window (%lu ms) elapsed; retrying connect (failures=%lu).",
                static_cast<unsigned long>(current_backoff_ms),
                static_cast<unsigned long>(consecutive_failures)
            );
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "esp_wifi_connect failed: %s; staying in Backoff.", esp_err_to_name(err));
                continue;
            }
            state = WiFiConnectionState::Connecting;
            publish_status();
            continue;
        }

        switch (evt.code) {
            case EventCode::StaStart: {
                ESP_LOGI(TAG, "STA_START received; issuing initial connect.");
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Initial esp_wifi_connect failed: %s", esp_err_to_name(err));
                    consecutive_failures = 1;
                    current_backoff_ms = config_.initial_backoff_ms;
                    state = WiFiConnectionState::Backoff;
                } else {
                    state = WiFiConnectionState::Connecting;
                }
                publish_status();
                break;
            }

            case EventCode::StaConnected:
                ESP_LOGI(TAG, "STA_CONNECTED; awaiting DHCP-assigned IP.");
                state = WiFiConnectionState::Connected;
                consecutive_failures = 0;
                current_backoff_ms = config_.initial_backoff_ms;
                publish_status();
                break;

            case EventCode::StaDisconnected: {
                last_disc_reason = evt.disconnect_reason;
                const bool was_online = (state == WiFiConnectionState::Online);

                consecutive_failures++;
                if (consecutive_failures == 1) {
                    current_backoff_ms = config_.initial_backoff_ms;
                } else {
                    uint64_t doubled = static_cast<uint64_t>(current_backoff_ms) * 2;
                    if (doubled > config_.max_backoff_ms) {
                        doubled = config_.max_backoff_ms;
                    }
                    current_backoff_ms = static_cast<uint32_t>(doubled);
                }

                ESP_LOGW(
                    TAG,
                    "STA_DISCONNECTED (reason=%u); entering Backoff for %lu ms (failures=%lu).",
                    last_disc_reason,
                    static_cast<unsigned long>(current_backoff_ms),
                    static_cast<unsigned long>(consecutive_failures)
                );

                current_ip = {};
                state = WiFiConnectionState::Backoff;
                publish_status();

                // Notify after publishing so observers querying status see Backoff/no-IP.
                if (was_online && observer_ != nullptr) {
                    observer_->on_ip_lost(last_disc_reason);
                }
                break;
            }

            case EventCode::GotIp:
                ESP_LOGI(TAG, "GOT_IP: " IPSTR, IP2STR(&evt.ip));
                current_ip = evt.ip;
                state = WiFiConnectionState::Online;
                consecutive_failures = 0;
                current_backoff_ms = config_.initial_backoff_ms;
                publish_status();
                if (observer_ != nullptr) {
                    observer_->on_ip_acquired(current_ip);
                }
                break;

            case EventCode::LostIp: {
                ESP_LOGW(TAG, "LOST_IP; awaiting reacquisition or disconnect.");
                const bool was_online = (state == WiFiConnectionState::Online);
                current_ip = {};
                // Stay associated; expect a fresh GOT_IP or a DISCONNECT to follow.
                state = WiFiConnectionState::Connected;
                publish_status();
                if (was_online && observer_ != nullptr) {
                    observer_->on_ip_lost(0);
                }
                break;
            }
        }
    }
}

}  // namespace pet_access::network
