#ifndef APP_ENTRY_H
#define APP_ENTRY_H

#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include "../../common/constants.h"
#include "../../common/types_common.h"
#include "../../common/enclave_types.h"

/**
 * Entry Class - Represents a single row/tuple in a table
 * 
 * Encapsulates all metadata and attributes for oblivious multi-way band join processing.
 * Supports conversion between C++ and C structs for SGX enclave communication.
 */
class Entry {
public:
    // Entry metadata (using int32_t for consistency with enclave struct)
    int32_t field_type;      // entry_type_t
    int32_t equality_type;   // equality_type_t
    bool is_encrypted;
    
    // Encryption nonce for AES-CTR mode
    uint64_t nonce;
    
    // Join attribute  
    int32_t join_attr;
    
    // Persistent metadata
    int32_t original_index;
    int32_t local_mult;
    int32_t final_mult;
    int32_t foreign_sum;
    
    // Temporary metadata
    int32_t local_cumsum;
    int32_t local_interval;
    int32_t foreign_interval;
    int32_t local_weight;
    
    // Expansion metadata
    int32_t copy_index;
    int32_t alignment_key;
    
    // Distribution fields
    int32_t dst_idx;
    int32_t index;
    
    // Data attributes - fixed array of MAX_ATTRIBUTES
    // Always process all MAX_ATTRIBUTES - empty slots have zeros
    int32_t attributes[MAX_ATTRIBUTES];
    
    // Constructors
    Entry();
    Entry(const entry_t& c_entry);
    
    // Conversion methods
    entry_t to_entry_t() const;
    void from_entry_t(const entry_t& c_entry);
    void from_entry_t(const entry_t& c_entry, const std::vector<std::string>& schema);
    
    // Utility methods
    void clear();
    // NOTE: Use CryptoUtils::encrypt_entry() and CryptoUtils::decrypt_entry() for actual encryption/decryption
    
    // Note: Attribute access by column name has been moved to Table class
    // Use Table::get_attribute(row, column_name) instead
    
    // Comparison operators for sorting
    bool operator<(const Entry& other) const;
    bool operator==(const Entry& other) const;
    
    // Debug output
    std::string to_string() const;
};

#endif // APP_ENTRY_H