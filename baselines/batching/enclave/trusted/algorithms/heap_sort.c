#include "min_heap.h"
#include "../crypto/aes_crypto.h"
#include "../crypto/crypto_helpers.h"
#include "../../common/batch_types.h"
#include "../Enclave_t.h"
#include <string.h>

// External function to get merge comparator
extern comparator_func_t get_merge_comparator(OpEcall type);

/**
 * Heap sort ecall implementation
 * Sorts an array of entries in-place using heap sort
 */
sgx_status_t ecall_heap_sort(entry_t* array, size_t size, int comparator_type) {
    if (!array || size == 0) {
        return SGX_SUCCESS;  // Nothing to sort
    }
    
    // Decrypt all entries
    for (size_t i = 0; i < size; i++) {
        if (array[i].is_encrypted) {
            crypto_status_t status = aes_decrypt_entry(&array[i]);
            if (status != CRYPTO_SUCCESS) {
                // Re-encrypt any already decrypted entries before returning
                for (size_t j = 0; j < i; j++) {
                    aes_encrypt_entry(&array[j]);
                }
                return SGX_ERROR_UNEXPECTED;
            }
        }
    }
    
    // Get the appropriate comparator function
    comparator_func_t compare = get_merge_comparator((OpEcall)comparator_type);
    
    // Perform heap sort
    heap_sort(array, size, compare);
    
    // Re-encrypt all entries
    for (size_t i = 0; i < size; i++) {
        crypto_status_t status = aes_encrypt_entry(&array[i]);
        if (status != CRYPTO_SUCCESS) {
            return SGX_ERROR_UNEXPECTED;
        }
    }
    
    return SGX_SUCCESS;
}