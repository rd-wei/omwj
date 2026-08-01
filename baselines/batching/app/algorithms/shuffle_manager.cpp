#include "shuffle_manager.h"
#include "../utils/counted_ecalls.h"  // For counted ecall wrappers
#include "../utils/counted_ocalls.h"  // For centralized ocall definitions
#include "debug_util.h"
#include "../crypto/crypto_utils.h"
#include <algorithm>

// Static member initialization
ShuffleManager* ShuffleManager::current_instance = nullptr;

// Note: Ocall handlers have been moved to app/utils/counted_ocalls.cpp

ShuffleManager::ShuffleManager(sgx_enclave_id_t enclave_id) 
    : eid(enclave_id) {
    DEBUG_INFO("ShuffleManager created");
}

ShuffleManager::~ShuffleManager() {
    clear_current();
}

void ShuffleManager::shuffle(Table& table) {
    if (table.size() <= 1) return;
    
    DEBUG_INFO("ShuffleManager::shuffle starting with %zu entries", table.size());
    
    // Verify input is valid size (2^a * k^b)
    if (!Table::is_valid_shuffle_size(table.size())) {
        DEBUG_ERROR("Invalid shuffle size: %zu (not 2^a * k^b format)", table.size());
        // Could throw exception or assert in production
        return;
    }
    
    // Convert table to vector of entries
    std::vector<Entry> entries;
    entries.reserve(table.size());
    for (size_t i = 0; i < table.size(); i++) {
        entries.push_back(table[i]);
    }
    
    // Perform recursive shuffle (input already padded)
    recursive_shuffle(entries);
    
    // Copy back to table
    table.clear();
    for (const auto& entry : entries) {
        table.add_entry(entry);
    }
    
    DEBUG_INFO("ShuffleManager::shuffle complete");
}

void ShuffleManager::recursive_shuffle(std::vector<Entry>& entries) {
    size_t n = entries.size();
    if (n <= 1) return;
    
    DEBUG_TRACE("Recursive shuffle: n=%zu", n);
    
    if (n <= WAKSMAN_THRESHOLD) {
        shuffle_small(entries);
    } else {
        shuffle_large(entries);
    }
}

void ShuffleManager::shuffle_small(std::vector<Entry>& entries) {
    size_t n = entries.size();
    
    DEBUG_INFO("Small shuffle: n=%zu", n);
    
    // No padding here - already done in shuffle()
    
    // Convert to entry_t array
    std::vector<entry_t> c_entries;
    c_entries.reserve(n);
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    
    // Call 2-way Waksman shuffle
    sgx_status_t status = SGX_SUCCESS;
    sgx_status_t ecall_status = counted_ecall_oblivious_2way_waksman(
        eid, &status, c_entries.data(), n);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("Waksman shuffle failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        return;
    }
    
    // Convert back all entries (including padding)
    entries.clear();
    for (size_t i = 0; i < n; i++) {
        Entry e;
        e.from_entry_t(c_entries[i]);
        entries.push_back(e);
    }
    
    DEBUG_INFO("Small shuffle complete: %zu entries", n);
}

void ShuffleManager::shuffle_large(std::vector<Entry>& entries) {
    size_t n = entries.size();
    const size_t k = MERGE_SORT_K;  // Use same K as merge sort (8)
    
    // No padding here - already done in shuffle()
    // n should already be a valid size (2^a * k^b)
    
    DEBUG_INFO("Large shuffle: n=%zu, k=%zu", n, k);
    
    // Phase 1: K-way decomposition
    set_as_current();
    
    // Initialize groups
    groups.clear();
    groups.resize(k);
    for (auto& group : groups) {
        group.reserve(n / k);
    }
    
    // Convert to entry_t and call decompose
    std::vector<entry_t> c_entries;
    c_entries.reserve(n);
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    
    sgx_status_t status = SGX_SUCCESS;
    sgx_status_t ecall_status = counted_ecall_k_way_shuffle_decompose(
        eid, &status, c_entries.data(), n);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("K-way decompose failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        clear_current();
        return;
    }
    
    DEBUG_TRACE("Decomposition complete, group sizes:");
    for (size_t i = 0; i < k; i++) {
        DEBUG_TRACE("  Group %zu: %zu entries", i, groups[i].size());
    }
    
    // Phase 2: Recursively shuffle each group
    for (size_t i = 0; i < k; i++) {
        DEBUG_TRACE("Recursively shuffling group %zu", i);
        recursive_shuffle(groups[i]);
    }
    
    // Phase 3: K-way reconstruction
    output_entries.clear();
    output_entries.reserve(n);
    
    // Reset positions for reading
    group_positions.clear();
    group_positions.resize(k, 0);
    
    ecall_status = counted_ecall_k_way_shuffle_reconstruct(
        eid, &status, n);
    
    if (ecall_status != SGX_SUCCESS || status != SGX_SUCCESS) {
        DEBUG_ERROR("K-way reconstruct failed: ecall_status=%d, status=%d", 
                    ecall_status, status);
        clear_current();
        return;
    }
    
    // Move output to entries vector (keep all entries including padding)
    entries = std::move(output_entries);
    
    clear_current();
    
    DEBUG_INFO("Large shuffle complete: %zu entries", n);
}


// Ocall handlers for buffered I/O
void ShuffleManager::handle_flush_to_group(int group_idx, entry_t* buffer, size_t buffer_size) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid flush_to_group: group_idx=%d, current=%p", 
                    group_idx, current_instance);
        return;
    }
    
    // Append buffer contents to the specified group
    for (size_t i = 0; i < buffer_size; i++) {
        Entry e;
        e.from_entry_t(buffer[i]);
        current_instance->groups[group_idx].push_back(e);
    }
    
    DEBUG_TRACE("Flushed %zu entries to group %d (total=%zu)", 
                buffer_size, group_idx, current_instance->groups[group_idx].size());
}

void ShuffleManager::handle_refill_from_group(int group_idx, entry_t* buffer, 
                                               size_t buffer_size, size_t* actual_filled) {
    if (!current_instance || group_idx < 0 || group_idx >= MERGE_SORT_K) {
        DEBUG_ERROR("Invalid refill_from_group: group_idx=%d", group_idx);
        *actual_filled = 0;
        return;
    }
    
    size_t& pos = current_instance->group_positions[group_idx];
    const auto& group = current_instance->groups[group_idx];
    size_t available = group.size() - pos;
    size_t to_fill = std::min(buffer_size, available);
    
    // Fill buffer from current position in group
    for (size_t i = 0; i < to_fill; i++) {
        buffer[i] = group[pos + i].to_entry_t();
    }
    
    pos += to_fill;
    *actual_filled = to_fill;
    
    DEBUG_TRACE("Refilled %zu entries from group %d (pos=%zu/%zu)", 
                to_fill, group_idx, pos, group.size());
}

void ShuffleManager::handle_flush_output(entry_t* buffer, size_t buffer_size) {
    if (!current_instance) {
        DEBUG_ERROR("No current instance for flush_output");
        return;
    }
    
    // Append buffer contents to output
    for (size_t i = 0; i < buffer_size; i++) {
        Entry e;
        e.from_entry_t(buffer[i]);
        current_instance->output_entries.push_back(e);
    }
    
    DEBUG_TRACE("Flushed %zu entries to output (total=%zu)", 
                buffer_size, current_instance->output_entries.size());
}

void ShuffleManager::set_as_current() {
    current_instance = this;
}

void ShuffleManager::clear_current() {
    if (current_instance == this) {
        current_instance = nullptr;
    }
}

