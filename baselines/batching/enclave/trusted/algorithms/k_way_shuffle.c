/**
 * K-way Shuffle Implementation for Large Vectors
 * 
 * Implements k-way decomposition and reconstruction for shuffling
 * vectors larger than MAX_BATCH_SIZE using recursive structure.
 * Uses buffered I/O similar to k-way merge for efficiency.
 */

#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "../crypto/crypto_helpers.h"
#include "../../common/constants.h"
#include "../../common/debug_util.h"
#include "../Enclave_t.h"
#include <string.h>
#include <stdint.h>

// The shuffle's I/O buffer size is fixed regardless of MAX_BATCH_SIZE.
// constants.h defines MERGE_BUFFER_SIZE = MAX_BATCH_SIZE / MERGE_SORT_K, which
// would make g_shuffle_state's static arrays grow with batch size and exhaust EPC.
#undef MERGE_BUFFER_SIZE
#define MERGE_BUFFER_SIZE 256


// Forward declaration of waksman_recursive from oblivious_waksman.c
typedef struct {
    uint64_t shuffle_nonce;
} ShuffleRNG;

// Function to initialize RNG (from oblivious_waksman.c)
static void init_shuffle_rng_local(ShuffleRNG* rng) {
    // Ensure AES key is initialized
    if (!aes_key_initialized) {
        init_aes_key();
    }
    // Get unique nonce from global counter
    rng->shuffle_nonce = get_next_nonce();
}

// Forward declaration from oblivious_waksman.c
void waksman_recursive(entry_t* data, size_t start, size_t m, size_t n, 
                       uint32_t level, ShuffleRNG* rng);

/**
 * Shuffle state for buffered operations
 */
typedef struct {
    // Output buffers for decompose (k buffers, one per group)
    entry_t output_buffers[MERGE_SORT_K][MERGE_BUFFER_SIZE];
    size_t output_buffer_sizes[MERGE_SORT_K];  // Current fill level
    
    // Input buffers for reconstruct (k buffers, one per group)
    entry_t input_buffers[MERGE_SORT_K][MERGE_BUFFER_SIZE];
    size_t input_buffer_sizes[MERGE_SORT_K];   // Valid entries in buffer
    size_t input_buffer_pos[MERGE_SORT_K];     // Current read position
    
    // Track which round we're on for each group
    size_t group_rounds_processed[MERGE_SORT_K];
    
    // Common state
    size_t total_rounds;                       // Total rounds to process
    size_t current_round;                      // Current round being processed
    
    int decompose_initialized;                 // Decompose state initialized
    int reconstruct_initialized;               // Reconstruct state initialized
} ShuffleState;

// Global state (persists across ecalls)
static ShuffleState g_shuffle_state = {0};

/**
 * Initialize decompose state
 */
static void init_decompose_state(size_t n) {
    memset(&g_shuffle_state, 0, sizeof(ShuffleState));
    g_shuffle_state.total_rounds = n / MERGE_SORT_K;
    g_shuffle_state.current_round = 0;
    g_shuffle_state.decompose_initialized = 1;
    
    DEBUG_INFO("Decompose state initialized: n=%zu, rounds=%zu", n, g_shuffle_state.total_rounds);
}

/**
 * Initialize reconstruct state
 */
static void init_reconstruct_state(size_t n) {
    memset(&g_shuffle_state, 0, sizeof(ShuffleState));
    g_shuffle_state.total_rounds = n / MERGE_SORT_K;
    g_shuffle_state.current_round = 0;
    g_shuffle_state.reconstruct_initialized = 1;
    
    // Initialize all input buffers as empty (need refill)
    for (size_t i = 0; i < MERGE_SORT_K; i++) {
        g_shuffle_state.input_buffer_sizes[i] = 0;
        g_shuffle_state.input_buffer_pos[i] = 0;
        g_shuffle_state.group_rounds_processed[i] = 0;
    }
    
    DEBUG_INFO("Reconstruct state initialized: n=%zu, rounds=%zu", n, g_shuffle_state.total_rounds);
}

/**
 * Flush output buffer for a specific group
 */
static sgx_status_t flush_output_buffer(int group_idx) {
    if (g_shuffle_state.output_buffer_sizes[group_idx] == 0) {
        return SGX_SUCCESS;  // Nothing to flush
    }
    
    // Encrypt all entries in the buffer
    for (size_t i = 0; i < g_shuffle_state.output_buffer_sizes[group_idx]; i++) {
        crypto_status_t status = aes_encrypt_entry(&g_shuffle_state.output_buffers[group_idx][i]);
        if (status != CRYPTO_SUCCESS && status != CRYPTO_ALREADY_ENCRYPTED) {
            DEBUG_ERROR("Failed to encrypt entry before flush");
            return SGX_ERROR_UNEXPECTED;
        }
    }
    
    // Ocall to flush this group's buffer
    ocall_flush_to_group(group_idx, 
                         g_shuffle_state.output_buffers[group_idx],
                         g_shuffle_state.output_buffer_sizes[group_idx]);
    
    // Reset buffer
    g_shuffle_state.output_buffer_sizes[group_idx] = 0;
    
    return SGX_SUCCESS;
}

/**
 * Refill input buffer for a specific group
 */
static sgx_status_t refill_input_buffer(int group_idx) {
    size_t actual_filled = 0;
    
    // Request buffer refill via ocall
    ocall_refill_from_group(group_idx,
                            g_shuffle_state.input_buffers[group_idx],
                            MERGE_BUFFER_SIZE,
                            &actual_filled);
    
    if (actual_filled > 0) {
        // Decrypt all entries in the refilled buffer
        for (size_t i = 0; i < actual_filled; i++) {
            if (g_shuffle_state.input_buffers[group_idx][i].is_encrypted) {
                crypto_status_t status = aes_decrypt_entry(&g_shuffle_state.input_buffers[group_idx][i]);
                if (status != CRYPTO_SUCCESS) {
                    DEBUG_ERROR("Failed to decrypt entry from group %d", group_idx);
                    return SGX_ERROR_UNEXPECTED;
                }
            }
        }
        
        g_shuffle_state.input_buffer_sizes[group_idx] = actual_filled;
        g_shuffle_state.input_buffer_pos[group_idx] = 0;
    } else {
        g_shuffle_state.input_buffer_sizes[group_idx] = 0;
    }
    
    return SGX_SUCCESS;
}

/**
 * K-way shuffle decomposition
 * Takes n elements and distributes them into k groups
 */
sgx_status_t ecall_k_way_shuffle_decompose(entry_t* input, size_t n) {
    const size_t k = MERGE_SORT_K;
    
    DEBUG_INFO("K-way decompose: n=%zu, k=%zu", n, k);
    
    // Verify n is multiple of k
    if (n % k != 0) {
        DEBUG_ERROR("n=%zu is not multiple of k=%zu", n, k);
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize state
    init_decompose_state(n);
    
    // Decrypt all input entries first
    for (size_t i = 0; i < n; i++) {
        if (input[i].is_encrypted) {
            crypto_status_t status = aes_decrypt_entry(&input[i]);
            if (status != CRYPTO_SUCCESS) {
                // Re-encrypt already processed entries
                for (size_t j = 0; j < i; j++) {
                    aes_encrypt_entry(&input[j]);
                }
                return SGX_ERROR_UNEXPECTED;
            }
        }
    }
    
    // Initialize RNG for shuffling
    ShuffleRNG rng;
    init_shuffle_rng_local(&rng);
    
    size_t rounds = n / k;
    entry_t temp[MERGE_SORT_K];
    
    // Process all rounds
    for (size_t round = 0; round < rounds; round++) {
        // Copy k elements to temp buffer
        for (size_t i = 0; i < k; i++) {
            temp[i] = input[round * k + i];
        }
        
        // Shuffle these k elements
        waksman_recursive(temp, 0, 1, k, (uint32_t)round, &rng);
        
        // Send element i to group i's output buffer
        for (size_t i = 0; i < k; i++) {
            // Add to output buffer
            g_shuffle_state.output_buffers[i][g_shuffle_state.output_buffer_sizes[i]] = temp[i];
            g_shuffle_state.output_buffer_sizes[i]++;
            
            // Flush if buffer is full
            if (g_shuffle_state.output_buffer_sizes[i] >= MERGE_BUFFER_SIZE) {
                sgx_status_t status = flush_output_buffer((int)i);
                if (status != SGX_SUCCESS) {
                    return status;
                }
            }
        }
    }

    // Flush any remaining data in output buffers
    for (size_t i = 0; i < k; i++) {
        sgx_status_t status = flush_output_buffer((int)i);
        if (status != SGX_SUCCESS) {
            return status;
        }
    }
    
    DEBUG_INFO("K-way decompose complete: processed %zu rounds", rounds);
    g_shuffle_state.decompose_initialized = 0;
    return SGX_SUCCESS;
}

/**
 * K-way shuffle reconstruction
 * Reconstructs shuffled output from k groups
 */
sgx_status_t ecall_k_way_shuffle_reconstruct(size_t n) {
    const size_t k = MERGE_SORT_K;
    
    DEBUG_INFO("K-way reconstruct: n=%zu, k=%zu", n, k);
    
    if (n % k != 0) {
        DEBUG_ERROR("n=%zu is not multiple of k=%zu", n, k);
        return SGX_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize state
    init_reconstruct_state(n);
    
    // Initialize RNG
    ShuffleRNG rng;
    init_shuffle_rng_local(&rng);
    
    size_t rounds = n / k;
    entry_t temp[MERGE_SORT_K];
    entry_t output_buffer[MERGE_BUFFER_SIZE];
    size_t output_buffer_size = 0;
    
    for (size_t round = 0; round < rounds; round++) {
        // Collect one element from each group
        for (size_t i = 0; i < k; i++) {
            // Check if we need to refill this group's buffer
            if (g_shuffle_state.input_buffer_pos[i] >= g_shuffle_state.input_buffer_sizes[i]) {
                sgx_status_t status = refill_input_buffer((int)i);
                if (status != SGX_SUCCESS) {
                    return status;
                }
                
                // Check if refill succeeded
                if (g_shuffle_state.input_buffer_sizes[i] == 0) {
                    DEBUG_ERROR("Group %zu exhausted at round %zu", i, round);
                    return SGX_ERROR_UNEXPECTED;
                }
            }
            
            // Get next element from group i
            temp[i] = g_shuffle_state.input_buffers[i][g_shuffle_state.input_buffer_pos[i]];
            g_shuffle_state.input_buffer_pos[i]++;
        }
        
        // Shuffle these k elements
        waksman_recursive(temp, 0, 1, k, (uint32_t)(round + 100000), &rng);
        
        // Add to output buffer
        for (size_t i = 0; i < k; i++) {
            // Encrypt before adding to output
            crypto_status_t status = aes_encrypt_entry(&temp[i]);
            if (status != CRYPTO_SUCCESS && status != CRYPTO_ALREADY_ENCRYPTED) {
                DEBUG_ERROR("Failed to encrypt entry for output");
                return SGX_ERROR_UNEXPECTED;
            }
            
            output_buffer[output_buffer_size++] = temp[i];
            
            // Flush output buffer if full
            if (output_buffer_size >= MERGE_BUFFER_SIZE) {
                ocall_flush_output(output_buffer, output_buffer_size);
                output_buffer_size = 0;
            }
        }
    }
    
    // Flush any remaining output
    if (output_buffer_size > 0) {
        ocall_flush_output(output_buffer, output_buffer_size);
    }
    
    DEBUG_INFO("K-way reconstruct complete: processed %zu rounds", rounds);
    g_shuffle_state.reconstruct_initialized = 0;
    return SGX_SUCCESS;
}