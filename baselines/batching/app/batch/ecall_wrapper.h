#ifndef ECALL_WRAPPER_H
#define ECALL_WRAPPER_H

#include "sgx_urts.h"
#include "Enclave_u.h"
#include <atomic>

// Global ecall counter
extern std::atomic<size_t> g_ecall_count;

// Global ocall counter
extern std::atomic<size_t> g_ocall_count;

// Counter management functions
void reset_ecall_count();
size_t get_ecall_count();

void reset_ocall_count();
size_t get_ocall_count();

// Wrapper macro for ecalls that automatically increments counter
#define COUNTED_ECALL(func_name, ...) \
    ({ \
        sgx_status_t _status = func_name(__VA_ARGS__); \
        if (_status == SGX_SUCCESS) { \
            g_ecall_count.fetch_add(1, std::memory_order_relaxed); \
        } \
        _status; \
    })

// Note: All individual ecall wrappers have been moved to counted_ecalls.h
// After batching optimization, only 4 ecalls remain:
// 1. encrypt_entry
// 2. decrypt_entry  
// 3. obtain_output_size
// 4. batch_dispatcher
// These are now defined in counted_ecalls.h for better organization

#endif // ECALL_WRAPPER_H