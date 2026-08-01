#include "performance_timer.h"

// Global performance timers (nanoseconds)
std::atomic<uint64_t> g_in_enclave_time_ns(0);
std::atomic<uint64_t> g_out_enclave_time_ns(0);

// Per-category ecall timers
std::atomic<uint64_t> g_sort_time_ns(0);
std::atomic<uint64_t> g_shuffle_time_ns(0);
std::atomic<uint64_t> g_fileio_time_ns(0);
std::atomic<uint64_t> g_batch_total_time_ns(0);

// AKS distribute ocall timing
std::atomic<uint64_t> g_aks_scan_ocall_ns(0);
std::atomic<uint64_t> g_aks_swap_ocall_ns(0);

// Global timer state
std::atomic<TimerState> g_current_timer_state(TIMER_OUT_ENCLAVE);
std::atomic<uint64_t> g_last_transition_ns(0);

// Session start time
std::chrono::high_resolution_clock::time_point g_session_start;

uint64_t get_sort_time_ns()         { return g_sort_time_ns.load(std::memory_order_relaxed); }
uint64_t get_shuffle_time_ns()      { return g_shuffle_time_ns.load(std::memory_order_relaxed); }
uint64_t get_fileio_time_ns()       { return g_fileio_time_ns.load(std::memory_order_relaxed); }
uint64_t get_batch_total_time_ns()  { return g_batch_total_time_ns.load(std::memory_order_relaxed); }

void reset_performance_timers() {
    // Reset all counters
    g_in_enclave_time_ns.store(0, std::memory_order_relaxed);
    g_out_enclave_time_ns.store(0, std::memory_order_relaxed);
    g_sort_time_ns.store(0, std::memory_order_relaxed);
    g_shuffle_time_ns.store(0, std::memory_order_relaxed);
    g_fileio_time_ns.store(0, std::memory_order_relaxed);
    g_batch_total_time_ns.store(0, std::memory_order_relaxed);
    g_aks_scan_ocall_ns.store(0, std::memory_order_relaxed);
    g_aks_swap_ocall_ns.store(0, std::memory_order_relaxed);

    // Initialize timer state to out-enclave (application code)
    g_current_timer_state.store(TIMER_OUT_ENCLAVE, std::memory_order_relaxed);

    // Record session start time and set as last transition
    g_session_start = std::chrono::high_resolution_clock::now();
    auto start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        g_session_start.time_since_epoch()).count();
    g_last_transition_ns.store(start_ns, std::memory_order_relaxed);
}

uint64_t get_in_enclave_time_ns() {
    return g_in_enclave_time_ns.load(std::memory_order_relaxed);
}

uint64_t get_out_enclave_time_ns() {
    return g_out_enclave_time_ns.load(std::memory_order_relaxed);
}

uint64_t get_total_time_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now - g_session_start;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

void start_in_enclave_timer() {
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    uint64_t last_ns = g_last_transition_ns.load(std::memory_order_relaxed);

    // Accumulate time since last transition to out-enclave
    if (g_current_timer_state.load(std::memory_order_relaxed) == TIMER_OUT_ENCLAVE) {
        uint64_t duration = now_ns - last_ns;
        g_out_enclave_time_ns.fetch_add(duration, std::memory_order_relaxed);
    }

    // Switch to in-enclave timer
    g_current_timer_state.store(TIMER_IN_ENCLAVE, std::memory_order_relaxed);
    g_last_transition_ns.store(now_ns, std::memory_order_relaxed);
}

void stop_in_enclave_timer() {
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    uint64_t last_ns = g_last_transition_ns.load(std::memory_order_relaxed);

    // Accumulate time since last transition to in-enclave
    if (g_current_timer_state.load(std::memory_order_relaxed) == TIMER_IN_ENCLAVE) {
        uint64_t duration = now_ns - last_ns;
        g_in_enclave_time_ns.fetch_add(duration, std::memory_order_relaxed);
    }

    // Switch to out-enclave timer
    g_current_timer_state.store(TIMER_OUT_ENCLAVE, std::memory_order_relaxed);
    g_last_transition_ns.store(now_ns, std::memory_order_relaxed);
}

void finalize_timers() {
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    uint64_t last_ns = g_last_transition_ns.load(std::memory_order_relaxed);

    // Account for any remaining time in current state
    if (g_current_timer_state.load(std::memory_order_relaxed) == TIMER_IN_ENCLAVE) {
        // Should not happen - we should always be out-enclave at this point
        uint64_t duration = now_ns - last_ns;
        g_in_enclave_time_ns.fetch_add(duration, std::memory_order_relaxed);
    } else {
        // Normal case - accumulate final out-enclave time
        uint64_t duration = now_ns - last_ns;
        g_out_enclave_time_ns.fetch_add(duration, std::memory_order_relaxed);
    }

    // Update last transition to prevent double counting if called multiple times
    g_last_transition_ns.store(now_ns, std::memory_order_relaxed);
}