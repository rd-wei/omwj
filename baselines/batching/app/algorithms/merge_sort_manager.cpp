#include "merge_sort_manager.h"
#include "debug_util.h"
#include "../utils/counted_ecalls.h"
#include "../utils/counted_ocalls.h"  // For centralized ocall definitions
#include <algorithm>
#include <cmath>

// Static member initialization
MergeSortManager* MergeSortManager::current_instance = nullptr;

// Note: ocall_refill_buffer has been moved to app/utils/counted_ocalls.cpp

MergeSortManager::MergeSortManager(sgx_enclave_id_t enclave_id, OpEcall type)
    : eid(enclave_id), comparator_type(type) {
    DEBUG_INFO("MergeSortManager created with comparator type %d", type);
}

MergeSortManager::~MergeSortManager() {
    clear_current();
}

void MergeSortManager::set_as_current() {
    current_instance = this;
}

void MergeSortManager::clear_current() {
    if (current_instance == this) {
        current_instance = nullptr;
    }
}

void MergeSortManager::handle_refill_buffer(int buffer_idx, entry_t* buffer, 
                                           size_t buffer_size, size_t* actual_filled) {
    if (!current_instance || buffer_idx < 0 || 
        buffer_idx >= (int)current_instance->current_merge_indices.size()) {
        DEBUG_ERROR("Invalid buffer_idx %d (current_merge_indices.size=%zu)", 
                    buffer_idx, current_instance ? current_instance->current_merge_indices.size() : 0);
        *actual_filled = 0;
        return;
    }
    
    // Map enclave's buffer index to actual run index
    size_t run_idx = current_instance->current_merge_indices[buffer_idx];
    
    if (run_idx >= current_instance->runs.size()) {
        DEBUG_ERROR("Invalid run_idx %zu (runs.size=%zu)", 
                    run_idx, current_instance->runs.size());
        *actual_filled = 0;
        return;
    }
    
    auto& run = current_instance->runs[run_idx];
    auto& pos = current_instance->run_positions[run_idx];
    
    // Calculate how many entries to copy
    size_t remaining = run.size() - pos;
    size_t to_copy = std::min(buffer_size, remaining);
    
    // Copy entries to buffer
    for (size_t i = 0; i < to_copy; i++) {
        buffer[i] = run[pos + i].to_entry_t();
    }
    
    pos += to_copy;
    *actual_filled = to_copy;
    
    DEBUG_TRACE("Refilled buffer %d (run %zu) with %zu entries (pos now %zu/%zu)", 
                buffer_idx, run_idx, to_copy, pos, run.size());
}

void MergeSortManager::sort(Table& table) {
    if (table.size() <= 1) {
        return;  // Already sorted
    }
    
    DEBUG_INFO("Starting merge sort on table with %zu entries", table.size());
    
    // Phase 1: Create initial sorted runs
    create_sorted_runs(table);
    
    // Phase 2: Merge runs recursively
    merge_runs_recursive();
    
    // Copy final result back to table
    if (runs.size() == 1) {
        // Store the original size for verification
        size_t original_size = table.size();
        
        table.clear();
        for (const auto& entry : runs[0]) {
            table.add_entry(entry);
        }
        
        // Verify that merge sort preserved the size
        if (table.size() != original_size) {
            DEBUG_ERROR("MERGE SORT BUG: Size changed from %zu to %zu (diff: %zd)", 
                        original_size, table.size(), 
                        (ssize_t)table.size() - (ssize_t)original_size);
        }
        
        DEBUG_INFO("Merge sort complete, table has %zu sorted entries", table.size());
    } else {
        DEBUG_ERROR("Merge sort failed - expected 1 run, got %zu", runs.size());
    }
}

void MergeSortManager::create_sorted_runs(Table& table) {
    size_t run_size = MAX_BATCH_SIZE;  // Maximum entries per run
    size_t num_runs = (table.size() + run_size - 1) / run_size;
    
    DEBUG_INFO("Creating %zu sorted runs of size %zu", num_runs, run_size);
    
    runs.clear();
    runs.reserve(num_runs);
    
    for (size_t i = 0; i < num_runs; i++) {
        size_t start = i * run_size;
        size_t end = std::min(start + run_size, table.size());
        size_t count = end - start;
        
        DEBUG_TRACE("Creating run %zu: start=%zu, end=%zu, count=%zu", i, start, end, count);
        
        // Extract run from table
        std::vector<Entry> run;
        run.reserve(count);
        for (size_t j = start; j < end; j++) {
            run.push_back(table[j]);
        }
        
        // Sort this run in enclave
        sort_run_in_enclave(run);
        
        DEBUG_TRACE("Run %zu after sorting has %zu entries", i, run.size());
        
        // Add to runs
        runs.push_back(std::move(run));
    }
    
    DEBUG_INFO("Created %zu sorted runs", runs.size());
    
#if DEBUG_LEVEL >= 1
    // Debug: Check total entries in all runs
    size_t total_entries = 0;
    for (const auto& run : runs) {
        total_entries += run.size();
    }
    DEBUG_INFO("Total entries in all runs: %zu (original: %zu)", total_entries, table.size());
#endif
}

void MergeSortManager::sort_run_in_enclave(std::vector<Entry>& entries) {
    if (entries.empty()) {
        return;
    }
    
    size_t size = entries.size();
    DEBUG_TRACE("Sorting run of %zu entries in enclave", size);
    
    // Convert to entry_t array
    std::vector<entry_t> entry_array(size);
    for (size_t i = 0; i < size; i++) {
        entry_array[i] = entries[i].to_entry_t();
    }
    
    // Call enclave to sort
    sgx_status_t retval;
    sgx_status_t status = counted_ecall_heap_sort(eid, &retval, entry_array.data(), size,
                                                   (int)comparator_type);
    
    if (status != SGX_SUCCESS) {
        DEBUG_ERROR("ecall_heap_sort failed with status %d", status);
        return;
    }
    
    // Convert back to Entry objects
    for (size_t i = 0; i < size; i++) {
        entries[i].from_entry_t(entry_array[i]);
    }
    
    DEBUG_TRACE("Run sorted successfully");
}

void MergeSortManager::merge_runs_recursive() {
    while (runs.size() > 1) {
        std::vector<std::vector<Entry>> new_runs;
        
        DEBUG_TRACE("Starting merge iteration with %zu runs", runs.size());
        
#if DEBUG_LEVEL >= 2
        // Debug: Check input sizes
        size_t total_before = 0;
        for (const auto& run : runs) {
            total_before += run.size();
        }
        DEBUG_TRACE("Total entries before merge: %zu", total_before);
#endif
        
        // Merge runs in groups of k
        for (size_t i = 0; i < runs.size(); i += MERGE_SORT_K) {
            size_t end = std::min(i + MERGE_SORT_K, runs.size());
            std::vector<size_t> run_indices;
            
#if DEBUG_LEVEL >= 2
            size_t input_total = 0;
#endif
            for (size_t j = i; j < end; j++) {
                run_indices.push_back(j);
#if DEBUG_LEVEL >= 2
                input_total += runs[j].size();
#endif
            }
            
            DEBUG_TRACE("Merging runs %zu-%zu (%zu runs)", i, end-1, run_indices.size());
            
            // Merge these k (or fewer) runs
            std::vector<Entry> merged = k_way_merge(run_indices);
            
#if DEBUG_LEVEL >= 2
            DEBUG_TRACE("Merged result has %zu entries (expected %zu)", 
                       merged.size(), input_total);
#endif
            
            new_runs.push_back(std::move(merged));
        }
        
#if DEBUG_LEVEL >= 2
        // Debug: Check output sizes
        size_t total_after = 0;
        for (const auto& run : new_runs) {
            total_after += run.size();
        }
        DEBUG_TRACE("Total entries after merge: %zu", total_after);
#endif
        
        DEBUG_INFO("Merged %zu runs into %zu runs", runs.size(), new_runs.size());
        runs = std::move(new_runs);
    }
}

std::vector<Entry> MergeSortManager::k_way_merge(const std::vector<size_t>& run_indices) {
    size_t k = run_indices.size();
    if (k == 0) {
        return {};
    }
    
    if (k == 1) {
        // Single run, just return it
        return runs[run_indices[0]];
    }
    
    DEBUG_TRACE("Starting k-way merge with k=%zu", k);
    
    // Calculate expected total entries
    size_t expected_total = 0;
    for (size_t idx : run_indices) {
        expected_total += runs[idx].size();
        DEBUG_TRACE("Run %zu has %zu entries", idx, runs[idx].size());
    }
    DEBUG_TRACE("Expected total after merge: %zu", expected_total);
    
    // Set up for ocall handling
    set_as_current();
    
    // Set up index mapping for this merge
    current_merge_indices = run_indices;
    
    // Reset run positions
    run_positions.clear();
    run_positions.resize(runs.size(), 0);
    
    // Initialize merge in enclave
    sgx_status_t retval;
    sgx_status_t status = counted_ecall_k_way_merge_init(eid, &retval, k, (int)comparator_type);
    if (status != SGX_SUCCESS || retval != SGX_SUCCESS) {
        DEBUG_ERROR("ecall_k_way_merge_init failed with status %d, retval %d", status, retval);
        clear_current();
        return {};
    }

    // Collect merged output
    std::vector<Entry> result;
    std::vector<entry_t> output_buffer(MERGE_BUFFER_SIZE);

    int merge_complete = 0;
    while (!merge_complete) {
        size_t output_produced = 0;

        status = counted_ecall_k_way_merge_process(eid, &retval, output_buffer.data(),
                                                    MERGE_BUFFER_SIZE,
                                                    &output_produced, &merge_complete);

        if (status != SGX_SUCCESS) {
            DEBUG_ERROR("ecall_k_way_merge_process failed with status %d", status);
            break;
        }

        // Add produced entries to result
        for (size_t i = 0; i < output_produced; i++) {
            Entry e;
            e.from_entry_t(output_buffer[i]);
            result.push_back(e);
        }

        DEBUG_TRACE("Merge produced %zu entries, complete=%d", output_produced, merge_complete);
    }

    // Clean up merge state
    counted_ecall_k_way_merge_cleanup(eid, &retval);
    clear_current();
    
    DEBUG_TRACE("K-way merge complete, produced %zu entries (expected %zu)", 
                result.size(), expected_total);
    
    if (result.size() != expected_total) {
        DEBUG_ERROR("K-WAY MERGE BUG: Size mismatch! Expected %zu but got %zu (diff: %zd)",
                    expected_total, result.size(), 
                    (ssize_t)result.size() - (ssize_t)expected_total);
    }
    
    return result;
}