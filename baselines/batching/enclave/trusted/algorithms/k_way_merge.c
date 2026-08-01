#include "min_heap.h"
#include "../crypto/aes_crypto.h"
#include "../crypto/crypto_helpers.h"
#include "../../common/batch_types.h"
#include "../../common/constants.h"
#include "../Enclave_t.h"
#include <stdlib.h>
#include <string.h>

// External function to get merge comparator
extern comparator_func_t get_merge_comparator(OpEcall type);

/**
 * Merge State Structure
 * Maintains state across ecalls for k-way merge
 */
typedef struct {
    entry_t* buffers[MERGE_SORT_K];      // Input buffers
    size_t buffer_sizes[MERGE_SORT_K];   // Valid entries in each buffer
    size_t buffer_pos[MERGE_SORT_K];     // Current position in each buffer
    int run_exhausted[MERGE_SORT_K];     // Whether run is exhausted
    size_t k;                             // Number of runs
    
    MinHeap heap;                         // Min-heap for k-way merge
    comparator_func_t compare;            // Comparison function
    
    int initialized;                      // State initialized flag
} MergeState;

// Global state (persists across ecalls)
static MergeState g_merge_state = {0};

/**
 * Initialize k-way merge state
 */
sgx_status_t ecall_k_way_merge_init(size_t k, int comparator_type) {
    // Clean up any previous state
    if (g_merge_state.initialized) {
        ecall_k_way_merge_cleanup();
    }
    
    if (k == 0 || k > MERGE_SORT_K) {
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize state
    g_merge_state.k = k;
    g_merge_state.compare = get_merge_comparator((OpEcall)comparator_type);
    
    // Initialize heap
    minheap_init(&g_merge_state.heap, k, g_merge_state.compare);
    
    // Allocate buffers
    for (size_t i = 0; i < k; i++) {
        g_merge_state.buffers[i] = (entry_t*)malloc(MERGE_BUFFER_SIZE * sizeof(entry_t));
        if (!g_merge_state.buffers[i]) {
            // Clean up allocated buffers
            for (size_t j = 0; j < i; j++) {
                free(g_merge_state.buffers[j]);
                g_merge_state.buffers[j] = NULL;
            }
            heap_destroy(&g_merge_state.heap);
            return SGX_ERROR_OUT_OF_MEMORY;
        }
        
        g_merge_state.buffer_sizes[i] = 0;
        g_merge_state.buffer_pos[i] = 0;
        g_merge_state.run_exhausted[i] = 0;
    }
    
    // Initial buffer fill - request from app via ocall
    for (size_t i = 0; i < k; i++) {
        size_t actual_filled = 0;
        
        // Request initial buffer fill
        ocall_refill_buffer((int)i, g_merge_state.buffers[i], 
                           MERGE_BUFFER_SIZE, &actual_filled);
        
        if (actual_filled > 0) {
            // Decrypt buffer
            for (size_t j = 0; j < actual_filled; j++) {
                if (g_merge_state.buffers[i][j].is_encrypted) {
                    crypto_status_t status = aes_decrypt_entry(&g_merge_state.buffers[i][j]);
                    if (status != CRYPTO_SUCCESS) {
                        ecall_k_way_merge_cleanup();
                        return SGX_ERROR_UNEXPECTED;
                    }
                }
            }
            
            g_merge_state.buffer_sizes[i] = actual_filled;
            g_merge_state.buffer_pos[i] = 0;
            
            // Add first element to heap
            heap_push(&g_merge_state.heap, &g_merge_state.buffers[i][0], i);
            g_merge_state.buffer_pos[i] = 1;
        } else {
            g_merge_state.run_exhausted[i] = 1;
        }
    }
    
    g_merge_state.initialized = 1;
    return SGX_SUCCESS;
}

/**
 * Process k-way merge
 * Merges entries from k runs and outputs sorted result
 */
sgx_status_t ecall_k_way_merge_process(entry_t* output, size_t output_capacity,
                                       size_t* output_produced, int* merge_complete) {
    if (!g_merge_state.initialized) {
        return SGX_ERROR_INVALID_STATE;
    }
    
    if (!output || !output_produced || !merge_complete) {
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    *output_produced = 0;
    *merge_complete = 0;
    
    while (*output_produced < output_capacity) {
        entry_t min_entry;
        size_t run_idx;
        
        // Get minimum from heap
        if (!heap_pop(&g_merge_state.heap, &min_entry, &run_idx)) {
            // All runs exhausted
            *merge_complete = 1;
            break;
        }
        
        // Encrypt and output the minimum
        crypto_status_t status = aes_encrypt_entry(&min_entry);
        if (status != CRYPTO_SUCCESS) {
            return SGX_ERROR_UNEXPECTED;
        }
        output[(*output_produced)++] = min_entry;
        
        // Get next entry from same run
        if (g_merge_state.buffer_pos[run_idx] >= g_merge_state.buffer_sizes[run_idx]) {
            // Buffer exhausted, need refill if run not exhausted
            if (!g_merge_state.run_exhausted[run_idx]) {
                size_t actual_filled = 0;
                
                // Request buffer refill via ocall
                ocall_refill_buffer((int)run_idx, g_merge_state.buffers[run_idx],
                                   MERGE_BUFFER_SIZE, &actual_filled);
                
                if (actual_filled > 0) {
                    // Decrypt refilled buffer
                    for (size_t i = 0; i < actual_filled; i++) {
                        if (g_merge_state.buffers[run_idx][i].is_encrypted) {
                            crypto_status_t decrypt_status = 
                                aes_decrypt_entry(&g_merge_state.buffers[run_idx][i]);
                            if (decrypt_status != CRYPTO_SUCCESS) {
                                return SGX_ERROR_UNEXPECTED;
                            }
                        }
                    }
                    
                    g_merge_state.buffer_sizes[run_idx] = actual_filled;
                    g_merge_state.buffer_pos[run_idx] = 0;
                } else {
                    // Run exhausted
                    g_merge_state.run_exhausted[run_idx] = 1;
                    continue;
                }
            } else {
                // Run already exhausted
                continue;
            }
        }
        
        // Add next entry from run to heap
        if (g_merge_state.buffer_pos[run_idx] < g_merge_state.buffer_sizes[run_idx]) {
            heap_push(&g_merge_state.heap,
                     &g_merge_state.buffers[run_idx][g_merge_state.buffer_pos[run_idx]],
                     run_idx);
            g_merge_state.buffer_pos[run_idx]++;
        }
    }
    
    return SGX_SUCCESS;
}

/**
 * Clean up k-way merge state
 */
sgx_status_t ecall_k_way_merge_cleanup(void) {
    // Free buffers
    for (size_t i = 0; i < MERGE_SORT_K; i++) {
        if (g_merge_state.buffers[i]) {
            // Clear sensitive data before freeing
            memset(g_merge_state.buffers[i], 0, MERGE_BUFFER_SIZE * sizeof(entry_t));
            free(g_merge_state.buffers[i]);
            g_merge_state.buffers[i] = NULL;
        }
    }
    
    // Destroy heap
    heap_destroy(&g_merge_state.heap);
    
    // Reset state
    memset(&g_merge_state, 0, sizeof(MergeState));
    
    return SGX_SUCCESS;
}