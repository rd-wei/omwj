#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include "../data_structures/data_structures.h"
#include "entry_crypto.h"
#include "sgx_urts.h"
#include <string>

/**
 * Application-side crypto utilities for Entry encryption/decryption
 * These functions handle the conversion between C++ Entry and C entry_t
 * and call the appropriate SGX ecalls
 */
class CryptoUtils {
public:
    /**
     * Encrypt a single entry using secure AES-CTR encryption
     * Uses encryption key stored securely inside the enclave
     * @param entry Entry to encrypt (modified in-place)
     * @param eid Enclave ID
     * @return Status code indicating success or failure
     */
    static crypto_status_t encrypt_entry(Entry& entry, sgx_enclave_id_t eid);
    
    /**
     * Decrypt a single entry using secure AES-CTR encryption
     * Uses encryption key stored securely inside the enclave
     * @param entry Entry to decrypt (modified in-place)
     * @param eid Enclave ID
     * @return Status code indicating success or failure
     */
    static crypto_status_t decrypt_entry(Entry& entry, sgx_enclave_id_t eid);
    
    /**
     * Generate a random encryption key
     * @return Random 32-bit key
     */
    static uint32_t generate_key();
    
    /**
     * Get human-readable error message for crypto status
     * @param status Crypto status code
     * @return Error message string
     */
    static std::string get_status_message(crypto_status_t status);

private:
    /**
     * Log crypto errors (placeholder for warning system)
     * @param status Error status
     * @param operation Operation that failed
     */
    static void log_crypto_error(crypto_status_t status, const std::string& operation);
};

#endif // CRYPTO_UTILS_H