#ifndef AES_CRYPTO_H
#define AES_CRYPTO_H

#include "../enclave_types.h"
#include "entry_crypto.h"  // For crypto_status_t
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AES-CTR Encryption/Decryption for entry_t
 * 
 * Uses AES-128 in CTR mode with:
 * - 128-bit AES key derived from secure key
 * - 64-bit nonce (unique per encryption)
 * - 64-bit counter (starts at 0)
 * 
 * The nonce is automatically generated and stored in entry->nonce
 */

/**
 * Encrypt an entry using AES-CTR
 * - Generates unique nonce and stores in entry->nonce
 * - Encrypts all fields except: is_encrypted, nonce, column_names
 * - Sets is_encrypted flag to 1
 * 
 * @param entry The entry to encrypt
 * @return CRYPTO_SUCCESS on success, error code otherwise
 */
crypto_status_t aes_encrypt_entry(entry_t* entry);

/**
 * Decrypt an entry using AES-CTR
 * - Uses nonce stored in entry->nonce
 * - Decrypts all encrypted fields
 * - Sets is_encrypted flag to 0
 * 
 * @param entry The entry to decrypt
 * @return CRYPTO_SUCCESS on success, error code otherwise
 */
crypto_status_t aes_decrypt_entry(entry_t* entry);

/**
 * Get the next unique nonce value
 * @return Next nonce value
 */
uint64_t get_next_nonce(void);

/**
 * Reset nonce counter to 1 (for testing)
 */
void reset_nonce_counter(void);

/**
 * Initialize AES key (for internal use)
 */
void init_aes_key(void);

// Exposed for Waksman shuffle RNG
extern uint8_t aes_key[16];
extern int aes_key_initialized;

#ifdef __cplusplus
}
#endif

#endif // AES_CRYPTO_H