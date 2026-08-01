#ifndef COUNTED_ECALLS_H
#define COUNTED_ECALLS_H

/*
 * This header provides counted and timed wrappers for all ecalls.
 *
 * After batching optimization, we've reduced from 40+ ecalls to just 4 core ecalls:
 * 1. encrypt_entry - For file I/O and debug
 * 2. decrypt_entry - For file I/O and debug
 * 3. obtain_output_size - Get output size
 * 4. batch_dispatcher - Handles all 36+ batched operations
 *
 * Sort/shuffle ecalls (heap_sort, k_way_merge_*, k_way_shuffle_*, waksman) are
 * also wrapped here for uniform counting and timing.
 *
 * IMPORTANT: Always include this header instead of Enclave_u.h when making ecalls.
 * This ensures all ecalls are properly counted and timed for performance monitoring.
 *
 * DESIGN: All timing is measured from the UNTRUSTED side only. No ocalls are added
 * inside the enclave for timing purposes — that would add expensive SGX transitions.
 */

#include "Enclave_u.h"
#include "../batch/ecall_wrapper.h"
#include "../batch/performance_timer.h"

// ============================================================================
// Core ecall wrappers
// ============================================================================

inline sgx_status_t counted_ecall_encrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_encrypt_entry(eid, retval, entry);

    g_fileio_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_decrypt_entry(sgx_enclave_id_t eid, crypto_status_t* retval, entry_t* entry) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_decrypt_entry(eid, retval, entry);

    g_fileio_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_obtain_output_size(sgx_enclave_id_t eid, int32_t* retval, const entry_t* entry) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_obtain_output_size(eid, retval, entry);

    g_fileio_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_batch_dispatcher(sgx_enclave_id_t eid, entry_t* data_array, size_t data_count,
                                                   void* ops_array, size_t ops_count, size_t ops_size, int32_t op_type) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_batch_dispatcher(eid, data_array, data_count, ops_array, ops_count, ops_size, op_type);

    g_batch_total_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// ============================================================================
// Shuffle ecall wrappers
// ============================================================================

inline sgx_status_t counted_ecall_k_way_shuffle_decompose(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* input, size_t n) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_k_way_shuffle_decompose(eid, retval, input, n);

    g_shuffle_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_shuffle_reconstruct(sgx_enclave_id_t eid, sgx_status_t* retval, size_t n) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_k_way_shuffle_reconstruct(eid, retval, n);

    g_shuffle_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_oblivious_2way_waksman(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* data, size_t n) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_oblivious_2way_waksman(eid, retval, data, n);

    g_shuffle_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// ============================================================================
// Sort ecall wrappers
// ============================================================================

inline sgx_status_t counted_ecall_heap_sort(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* array, size_t size, int comparator_type) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_heap_sort(eid, retval, array, size, comparator_type);

    g_sort_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_merge_init(sgx_enclave_id_t eid, sgx_status_t* retval, size_t k, int comparator_type) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_k_way_merge_init(eid, retval, k, comparator_type);

    g_sort_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_merge_process(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* output, size_t output_capacity, size_t* output_produced, int* merge_complete) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_k_way_merge_process(eid, retval, output, output_capacity, output_produced, merge_complete);

    g_sort_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_k_way_merge_cleanup(sgx_enclave_id_t eid, sgx_status_t* retval) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_k_way_merge_cleanup(eid, retval);

    g_sort_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// ============================================================================
// AKS distribute ecall wrapper
// ============================================================================

inline sgx_status_t counted_ecall_aks_distribute_large(sgx_enclave_id_t eid, sgx_status_t* retval, size_t n) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_aks_distribute_large(eid, retval, n);

    g_sort_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

inline sgx_status_t counted_ecall_distribute_small(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* data, size_t n) {
    start_in_enclave_timer();
    uint64_t t0 = get_current_time_ns();

    sgx_status_t status = ecall_distribute_small(eid, retval, data, n);

    g_sort_time_ns.fetch_add(get_current_time_ns() - t0, std::memory_order_relaxed);
    stop_in_enclave_timer();

    if (status == SGX_SUCCESS) {
        g_ecall_count.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
}

// For convenience, provide a macro that can be used for any ecall (no timing)
#define COUNTED_ECALL(func_name, ...) \
    ({ \
        sgx_status_t _status = func_name(__VA_ARGS__); \
        if (_status == SGX_SUCCESS) { \
            g_ecall_count.fetch_add(1, std::memory_order_relaxed); \
        } \
        _status; \
    })

#endif // COUNTED_ECALLS_H
