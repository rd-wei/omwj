#include "Enclave_t.h"
#include "enclave_types.h"
#include "crypto/entry_crypto.h"
#include "crypto/aes_crypto.h"
#include "core.h"
#include "algorithms/aks_distribute.h"

// ENCLAVE_BUILD is already defined in Makefile
#include "secure_key.h"

/**
 * Essential Ecall Implementations
 * Only 4 ecalls remain after batching optimization:
 * 1. encrypt_entry - For file I/O and debug
 * 2. decrypt_entry - For file I/O and debug  
 * 3. obtain_output_size - Get output size from last entry
 * 4. batch_dispatcher - Handles all 36+ batched operations
 */

crypto_status_t ecall_encrypt_entry(entry_t* entry) {
    // Use AES-CTR encryption with secure enclave key
    return aes_encrypt_entry(entry);
}

crypto_status_t ecall_decrypt_entry(entry_t* entry) {
    // Use AES-CTR decryption with secure enclave key
    return aes_decrypt_entry(entry);
}

void ecall_obtain_output_size(int32_t* retval, const entry_t* entry) {
    *retval = obtain_output_size(entry);
}

// Note: ecall_batch_dispatcher is implemented in enclave/batch/batch_dispatcher.c

// Note: ocall_debug_print is implemented in untrusted code (app side), not here

sgx_status_t ecall_distribute_small(entry_t* data, size_t n) {
    aks_distribute(data, n);
    return SGX_SUCCESS;
}

sgx_status_t ecall_aks_distribute_large(size_t n) {
    aks_distribute_large(n);
    return SGX_SUCCESS;
}

void ecall_aes_benchmark(entry_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) aes_decrypt_entry(&data[i]);
    for (size_t i = 0; i < n; i++) aes_encrypt_entry(&data[i]);
}