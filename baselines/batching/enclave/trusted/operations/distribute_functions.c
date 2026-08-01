#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "../../common/debug_util.h"
#include <stdint.h>

/**
 * Distribute utility functions
 * This file contains utility functions for the distribute-expand phase
 * Transform and window functions have been moved to their respective files
 */

/**
 * Get output size from last entry
 * Returns dst_idx + final_mult
 */
int32_t obtain_output_size(const entry_t* last_entry) {
    // Check if encrypted and decrypt if needed
    if (last_entry->is_encrypted) {
        entry_t decrypted = *last_entry;
        aes_decrypt_entry(&decrypted);
        return decrypted.dst_idx + decrypted.final_mult;
    } else {
        return last_entry->dst_idx + last_entry->final_mult;
    }
}