#include "entry.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <sstream>
#include "debug_util.h"

Entry::Entry() 
    : field_type(SOURCE), 
      equality_type(EQ), 
      is_encrypted(false),
      nonce(0),
      join_attr(0), 
      original_index(0), 
      local_mult(0), 
      final_mult(0),
      foreign_sum(0), 
      local_cumsum(0), 
      local_interval(0),
      foreign_interval(0), 
      local_weight(0),
      copy_index(0), 
      alignment_key(0),
      dst_idx(0),
      index(0) {
    // Initialize arrays
    memset(attributes, 0, sizeof(attributes));
}

Entry::Entry(const entry_t& c_entry) {
    from_entry_t(c_entry);
}

entry_t Entry::to_entry_t() const {
    // Removed TRACE logs to reduce debug output volume
    
    entry_t result;
    memset(&result, 0, sizeof(entry_t));
    
    // Copy basic fields
    result.field_type = field_type;
    result.equality_type = equality_type;
    result.is_encrypted = is_encrypted;
    result.nonce = nonce;
    result.join_attr = join_attr;
    result.original_index = original_index;
    result.local_mult = local_mult;
    result.final_mult = final_mult;
    result.foreign_sum = foreign_sum;
    result.local_cumsum = local_cumsum;
    result.local_interval = local_interval;
    result.foreign_interval = foreign_interval;
    result.local_weight = local_weight;
    result.copy_index = copy_index;
    result.alignment_key = alignment_key;
    result.dst_idx = dst_idx;
    result.index = index;
    
    // Copy all MAX_ATTRIBUTES - simpler and consistent
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        result.attributes[i] = attributes[i];
    }
    
    return result;
}

void Entry::from_entry_t(const entry_t& c_entry) {
    // Removed TRACE logs to reduce debug output volume
    
    field_type = c_entry.field_type;
    equality_type = c_entry.equality_type;
    is_encrypted = c_entry.is_encrypted;
    nonce = c_entry.nonce;
    join_attr = c_entry.join_attr;
    original_index = c_entry.original_index;
    local_mult = c_entry.local_mult;
    final_mult = c_entry.final_mult;
    foreign_sum = c_entry.foreign_sum;
    local_cumsum = c_entry.local_cumsum;
    local_interval = c_entry.local_interval;
    foreign_interval = c_entry.foreign_interval;
    local_weight = c_entry.local_weight;
    copy_index = c_entry.copy_index;
    alignment_key = c_entry.alignment_key;
    dst_idx = c_entry.dst_idx;
    index = c_entry.index;
    
    // Copy all MAX_ATTRIBUTES - no need to figure out actual size
    // Empty entries will just have zeros
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        attributes[i] = c_entry.attributes[i];
    }
}

void Entry::from_entry_t(const entry_t& c_entry, const std::vector<std::string>& /*schema*/) {
    // Copy all metadata fields
    field_type = c_entry.field_type;
    equality_type = c_entry.equality_type;
    is_encrypted = c_entry.is_encrypted;
    nonce = c_entry.nonce;
    join_attr = c_entry.join_attr;
    original_index = c_entry.original_index;
    local_mult = c_entry.local_mult;
    final_mult = c_entry.final_mult;
    foreign_sum = c_entry.foreign_sum;
    local_cumsum = c_entry.local_cumsum;
    local_interval = c_entry.local_interval;
    foreign_interval = c_entry.foreign_interval;
    local_weight = c_entry.local_weight;
    copy_index = c_entry.copy_index;
    alignment_key = c_entry.alignment_key;
    dst_idx = c_entry.dst_idx;
    index = c_entry.index;
    
    // Copy all MAX_ATTRIBUTES from c_entry
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        attributes[i] = c_entry.attributes[i];
    }
    
    // Schema is now managed at Table level only
    // No column_names in Entry anymore
}

void Entry::clear() {
    *this = Entry();  // Reset to default values
}

// Note: Attribute access methods by column name have been removed
// Use Table::get_attribute(row, column_name) for name-based access
// This simplifies Entry and moves schema management to Table level only

bool Entry::operator<(const Entry& other) const {
    return join_attr < other.join_attr;
}

bool Entry::operator==(const Entry& other) const {
    return join_attr == other.join_attr &&
           field_type == other.field_type &&
           original_index == other.original_index;
}

std::string Entry::to_string() const {
    std::stringstream ss;
    ss << "Entry{type=" << field_type 
       << ", join_attr=" << join_attr
       << ", local_mult=" << local_mult
       << ", final_mult=" << final_mult
       << ", attrs=[";
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (i > 0) ss << ", ";
        ss << attributes[i];
    }
    ss << "]}";
    return ss.str();
}