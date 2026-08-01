#ifndef APP_IO_ENTRY_H
#define APP_IO_ENTRY_H

#include <vector>
#include <string>
#include <cstdint>
#include "../data_structures/entry.h"

/**
 * IO_Entry - A lightweight entry type for I/O operations only
 * 
 * This class provides a dynamic attribute vector for I/O operations,
 * avoiding the fixed MAX_ATTRIBUTES size requirement of Entry.
 * Used only by Table for save/load operations.
 */
class IO_Entry {
public:
    // Only store the actual data needed for I/O
    std::vector<int32_t> attributes;
    std::vector<std::string> column_names;
    bool is_encrypted;
    uint64_t nonce;
    
    // Join attribute for convenience
    int32_t join_attr;
    
    // Constructors
    IO_Entry() : is_encrypted(false), nonce(0), join_attr(0) {}
    
    // Conversion from regular Entry (without column_names)
    IO_Entry(const Entry& entry, const std::vector<std::string>& schema) {
        // Copy attributes based on schema size
        for (size_t i = 0; i < schema.size() && i < MAX_ATTRIBUTES; i++) {
            attributes.push_back(entry.attributes[i]);
            column_names.push_back(schema[i]);
        }
        is_encrypted = entry.is_encrypted;
        nonce = entry.nonce;
        join_attr = entry.join_attr;
    }
    
    Entry to_entry() const {
        Entry entry;
        
        // Copy data to fixed arrays
        size_t copy_count = std::min(attributes.size(), (size_t)MAX_ATTRIBUTES);
        for (size_t i = 0; i < copy_count; i++) {
            entry.attributes[i] = attributes[i];
            // Note: column_names are no longer stored in Entry
        }
        
        // Clear remaining array elements
        for (size_t i = copy_count; i < MAX_ATTRIBUTES; i++) {
            entry.attributes[i] = 0;
        }
        
        entry.is_encrypted = is_encrypted;
        entry.nonce = nonce;
        entry.join_attr = join_attr;
        
        // Initialize metadata fields to 0 (will be set to NULL_VALUE by enclave if needed)
        entry.field_type = 0;
        entry.equality_type = 0;
        entry.original_index = 0;
        entry.local_mult = 0;
        entry.final_mult = 0;
        entry.foreign_sum = 0;
        entry.local_cumsum = 0;
        entry.local_interval = 0;
        entry.foreign_interval = 0;
        entry.local_weight = 0;
        entry.copy_index = 0;
        entry.alignment_key = 0;
        entry.dst_idx = 0;
        entry.index = 0;
        
        return entry;
    }
};

#endif // APP_IO_ENTRY_H