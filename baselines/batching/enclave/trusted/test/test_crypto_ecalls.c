#include "../Enclave_t.h"
#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "../core.h"
#include <string.h>

/**
 * Test ecalls for measuring crypto and operation overhead separately
 */

// Decrypt entries only (no re-encryption)
void ecall_test_decrypt_only(entry_t* entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_encrypted) {
            aes_decrypt_entry(&entries[i]);
        }
    }
}

// Encrypt entries only 
void ecall_test_encrypt_only(entry_t* entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].is_encrypted) {
            aes_encrypt_entry(&entries[i]);
        }
    }
}

// Decrypt, do comparisons, but don't re-encrypt
void ecall_test_decrypt_and_compare(entry_t* entries, size_t count) {
    // Decrypt all
    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_encrypted) {
            aes_decrypt_entry(&entries[i]);
        }
    }
    
    // Do comparisons (similar to batch dispatcher)
    for (size_t i = 0; i < count - 1; i += 2) {
        comparator_join_attr_op(&entries[i], &entries[i + 1]);
    }
    // Leave decrypted - no re-encryption
}

// Just do comparisons on plaintext entries
void ecall_test_compare_only(entry_t* entries, size_t count) {
    for (size_t i = 0; i < count - 1; i += 2) {
        comparator_join_attr_op(&entries[i], &entries[i + 1]);
    }
}

// Full cycle: decrypt, compare, re-encrypt (like batch dispatcher)
void ecall_test_full_cycle(entry_t* entries, size_t count) {
    // Remember encryption state
    uint8_t was_encrypted[2048];
    size_t actual_count = count > 2048 ? 2048 : count;
    
    // Decrypt all
    for (size_t i = 0; i < actual_count; i++) {
        was_encrypted[i] = entries[i].is_encrypted;
        if (entries[i].is_encrypted) {
            aes_decrypt_entry(&entries[i]);
        }
    }
    
    // Do comparisons
    for (size_t i = 0; i < actual_count - 1; i += 2) {
        comparator_join_attr_op(&entries[i], &entries[i + 1]);
    }
    
    // Re-encrypt 
    for (size_t i = 0; i < actual_count; i++) {
        if (was_encrypted[i]) {
            aes_encrypt_entry(&entries[i]);
        }
    }
}

// Test with varying percentages of encrypted entries
void ecall_test_mixed_encryption(entry_t* entries, size_t count, int32_t encrypt_percent) {
    // Decrypt based on percentage
    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_encrypted && (int32_t)(i * 100 / count) < encrypt_percent) {
            aes_decrypt_entry(&entries[i]);
        }
    }
    
    // Do comparisons
    for (size_t i = 0; i < count - 1; i += 2) {
        comparator_join_attr_op(&entries[i], &entries[i + 1]);
    }
    
    // Re-encrypt what was decrypted
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].is_encrypted && (int32_t)(i * 100 / count) < encrypt_percent) {
            aes_encrypt_entry(&entries[i]);
        }
    }
}