/**
 * @file DiagnosticsService.hpp
 * @author João Berlese
 * @brief Thread-safe, zero-heap error ring buffer for cross-cutting error persistence.
 *
 * Passive sink — no background task. A future cloud-flush task will compose over
 * this service rather than live inside it.
 */
#pragma once

#include <esp_err.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "rtos.hpp"  // pet_access::rtos::StaticMutex, LockGuard

namespace pet_access::services {

// Identifies the subsystem (or HAL driver) that produced a SystemErrorRecord.
// uint8_t-backed; entries are append-only to keep the cloud wire format stable.
enum class ErrorSource : uint8_t {
    Unknown = 0,
    DistanceSensor = 1,
    TempSensor = 2,
    Servo = 3,
    LidController = 4,
    BleScanner = 5,
};

// Cloud-ready record for a single error event.
// Layout (16 B, naturally aligned): timestamp_us (8) | error_code (4) | source (1) + pad (3)
struct SystemErrorRecord {
    uint64_t timestamp_us;
    esp_err_t error_code;
    ErrorSource source;
    // Note: cloud sync status (bool sent / uint8_t retry_count) lands here in the next milestone.
};

class DiagnosticsService final {
public:
    DiagnosticsService() = default;
    ~DiagnosticsService() = default;

    // Rule of Five — resource-owning type (mutex), copy and move both forbidden.
    DiagnosticsService(const DiagnosticsService&) = delete;
    DiagnosticsService& operator=(const DiagnosticsService&) = delete;
    DiagnosticsService(DiagnosticsService&&) = delete;
    DiagnosticsService& operator=(DiagnosticsService&&) = delete;

    // Append an error record to the offline ring buffer. Thread-safe.
    // Silently overwrites the oldest record when full — freshest event has most diagnostic value.
    void push_error_record(ErrorSource source, esp_err_t err);

    // Remove and return the oldest record (FIFO). Returns nullopt when the buffer is empty.
    std::optional<SystemErrorRecord> pop_error_record();

    // Returns the number of records currently in the buffer.
    size_t get_error_count() const;

private:
    // 32 entries × 16 B = 512 B in DRAM.
    static constexpr size_t MAX_OFFLINE_ERRORS = 32;

    // mutable: allows const methods (e.g., get_error_count) to acquire the mutex for thread-safe access to the buffer
    mutable rtos::StaticMutex error_mutex_;

    // Ring buffer state — ALL three fields are covered by error_mutex_.
    std::array<SystemErrorRecord, MAX_OFFLINE_ERRORS> error_buffer_{};
    size_t error_buffer_head_{0};
    size_t error_buffer_count_{0};
};

}  // namespace pet_access::services
