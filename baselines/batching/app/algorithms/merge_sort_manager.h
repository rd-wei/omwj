#ifndef MERGE_SORT_MANAGER_H
#define MERGE_SORT_MANAGER_H

#include <vector>
#include <memory>
#include "../data_structures/table.h"
#include "../data_structures/entry.h"
#include "../../common/batch_types.h"
#include "../../common/constants.h"
#include "sgx_eid.h"
#include "sgx_error.h"

/**
 * MergeSortManager - Manages non-oblivious k-way merge sort
 * 
 * This class implements external merge sort where:
 * - External memory: Entry vector outside enclave
 * - Internal memory: Enclave's entry_t array
 * 
 * Algorithm:
 * 1. Create sorted runs using heap sort in enclave
 * 2. Merge runs using k-way merge
 * 3. Recursively merge until one sorted result
 */
class MergeSortManager {
private:
    sgx_enclave_id_t eid;
    OpEcall comparator_type;
    
    // Current runs being merged
    std::vector<std::vector<Entry>> runs;
    std::vector<size_t> run_positions;  // Current position in each run
    std::vector<size_t> current_merge_indices;  // Maps enclave buffer idx to actual run idx
    
    // Static instance for ocall handlers
    static MergeSortManager* current_instance;
    
    // No friend needed - ocall_refill_buffer will access via static function
    
public:
    MergeSortManager(sgx_enclave_id_t eid, OpEcall type);
    ~MergeSortManager();
    
    // Main sort function
    void sort(Table& table);
    
    // Ocall handler - must be static
    static void handle_refill_buffer(int buffer_idx, entry_t* buffer, 
                                     size_t buffer_size, size_t* actual_filled);
    
private:
    // Phase 1: Create initial sorted runs
    void create_sorted_runs(Table& table);
    
    // Phase 2: Merge runs recursively
    void merge_runs_recursive();
    
    // Helper: Merge k runs into one
    std::vector<Entry> k_way_merge(const std::vector<size_t>& run_indices);
    
    // Helper: Sort a single run in enclave
    void sort_run_in_enclave(std::vector<Entry>& entries);
    
    // Set current instance for ocall handling
    void set_as_current();
    void clear_current();
};

#endif // MERGE_SORT_MANAGER_H