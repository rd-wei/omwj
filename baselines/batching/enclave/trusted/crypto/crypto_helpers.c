#include "crypto_helpers.h"

/**
 * Helper for single entry operations
 * Handles the decrypt-operate-encrypt pattern
 */
void apply_to_decrypted_entry(entry_t* entry, entry_operation_t operation) {
    if (!entry || !operation) return;
    
    uint8_t was_encrypted = entry->is_encrypted;
    
    // Decrypt if needed
    if (was_encrypted) {
        crypto_status_t status = aes_decrypt_entry(entry);
        if (status != CRYPTO_SUCCESS) {
            // Failed to decrypt, cannot proceed
            return;
        }
    }
    
    // Apply the operation
    operation(entry);
    
    // Re-encrypt if it was encrypted
    if (was_encrypted) {
        aes_encrypt_entry(entry);
    }
}

/**
 * Helper for pair operations
 * Handles the decrypt-operate-encrypt pattern for two entries
 */
void apply_to_decrypted_pair(entry_t* e1, entry_t* e2, pair_operation_t operation) {
    if (!e1 || !e2 || !operation) return;
    
    uint8_t was_encrypted1 = e1->is_encrypted;
    uint8_t was_encrypted2 = e2->is_encrypted;
    
    // Decrypt first entry if needed
    if (was_encrypted1) {
        crypto_status_t status = aes_decrypt_entry(e1);
        if (status != CRYPTO_SUCCESS) {
            return;
        }
    }
    
    // Decrypt second entry if needed
    if (was_encrypted2) {
        crypto_status_t status = aes_decrypt_entry(e2);
        if (status != CRYPTO_SUCCESS) {
            // Re-encrypt first entry before returning
            if (was_encrypted1) {
                aes_encrypt_entry(e1);
            }
            return;
        }
    }
    
    // Apply the operation
    operation(e1, e2);
    
    // Re-encrypt both if they were encrypted
    if (was_encrypted1) {
        aes_encrypt_entry(e1);
    }
    if (was_encrypted2) {
        aes_encrypt_entry(e2);
    }
}