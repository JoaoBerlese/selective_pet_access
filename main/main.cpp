#include <cstdio>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "Main";

extern "C" void app_main(void) {
    // C++20 Lambda verification
    auto print_status = []() {
        ESP_LOGI(TAG, "System Running: FreeRTOS Scheduler Active (CPP20)");
    };

    while (true) {
        print_status();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
