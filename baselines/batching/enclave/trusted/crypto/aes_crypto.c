#include "aes_crypto.h"

// ENCLAVE_BUILD is already defined in Makefile
#include "../secure_key.h"

#include <stddef.h>
#include <string.h>
#include <sgx_tcrypto.h>

// Global nonce counter for unique nonce generation
static uint64_t g_nonce_counter = 1;

// AES key derived from secure key (16 bytes for AES-128)
uint8_t aes_key[16] = {0};
int aes_key_initialized = 0;

/**
 * Initialize AES key from secure key
 */
void init_aes_key(void) {
    if (!aes_key_initialized) {
        // Derive AES key from secure key (simple approach for now)
        uint32_t key = SECURE_ENCRYPTION_KEY;
        for (int i = 0; i < 16; i++) {
            aes_key[i] = (key >> ((i % 4) * 8)) & 0xFF;
            // Mix in counter for more entropy
            aes_key[i] ^= (uint8_t)(i * 0x37);
        }
        aes_key_initialized = 1;
    }
}

/**
 * Get next unique nonce
 */
uint64_t get_next_nonce(void) {
    return g_nonce_counter++;
}

/**
 * Encrypt entry using AES-CTR
 */
crypto_status_t aes_encrypt_entry(entry_t* entry) {
    if (!entry) {
        return CRYPTO_INVALID_PARAM;
    }
    
    // Check if already encrypted
    if (entry->is_encrypted) {
        return CRYPTO_ALREADY_ENCRYPTED;
    }
    
    // Initialize AES key if needed
    init_aes_key();
    
    // Get unique nonce
    entry->nonce = get_next_nonce();
    
    // Prepare counter block (16 bytes)
    // Format: [8 bytes nonce][8 bytes counter starting at 0]
    uint8_t ctr[16] = {0};
    memcpy(ctr, &entry->nonce, 8);
    
    // Simplified encryption: encrypt entire structure except is_encrypted and nonce
    // We keep is_encrypted and nonce unencrypted
    size_t is_encrypted_offset = offsetof(entry_t, is_encrypted);
    size_t nonce_offset = offsetof(entry_t, nonce);
    
    // Encrypt in chunks to avoid the excluded fields
    uint8_t* entry_bytes = (uint8_t*)entry;
    uint8_t encrypted_data[sizeof(entry_t)];
    memcpy(encrypted_data, entry_bytes, sizeof(entry_t));
    
    // Simplified: just two regions to encrypt
    struct {
        size_t start;
        size_t end;
    } regions[] = {
        {0, is_encrypted_offset},  // Before is_encrypted (includes field_type, equality_type)
        {nonce_offset + sizeof(uint64_t), sizeof(entry_t)}  // After nonce to end (entire attributes array)
    };
    
    // Encrypt each region (now just 2 regions)
    for (int i = 0; i < 2; i++) {
        size_t region_size = regions[i].end - regions[i].start;
        if (region_size > 0) {
            sgx_status_t status = sgx_aes_ctr_encrypt(
                (const sgx_aes_ctr_128bit_key_t*)aes_key,
                entry_bytes + regions[i].start,
                (uint32_t)region_size,
                ctr,
                128,  // Number of bits in counter
                encrypted_data + regions[i].start
            );
            
            if (status != SGX_SUCCESS) {
                return CRYPTO_OPERATION_FAILED;
            }
        }
    }
    
    // Copy encrypted data back to entry (except excluded fields)
    for (int i = 0; i < 2; i++) {
        size_t region_size = regions[i].end - regions[i].start;
        if (region_size > 0) {
            memcpy(entry_bytes + regions[i].start, 
                   encrypted_data + regions[i].start, 
                   region_size);
        }
    }
    
    // Mark as encrypted
    entry->is_encrypted = 1;
    
    return CRYPTO_SUCCESS;
}

/**
 * Decrypt entry using AES-CTR
 */
crypto_status_t aes_decrypt_entry(entry_t* entry) {
    if (!entry) {
        return CRYPTO_INVALID_PARAM;
    }
    
    // Check if not encrypted
    if (!entry->is_encrypted) {
        return CRYPTO_NOT_ENCRYPTED;
    }
    
    // Initialize AES key if needed
    init_aes_key();
    
    // Prepare counter block using stored nonce
    uint8_t ctr[16] = {0};
    memcpy(ctr, &entry->nonce, 8);
    
    // Simplified decryption: matches encryption with two regions
    size_t is_encrypted_offset = offsetof(entry_t, is_encrypted);
    size_t nonce_offset = offsetof(entry_t, nonce);
    
    uint8_t* entry_bytes = (uint8_t*)entry;
    uint8_t decrypted_data[sizeof(entry_t)];
    memcpy(decrypted_data, entry_bytes, sizeof(entry_t));
    
    struct {
        size_t start;
        size_t end;
    } regions[] = {
        {0, is_encrypted_offset},  // Before is_encrypted
        {nonce_offset + sizeof(uint64_t), sizeof(entry_t)}  // After nonce to end (entire attributes array)
    };
    
    // Decrypt each region (now just 2 regions)
    for (int i = 0; i < 2; i++) {
        size_t region_size = regions[i].end - regions[i].start;
        if (region_size > 0) {
            sgx_status_t status = sgx_aes_ctr_decrypt(
                (const sgx_aes_ctr_128bit_key_t*)aes_key,
                entry_bytes + regions[i].start,
                (uint32_t)region_size,
                ctr,
                128,
                decrypted_data + regions[i].start
            );
            
            if (status != SGX_SUCCESS) {
                return CRYPTO_OPERATION_FAILED;
            }
        }
    }
    
    // Copy decrypted data back
    for (int i = 0; i < 2; i++) {
        size_t region_size = regions[i].end - regions[i].start;
        if (region_size > 0) {
            memcpy(entry_bytes + regions[i].start, 
                   decrypted_data + regions[i].start, 
                   region_size);
        }
    }
    
    // Mark as not encrypted
    entry->is_encrypted = 0;
    
    return CRYPTO_SUCCESS;
}

/**
 * Reset nonce counter (for testing)
 */
void reset_nonce_counter(void) {
    g_nonce_counter = 1;
}