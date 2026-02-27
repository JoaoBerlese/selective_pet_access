#include "smart_led.hpp"

#include "esp_check.h"
#include "esp_log.h"

namespace pet_access::ui {

static const char* TAG = "SmartLED";

// =============================================================================
// Lifecycle & Hardware Initialization
// =============================================================================

SmartLed::SmartLed(const SmartLedConfig& config) {
    // 1. Configure Hardware (RMT)
    led_strip_config_t strip_conf = {};
    strip_conf.strip_gpio_num = config.pin;
    strip_conf.max_leds = 1;
    strip_conf.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_conf.led_model = LED_MODEL_WS2812;
    strip_conf.flags.invert_out = false;

    led_strip_rmt_config_t rmt_conf = {};
    rmt_conf.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_conf.resolution_hz = config.rmt_resolution_hz;
    rmt_conf.flags.with_dma = false;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_conf, &rmt_conf, &strip_handle_));

    // 2. Create Queue
    cmd_queue_ = xQueueCreate(QUEUE_LENGTH, sizeof(Message));
    if (!cmd_queue_) {
        ESP_LOGE(TAG, "Failed to create command queue");
        abort();  // Enforce strict failure
    }

    // 3. Start Task
    // Passing 'this' securely maps the static RTOS function back to this instance.
    BaseType_t ret = xTaskCreatePinnedToCore(
        SmartLed::task_entry,
        "SmartLED_Task",
        config.task_stack_size,  // Injected
        this,
        config.task_priority,  // Injected
        &task_handle_,
        config.task_core  // Injected
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        abort();
    }
}

SmartLed::~SmartLed() {
    // RAII Rule: Teardown must be the exact reverse of allocation,
    // and must be safe against dangling execution.

    // 1. Kill the task first so it stops accessing the queue/strip
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    // 2. Safely clear the queue
    if (cmd_queue_ != nullptr) {
        vQueueDelete(cmd_queue_);
        cmd_queue_ = nullptr;
    }

    // 3. Release hardware
    if (strip_handle_ != nullptr) {
        led_strip_del(strip_handle_);
        strip_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "SmartLed resources safely freed.");
}

// =============================================================================
// Public API (Thread-Safe Queue Pushers)
// =============================================================================

void SmartLed::set_static(uint8_t r, uint8_t g, uint8_t b) noexcept {
    Message msg{Mode::Static, r, g, b, 0};
    xQueueSend(cmd_queue_, &msg, 0);
}

void SmartLed::set_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms) noexcept {
    Message msg{Mode::Blink, r, g, b, period_ms};
    xQueueSend(cmd_queue_, &msg, 0);
}

void SmartLed::set_breath(uint8_t r, uint8_t g, uint8_t b) noexcept {
    Message msg{Mode::Breath, r, g, b, 0};
    xQueueSend(cmd_queue_, &msg, 0);
}

void SmartLed::set_off() noexcept {
    Message msg{Mode::Off, 0, 0, 0, 0};
    xQueueSend(cmd_queue_, &msg, 0);
}

// =============================================================================
// Private Utilities & Math
// =============================================================================

constexpr uint8_t SmartLed::gamma_correct(uint8_t input) noexcept {
    return static_cast<uint8_t>((static_cast<uint16_t>(input) * input) / 255);
}

// =============================================================================
// RTOS Context & State Machine (Private)
// =============================================================================

void SmartLed::task_entry(void* arg) {
    auto* self = static_cast<SmartLed*>(arg);
    self->task_loop();
}

void SmartLed::task_loop() {
    Message current_state{Mode::Off, 0, 0, 0, 0};
    TickType_t wait_ticks = portMAX_DELAY;

    bool blink_toggle = false;
    int16_t breath_val = 0;
    int8_t breath_dir = 1;

    while (true) {
        Message new_msg;

        if (xQueueReceive(cmd_queue_, &new_msg, wait_ticks) == pdTRUE) {
            current_state = new_msg;
            blink_toggle = false;
            breath_val = 0;
            breath_dir = 1;
        }

        switch (current_state.mode) {
            case Mode::Static:
                led_strip_set_pixel(strip_handle_, 0, current_state.r, current_state.g, current_state.b);
                led_strip_refresh(strip_handle_);
                wait_ticks = portMAX_DELAY;
                break;

            case Mode::Blink:
                if (blink_toggle) {
                    led_strip_set_pixel(strip_handle_, 0, current_state.r, current_state.g, current_state.b);
                } else {
                    led_strip_clear(strip_handle_);
                }
                led_strip_refresh(strip_handle_);
                blink_toggle = !blink_toggle;
                wait_ticks = pdMS_TO_TICKS(current_state.blink_period_ms);
                break;

            case Mode::Breath: {
                breath_val += (breath_dir * 5);
                if (breath_val >= 255) {
                    breath_val = 255;
                    breath_dir = -1;
                } else if (breath_val <= 0) {
                    breath_val = 0;
                    breath_dir = 1;
                }

                uint32_t gamma = gamma_correct(static_cast<uint8_t>(breath_val));
                uint8_t r = (current_state.r * gamma) / 255;
                uint8_t g = (current_state.g * gamma) / 255;
                uint8_t b = (current_state.b * gamma) / 255;

                led_strip_set_pixel(strip_handle_, 0, r, g, b);
                led_strip_refresh(strip_handle_);
                wait_ticks = pdMS_TO_TICKS(20);
                break;
            }

            case Mode::Off:
            default:
                led_strip_clear(strip_handle_);
                led_strip_refresh(strip_handle_);
                wait_ticks = portMAX_DELAY;
                break;
        }
    }
}

}  // namespace pet_access::ui