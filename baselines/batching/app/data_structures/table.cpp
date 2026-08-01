#include "table.h"
#include <stdexcept>
#include <climits>
#include "debug_util.h"
#include "Enclave_u.h"
#include "../batch/ecall_batch_collector.h"
#include "../algorithms/merge_sort_manager.h"
#include "../algorithms/shuffle_manager.h"

// Constructor with required schema
Table::Table(const std::string& name, const std::vector<std::string>& schema) 
    : table_name(name), num_columns(schema.size()), schema_column_names(schema) {
    if (schema.empty()) {
        throw std::runtime_error("Table '" + name + "' cannot be created with empty schema");
    }
    if (schema.size() > MAX_ATTRIBUTES) {
        throw std::runtime_error("Table '" + name + "' schema has " + std::to_string(schema.size()) + 
                               " columns, exceeds MAX_ATTRIBUTES=" + std::to_string(MAX_ATTRIBUTES));
    }
}


void Table::add_entry(const Entry& entry) {
    entries.push_back(entry);
}

Entry& Table::get_entry(size_t index) {
    return entries[index];
}

const Entry& Table::get_entry(size_t index) const {
    return entries[index];
}

void Table::set_entry(size_t index, const Entry& entry) {
    if (index < entries.size()) {
        entries[index] = entry;
    }
}

Entry& Table::operator[](size_t index) {
    return entries[index];
}

const Entry& Table::operator[](size_t index) const {
    return entries[index];
}

size_t Table::size() const {
    return entries.size();
}

void Table::clear() {
    entries.clear();
}

void Table::set_all_field_type(entry_type_t type) {
    for (auto& entry : entries) {
        entry.field_type = type;
    }
}

void Table::initialize_original_indices() {
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].original_index = static_cast<int32_t>(i);
    }
}

void Table::initialize_leaf_multiplicities() {
    for (auto& entry : entries) {
        entry.local_mult = 1;
        entry.final_mult = 1;
    }
}

std::vector<entry_t> Table::to_entry_t_vector() const {
    std::vector<entry_t> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(entry.to_entry_t());
    }
    return result;
}

void Table::from_entry_t_vector(const std::vector<entry_t>& c_entries) {
    entries.clear();
    entries.reserve(c_entries.size());
    for (const auto& c_entry : c_entries) {
        entries.emplace_back(c_entry);
    }
}

void Table::set_table_name(const std::string& name) {
    table_name = name;
}

std::string Table::get_table_name() const {
    return table_name;
}

void Table::set_num_columns(size_t n) {
    num_columns = n;
}

size_t Table::get_num_columns() const {
    return num_columns;
}

// Schema management methods for slim mode migration
void Table::set_schema(const std::vector<std::string>& columns) {
    schema_column_names = columns;
    // Also update num_columns to match
    if (num_columns == 0 || num_columns != columns.size()) {
        num_columns = columns.size();
    }
}

std::vector<std::string> Table::get_schema() const {
    return schema_column_names;
}

size_t Table::get_column_index(const std::string& col_name) const {
    for (size_t i = 0; i < schema_column_names.size(); i++) {
        if (schema_column_names[i] == col_name) {
            return i;
        }
    }
    // Column not found in schema
    throw std::runtime_error("Column not found: " + col_name);
}

bool Table::has_column(const std::string& col_name) const {
    // Check schema only
    for (const auto& name : schema_column_names) {
        if (name == col_name) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Table::generate_generic_schema(size_t num_columns) {
    std::vector<std::string> schema;
    for (size_t i = 0; i < num_columns; i++) {
        schema.push_back("col" + std::to_string(i + 1));
    }
    return schema;
}

int32_t Table::get_attribute(size_t row, const std::string& col_name) const {
    if (row >= entries.size()) {
        throw std::out_of_range("Row index out of bounds");
    }
    size_t col_index = get_column_index(col_name);
    const Entry& entry = entries[row];
    if (col_index >= MAX_ATTRIBUTES) {
        throw std::out_of_range("Column index out of bounds");
    }
    return entry.attributes[col_index];
}

void Table::set_attribute(size_t row, const std::string& col_name, int32_t value) {
    if (row >= entries.size()) {
        throw std::out_of_range("Row index out of bounds");
    }
    size_t col_index = get_column_index(col_name);
    Entry& entry = entries[row];
    if (col_index >= MAX_ATTRIBUTES) {
        throw std::out_of_range("Column index exceeds MAX_ATTRIBUTES");
    }
    // Just set the value - column_names should already be set
    entry.attributes[col_index] = value;
}

std::vector<Entry>::iterator Table::begin() {
    return entries.begin();
}

std::vector<Entry>::iterator Table::end() {
    return entries.end();
}

std::vector<Entry>::const_iterator Table::begin() const {
    return entries.begin();
}

std::vector<Entry>::const_iterator Table::end() const {
    return entries.end();
}

Table::EncryptionStatus Table::get_encryption_status() const {
    if (entries.empty()) {
        return UNENCRYPTED;  // Empty table is considered unencrypted
    }
    
    bool first_is_encrypted = entries[0].is_encrypted;
    
    for (size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].is_encrypted != first_is_encrypted) {
            return MIXED;  // Found a mismatch
        }
    }
    
    return first_is_encrypted ? ENCRYPTED : UNENCRYPTED;
}

// ============================================================================
// Oblivious Operations Implementation
// ============================================================================

void Table::check_sgx_status(sgx_status_t status, const std::string& operation) {
    if (status != SGX_SUCCESS) {
        throw std::runtime_error("SGX error in " + operation + ": " + std::to_string(status));
    }
}








void Table::batched_distribute_pass(sgx_enclave_id_t eid, size_t distance, OpEcall op_type, int32_t* params) {
    DEBUG_TRACE("Table::batched_distribute_pass: Starting with distance %zu, op_type=%d", distance, op_type);
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all pairs at given distance
    // Process from right to left (same as non-batched version)
    for (size_t i = entries.size() - distance; i > 0; i--) {
        collector.add_operation(entries[i - 1], entries[i - 1 + distance], params);
    }
    
    // Handle i = 0 case separately to avoid underflow
    if (distance < entries.size()) {
        collector.add_operation(entries[0], entries[distance], params);
    }
    
    // Flush the batch - this writes back to entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_distribute_pass: Complete");
}


// ============================================================================
// Batched Operations Implementation
// ============================================================================

Table Table::batched_map(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params) const {
    DEBUG_TRACE("Table::batched_map: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    Table result(table_name, schema_column_names);  // Preserve schema
    result.set_num_columns(num_columns);
    
    // Copy entries to result first (since we can't modify const entries)
    for (const auto& entry : entries) {
        result.add_entry(entry);
    }
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all operations for map (single entry operations)
    for (size_t i = 0; i < result.entries.size(); i++) {
        collector.add_operation(result.entries[i], params);
    }
    
    // Flush the batch - this writes back to result.entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_map: Complete with %zu entries", result.size());
    return result;
}

void Table::batched_linear_pass(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params) {
    if (entries.size() < 2) return;
    
    DEBUG_TRACE("Table::batched_linear_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all window operations - work directly with Entry objects
    for (size_t i = 0; i < entries.size() - 1; i++) {
        collector.add_operation(entries[i], entries[i+1], params);
    }
    
    // Flush the batch - this writes back to entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_linear_pass: Complete");
}

void Table::batched_parallel_pass(Table& other, sgx_enclave_id_t eid, OpEcall op_type, int32_t* params) {
    if (entries.size() != other.entries.size()) {
        throw std::runtime_error("Tables must have the same size for parallel pass");
    }
    
    DEBUG_TRACE("Table::batched_parallel_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);
    
    // Create batch collector
    EcallBatchCollector collector(eid, op_type);
    
    // Add all parallel operations - work directly with Entry objects
    for (size_t i = 0; i < entries.size(); i++) {
        collector.add_operation(entries[i], other.entries[i], params);
    }
    
    // Flush the batch - this writes back to both tables' entries
    collector.flush();
    
    DEBUG_TRACE("Table::batched_parallel_pass: Complete");
}


void Table::add_batched_padding(size_t count, sgx_enclave_id_t eid, uint8_t encryption_status, OpEcall padding_op) {
    if (count == 0) {
        return;
    }
    
    DEBUG_TRACE("Table::add_batched_padding: Adding %zu padding entries", count);
    
    // Reserve space for new entries
    entries.reserve(entries.size() + count);
    
    // Create batch collector for padding creation
    EcallBatchCollector collector(eid, padding_op);
    
    // Create padding entries in batches
    for (size_t i = 0; i < count; i++) {
        Entry padding;
        // Initialize entry_t structure instead of Entry class
        entry_t padding_entry;
        memset(&padding_entry, 0, sizeof(entry_t));
        padding_entry.is_encrypted = encryption_status;  // Match table's encryption
        padding = Entry(padding_entry);
        
        // Add to table first
        entries.push_back(padding);
        
        // Add to batch for transformation
        collector.add_operation(entries.back());
    }
    
    // Flush any remaining operations
    collector.flush();
    
    DEBUG_TRACE("Table::add_batched_padding: Complete - added %zu entries", count);
}


void Table::pad_to_shuffle_size(sgx_enclave_id_t eid) {
    size_t current_size = entries.size();
    size_t target_size = calculate_shuffle_padding(current_size);
    
    if (target_size > current_size) {
        size_t padding_count = target_size - current_size;
        DEBUG_INFO("Table::pad_to_shuffle_size: Padding from %zu to %zu (adding %zu entries)", 
                   current_size, target_size, padding_count);
        
        // Determine encryption status from existing entries
        uint8_t encryption_status = 0;
        if (!entries.empty() && entries[0].is_encrypted) {
            encryption_status = 1;
        }
        
        // Add padding entries for shuffle sort
        add_batched_padding(padding_count, eid, encryption_status, OP_ECALL_TRANSFORM_SET_SORT_PADDING);
    }
}

size_t Table::calculate_shuffle_padding(size_t n) {
    if (n <= WAKSMAN_THRESHOLD) {
        // Small vector: just pad to power of 2
        return next_power_of_two(n);
    }

    // Large vector: need m = 2^a * k^b
    const size_t k = MERGE_SORT_K;

    // First determine b: number of k-way decomposition levels needed
    // After b levels, we want size <= WAKSMAN_THRESHOLD
    size_t temp = n;
    size_t b = 0;
    size_t k_power = 1;

    while (temp > WAKSMAN_THRESHOLD) {
        temp = (temp + k - 1) / k;  // Ceiling division
        b++;
        k_power *= k;
    }

    // Now temp <= WAKSMAN_THRESHOLD after b levels of division by k
    // We need temp to be a power of 2 for the final Waksman shuffle
    size_t a_part = next_power_of_two(temp);

    // Calculate m = a_part * k^b
    size_t m = a_part * k_power;

    // Ensure m >= n (it should be by construction, but let's be safe)
    if (m < n) {
        // This shouldn't happen, but if it does, we need to increase a_part
        a_part *= 2;
        m = a_part * k_power;
    }

    DEBUG_TRACE("Shuffle padding: n=%zu, b=%zu, a_part=%zu, k^b=%zu, m=%zu",
                n, b, a_part, k_power, m);

    return m;
}

bool Table::is_valid_shuffle_size(size_t n) {
    // Check if n = 2^a * k^b
    while (n > WAKSMAN_THRESHOLD && n % MERGE_SORT_K == 0) {
        n /= MERGE_SORT_K;
    }
    // Should be power of 2 and <= WAKSMAN_THRESHOLD
    return n <= WAKSMAN_THRESHOLD && (n & (n - 1)) == 0;
}

void Table::shuffle_merge_sort(sgx_enclave_id_t eid, OpEcall op_type) {
    if (entries.size() <= 1) return;
    
    size_t original_size = entries.size();
    DEBUG_INFO("Table::shuffle_merge_sort: Starting with %zu entries, op_type=%d", 
               original_size, op_type);
    
    // Phase 1: Pad to 2^a * k^b format
    pad_to_shuffle_size(eid);
    DEBUG_INFO("Table::shuffle_merge_sort: Padded to %zu entries", entries.size());
    
    // Phase 2: Shuffle using ShuffleManager (expects padded input)
    ShuffleManager shuffle_mgr(eid);
    shuffle_mgr.shuffle(*this);
    DEBUG_INFO("Table::shuffle_merge_sort: Shuffle phase complete");
    
    // Phase 3: Merge sort using MergeSortManager (works with padded data)
    MergeSortManager merge_mgr(eid, op_type);
    merge_mgr.sort(*this);
    DEBUG_INFO("Table::shuffle_merge_sort: Merge sort phase complete");
    
    // Phase 4: Truncate to original size
    // After sorting, padding entries (with JOIN_ATTR_POS_INF) are at the end
    if (entries.size() > original_size) {
        DEBUG_INFO("Table::shuffle_merge_sort: Truncated from %zu to %zu entries",
                   entries.size(), original_size);
        entries.resize(original_size);
    }
    
    DEBUG_INFO("Table::shuffle_merge_sort: Complete with %zu entries", entries.size());
}

