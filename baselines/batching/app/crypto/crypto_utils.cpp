#include "crypto_utils.h"
#include "../data_structures/entry.h"
#include "../utils/counted_ecalls.h"  // Use counted ecalls instead of direct Enclave_u.h
#include <iostream>
#include <random>
#include <chrono>

crypto_status_t CryptoUtils::encrypt_entry(Entry& entry, sgx_enclave_id_t eid) {
    // Check flag before calling SGX
    if (entry.is_encrypted) {
        log_crypto_error(CRYPTO_ALREADY_ENCRYPTED, "encrypt_entry");
        return CRYPTO_ALREADY_ENCRYPTED;
    }
    
    // Convert to entry_t
    entry_t c_entry = entry.to_entry_t();
    
    // Call SGX ecall (uses secure key inside enclave)
    crypto_status_t status;
    sgx_status_t sgx_status = counted_ecall_encrypt_entry(eid, &status, &c_entry);
    
    if (sgx_status != SGX_SUCCESS) {
        std::cerr << "SGX counted_ecall_encrypt_entry failed with status: " << sgx_status << std::endl;
        return CRYPTO_INVALID_PARAM;
    }
    
    if (status == CRYPTO_SUCCESS) {
        // Convert back only if successful
        entry.from_entry_t(c_entry);
    } else {
        log_crypto_error(status, "encrypt_entry (in enclave)");
    }
    
    return status;
}

crypto_status_t CryptoUtils::decrypt_entry(Entry& entry, sgx_enclave_id_t eid) {
    // Check flag before calling SGX
    if (!entry.is_encrypted) {
        log_crypto_error(CRYPTO_NOT_ENCRYPTED, "decrypt_entry");
        return CRYPTO_NOT_ENCRYPTED;
    }
    
    // Convert to entry_t
    entry_t c_entry = entry.to_entry_t();
    
    // Call SGX ecall (uses secure key inside enclave)
    crypto_status_t status;
    sgx_status_t sgx_status = counted_ecall_decrypt_entry(eid, &status, &c_entry);
    
    if (sgx_status != SGX_SUCCESS) {
        std::cerr << "SGX counted_ecall_decrypt_entry failed with status: " << sgx_status << std::endl;
        return CRYPTO_INVALID_PARAM;
    }
    
    if (status == CRYPTO_SUCCESS) {
        // Convert back only if successful
        entry.from_entry_t(c_entry);
    } else {
        log_crypto_error(status, "decrypt_entry (in enclave)");
    }
    
    return status;
}


uint32_t CryptoUtils::generate_key() {
    // Use random device for better randomness
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
    
    return dis(gen);
}

std::string CryptoUtils::get_status_message(crypto_status_t status) {
    switch (status) {
        case CRYPTO_SUCCESS:
            return "Success";
        case CRYPTO_ALREADY_ENCRYPTED:
            return "Entry is already encrypted";
        case CRYPTO_NOT_ENCRYPTED:
            return "Entry is not encrypted";
        case CRYPTO_INVALID_PARAM:
            return "Invalid parameter";
        default:
            return "Unknown error";
    }
}

void CryptoUtils::log_crypto_error(crypto_status_t status, const std::string& operation) {
    // TODO: Replace with proper warning system later
    std::string message = get_status_message(status);
    
    switch (status) {
        case CRYPTO_ALREADY_ENCRYPTED:
        case CRYPTO_NOT_ENCRYPTED:
            std::cerr << "WARNING: " << operation << " - " << message << std::endl;
            break;
        case CRYPTO_INVALID_PARAM:
            std::cerr << "ERROR: " << operation << " - " << message << std::endl;
            break;
        default:
            break;
    }
}