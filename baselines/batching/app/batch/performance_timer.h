#ifndef PERFORMANCE_TIMER_H
#define PERFORMANCE_TIMER_H

/*
 * Performance timer infrastructure for tracking in-enclave vs out-of-enclave execution time.
 *
 * This module provides fine-grained timing to measure:
 * - In-enclave time: Time spent executing code inside the enclave (ecalls)
 * - Out-of-enclave time: Time spent in untrusted code (including ocalls)
 * - Total session time: Overall wall-clock time from start to measurement
 *
 * The timing is integrated with the ecall/ocall wrapper infrastructure to
 * automatically track execution domains.
 */

#include <chrono>
#include <atomic>
#include <cstdint>

// Global performance timers (nanoseconds)
extern std::atomic<uint64_t> g_in_enclave_time_ns;
extern std::atomic<uint64_t> g_out_enclave_time_ns;

// Per-category ecall timers (wall-clock, measured from untrusted side)
extern std::atomic<uint64_t> g_sort_time_ns;        // heap_sort + k_way_merge_*
extern std::atomic<uint64_t> g_shuffle_time_ns;     // k_way_shuffle_* + waksman
extern std::atomic<uint64_t> g_fileio_time_ns;      // ecall_encrypt_entry + ecall_decrypt_entry + ecall_obtain_output_size
extern std::atomic<uint64_t> g_batch_total_time_ns; // ecall_batch_dispatcher full wall-clock (outer measurement)

// AKS distribute ocall timing (untrusted-side I/O, not counting in-EPC work)
extern std::atomic<uint64_t> g_aks_scan_ocall_ns;  // ocall_aks_read_scan_flat (scan phase reads)
extern std::atomic<uint64_t> g_aks_swap_ocall_ns;  // ocall_aks_read_flat + ocall_aks_write_flat (swap phase I/O)

// Timer state enum
enum TimerState {
    TIMER_OUT_ENCLAVE,  // Application code running (default)
    TIMER_IN_ENCLAVE    // Enclave code running
};

// Global timer state
extern std::atomic<TimerState> g_current_timer_state;
extern std::atomic<uint64_t> g_last_transition_ns;

// Session timing
extern std::chrono::high_resolution_clock::time_point g_session_start;

// Timer management functions
void reset_performance_timers();
uint64_t get_in_enclave_time_ns();
uint64_t get_out_enclave_time_ns();
uint64_t get_total_time_ns();
uint64_t get_sort_time_ns();
uint64_t get_shuffle_time_ns();
uint64_t get_fileio_time_ns();
uint64_t get_batch_total_time_ns();

// Timer state transition functions
void start_in_enclave_timer();   // Called when entering enclave
void stop_in_enclave_timer();    // Called when exiting enclave
void finalize_timers();          // Called at end to account for any remaining time

// Helper function to get current time in nanoseconds
inline uint64_t get_current_time_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

// Helper class for scoped timing
class ScopedTimer {
public:
    ScopedTimer(std::atomic<uint64_t>& counter)
        : counter_(counter), start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        counter_.fetch_add(duration_ns, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t>& counter_;
    std::chrono::high_resolution_clock::time_point start_;
};

#endif // PERFORMANCE_TIMER_H