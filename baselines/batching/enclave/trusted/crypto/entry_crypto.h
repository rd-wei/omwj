#ifndef ENTRY_CRYPTO_H
#define ENTRY_CRYPTO_H

#include "enclave_types.h"
#include <stdbool.h>
#include <stddef.h>  // For size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encryption status codes
 */
typedef enum {
    CRYPTO_SUCCESS = 0,           // Operation successful
    CRYPTO_ALREADY_ENCRYPTED = 1, // Attempted to encrypt already encrypted entry
    CRYPTO_NOT_ENCRYPTED = 2,     // Attempted to decrypt non-encrypted entry
    CRYPTO_INVALID_PARAM = 3,     // Invalid parameter (e.g., NULL pointer)
    CRYPTO_OPERATION_FAILED = 4   // Encryption/decryption operation failed
} crypto_status_t;

// Legacy XOR encryption functions have been removed.
// Use AES-CTR encryption from aes_crypto.h instead.

#ifdef __cplusplus
}
#endif

#endif // ENTRY_CRYPTO_H