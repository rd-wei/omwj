#ifndef SECURE_KEY_H
#define SECURE_KEY_H

/**
 * Secure Encryption Key Management
 * 
 * This file contains the encryption key that is ONLY accessible from within
 * the SGX enclave. The key never leaves the enclave's protected memory.
 * 
 * Security Properties:
 * - Key exists only in enclave memory (protected by CPU)
 * - Untrusted application cannot access this key
 * - All encryption/decryption happens inside the enclave
 * 
 * To ensure security:
 * - This file should ONLY be included in enclave source files
 * - Never pass this key value to untrusted code
 * - All crypto operations must be done via ECALLs
 */

#ifdef ENCLAVE_BUILD
    // The actual encryption key - only visible inside enclave
    #define SECURE_ENCRYPTION_KEY 0xDEADBEEF
    
    // Type for the key (matches XOR crypto functions)
    typedef uint32_t encryption_key_t;
    
#else
    // If someone tries to include this from untrusted code, fail at compile time
    #error "SECURITY VIOLATION: Encryption key can only be accessed from within the SGX enclave!"
#endif

#endif // SECURE_KEY_H