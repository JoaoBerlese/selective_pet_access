#include "DiagnosticsService.hpp"

#include <esp_log.h>
#include <esp_timer.h>

namespace pet_access::services {

static const char* TAG = "DiagnosticsService";

void DiagnosticsService::push_error_record(ErrorSource source, esp_err_t err) {
    const uint64_t timestamp_us = esp_timer_get_time();

    // Multiple FreeRTOS tasks (TelemetryService @3, LidController @5, ApplicationManager @6,
    // and main during boot) all call this. The LockGuard serializes the indexed write +
    // head advance + count cap into one atomic critical section.
    rtos::LockGuard lock(error_mutex_);

    error_buffer_[error_buffer_head_] =
        SystemErrorRecord{.timestamp_us = timestamp_us, .error_code = err, .source = source};

    error_buffer_head_ = (error_buffer_head_ + 1) % MAX_OFFLINE_ERRORS;

    if (error_buffer_count_ < MAX_OFFLINE_ERRORS) {
        error_buffer_count_++;
    }

    ESP_LOGD(
        TAG,
        "Recorded error: source=%u code=%s ts_us=%llu (buffer %zu/%zu)",
        static_cast<unsigned>(source),
        esp_err_to_name(err),
        static_cast<unsigned long long>(timestamp_us),
        error_buffer_count_,
        MAX_OFFLINE_ERRORS
    );
}

std::optional<SystemErrorRecord> DiagnosticsService::pop_error_record() {
    rtos::LockGuard lock(error_mutex_);

    if (error_buffer_count_ == 0) {
        return std::nullopt;
    }

    // Compute tail: oldest entry is (head - count) mod capacity.
    size_t tail = (MAX_OFFLINE_ERRORS + error_buffer_head_ - error_buffer_count_) % MAX_OFFLINE_ERRORS;
    SystemErrorRecord record = error_buffer_[tail];
    error_buffer_count_--;

    return record;
}

size_t DiagnosticsService::get_error_count() const {
    rtos::LockGuard lock(error_mutex_);
    return error_buffer_count_;
}

}  // namespace pet_access::services
