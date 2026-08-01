#include "counted_ocalls.h"
#include "../batch/ecall_wrapper.h"
#include "../batch/performance_timer.h"
#include "../algorithms/shuffle_manager.h"
#include "../algorithms/merge_sort_manager.h"
#include <time.h>

/*
 * Centralized ocall implementations with automatic counting and timing.
 * These functions are called by the enclave through ocalls.
 * They increment the ocall counter, track out-of-enclave time,
 * and then delegate to the appropriate handler function in the manager classes.
 */

extern "C" {

// Timestamp ocall: returns current monotonic time in nanoseconds.
// Trivial body so almost all measured time is the ocall transition itself.
uint64_t ocall_get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Shuffle Manager ocalls

void ocall_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
    stop_in_enclave_timer();  // Exiting enclave into application

    g_ocall_count.fetch_add(1, std::memory_order_relaxed);  // Count the ocall
    ShuffleManager::handle_flush_to_group(group_idx, buffer, buffer_size);

    start_in_enclave_timer();  // Returning to enclave
}

void ocall_refill_from_group(int group_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled) {
    stop_in_enclave_timer();  // Exiting enclave into application

    g_ocall_count.fetch_add(1, std::memory_order_relaxed);  // Count the ocall
    ShuffleManager::handle_refill_from_group(group_idx, buffer, buffer_size, actual_filled);

    start_in_enclave_timer();  // Returning to enclave
}

void ocall_flush_output(entry_t* buffer, size_t buffer_size) {
    stop_in_enclave_timer();  // Exiting enclave into application

    g_ocall_count.fetch_add(1, std::memory_order_relaxed);  // Count the ocall
    ShuffleManager::handle_flush_output(buffer, buffer_size);

    start_in_enclave_timer();  // Returning to enclave
}

// Merge Sort Manager ocalls

void ocall_refill_buffer(int buffer_idx, entry_t* buffer, size_t buffer_size, size_t* actual_filled) {
    stop_in_enclave_timer();  // Exiting enclave into application

    g_ocall_count.fetch_add(1, std::memory_order_relaxed);  // Count the ocall
    MergeSortManager::handle_refill_buffer(buffer_idx, buffer, buffer_size, actual_filled);

    start_in_enclave_timer();  // Returning to enclave
}

void ocall_aks_read_scan_flat(size_t offset, entry_t* buf, size_t count) {
    stop_in_enclave_timer();
    g_ocall_count.fetch_add(1, std::memory_order_relaxed);
    uint64_t t0 = get_current_time_ns();
    DistributeManager::handle_read_flat(offset, buf, count);
    g_aks_scan_ocall_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    start_in_enclave_timer();
}

void ocall_aks_read_flat(size_t offset, entry_t* buf, size_t count) {
    stop_in_enclave_timer();
    g_ocall_count.fetch_add(1, std::memory_order_relaxed);
    uint64_t t0 = get_current_time_ns();
    DistributeManager::handle_read_flat(offset, buf, count);
    g_aks_swap_ocall_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    start_in_enclave_timer();
}

void ocall_aks_write_flat(size_t offset, entry_t* buf, size_t count) {
    stop_in_enclave_timer();
    g_ocall_count.fetch_add(1, std::memory_order_relaxed);
    uint64_t t0 = get_current_time_ns();
    DistributeManager::handle_write_flat(offset, buf, count);
    g_aks_swap_ocall_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    start_in_enclave_timer();
}

} // extern "C"