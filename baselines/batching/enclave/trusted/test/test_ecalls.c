#include "../Enclave_t.h"
#include "../enclave_types.h"

/**
 * Test ecalls for measuring SGX overhead components
 * These are kept separate from main code to avoid any interference
 */

// Pure no-op - measures SGX transition overhead only
void ecall_test_noop(void) {
    // Absolutely nothing - pure transition cost
}

// No-op with small data - measures transition + small marshalling
void ecall_test_noop_small(void* data, size_t size) {
    (void)data;  // Suppress unused parameter warning
    (void)size;
    // Do nothing with the data
}

// No-op with in/out data - measures bidirectional marshalling
void ecall_test_noop_inout(void* data, size_t size) {
    (void)data;
    (void)size;
    // Data is marked in/out so it gets copied back
    // But we don't modify it
}

// No-op with entry array - similar to real workload data structure
void ecall_test_noop_entries(entry_t* entries, size_t count) {
    (void)entries;
    (void)count;
    // Do nothing with entries
}

// Simple computation (sum) on data - measures computation overhead
int32_t ecall_test_sum_array(int32_t* data, size_t size) {
    int32_t sum = 0;
    size_t count = size / sizeof(int32_t);
    for (size_t i = 0; i < count; i++) {
        sum += data[i];
    }
    return sum;
}

// Simple entry processing - touch each entry once
void ecall_test_touch_entries(entry_t* entries, size_t count) {
    // Just read one field from each entry to ensure memory access
    volatile int32_t dummy = 0;
    for (size_t i = 0; i < count; i++) {
        dummy += entries[i].join_attr;
    }
    (void)dummy;  // Suppress unused warning
}

// Entry processing with simple operation (increment join_attr)
void ecall_test_increment_entries(entry_t* entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        entries[i].join_attr++;
    }
}

// Stub: no ocall_get_timestamp_ns available in baseline; returns 0
void ecall_benchmark_ocall_overhead(size_t iterations, uint64_t* elapsed_ns) {
    (void)iterations;
    if (elapsed_ns) *elapsed_ns = 0;
}