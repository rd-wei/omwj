#ifndef CRYPTO_HELPERS_H
#define CRYPTO_HELPERS_H

#include "../enclave_types.h"
#include "entry_crypto.h"
#include "aes_crypto.h"

/**
 * Crypto Helper Functions
 * 
 * These helpers provide a consistent pattern for decrypt-operate-encrypt
 * operations on entries, eliminating code duplication across transform,
 * window, and comparator functions.
 */

// Function pointer types for operations
typedef void (*entry_operation_t)(entry_t*);
typedef void (*pair_operation_t)(entry_t*, entry_t*);

/**
 * Apply an operation to a single entry with automatic decrypt/encrypt
 * @param entry The entry to operate on
 * @param operation The operation to apply while decrypted
 */
void apply_to_decrypted_entry(entry_t* entry, entry_operation_t operation);

/**
 * Apply an operation to a pair of entries with automatic decrypt/encrypt
 * @param e1 First entry
 * @param e2 Second entry  
 * @param operation The operation to apply while both are decrypted
 */
void apply_to_decrypted_pair(entry_t* e1, entry_t* e2, pair_operation_t operation);

#endif // CRYPTO_HELPERS_H