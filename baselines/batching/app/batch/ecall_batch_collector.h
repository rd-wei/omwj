#ifndef ECALL_BATCH_COLLECTOR_H
#define ECALL_BATCH_COLLECTOR_H

#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include "sgx_eid.h"
#include "sgx_error.h"
#include "batch_types.h"
#include "types_common.h"
#include "../data_structures/entry.h"

// Statistics for performance monitoring
struct BatchStats {
    size_t total_operations;
    size_t total_flushes;
    size_t total_entries_processed;
    size_t max_batch_size_reached;
};

/**
 * EcallBatchCollector - Batches multiple ecall operations to reduce SGX overhead
 * 
 * This class collects operations that would normally be individual ecalls
 * and batches them into a single ecall to the enclave. This dramatically
 * reduces the overhead of SGX transitions.
 * 
 * Features:
 * - Automatic deduplication of entries
 * - Auto-flush at configurable batch size
 * - Manual flush capability
 * - RAII - destructor ensures pending operations are flushed
 * 
 * Usage:
 *   EcallBatchCollector collector(eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
 *   for (...) {
 *     collector.add_operation(entry1, entry2);  // Automatically flushes at batch size
 *   }
 *   collector.flush();  // Manual flush of remaining operations
 */
class EcallBatchCollector {
private:
    // Forward mapping: Entry pointer -> batch array index
    std::unordered_map<Entry*, int32_t> entry_map;
    
    // Reverse mapping: batch array index -> Entry pointer (for write-back)
    std::vector<Entry*> entry_pointers;
    
    // Batch data to send to enclave (converted to entry_t format)
    std::vector<entry_t> batch_data;
    
    // Operations to execute in the enclave
    std::vector<BatchOperation> operations;
    
    // Configuration
    sgx_enclave_id_t eid;
    OpEcall op_type;
    size_t max_batch_size;
    
    // Statistics for performance monitoring
    BatchStats stats;
    
    // Helper to check SGX status
    void check_sgx_status(sgx_status_t status, const std::string& operation);
    
public:
    /**
     * Constructor
     * @param eid SGX enclave ID
     * @param op Operation type to batch
     * @param max_size Maximum batch size before auto-flush (default: MAX_BATCH_SIZE)
     */
    EcallBatchCollector(sgx_enclave_id_t eid, OpEcall op, size_t max_size = MAX_BATCH_SIZE);
    
    /**
     * Add a two-parameter operation to the batch
     * @param e1 First entry
     * @param e2 Second entry
     * @param params Optional parameters array (up to 4 values)
     */
    void add_operation(Entry& e1, Entry& e2, int32_t* params = nullptr);
    
    /**
     * Add a single-parameter operation to the batch
     * @param e Entry to operate on
     * @param params Optional parameters array (up to 4 values)
     */
    void add_operation(Entry& e, int32_t* params = nullptr);
    
    
    /**
     * Execute all pending operations
     * Called automatically when batch is full or on destruction
     */
    void flush();
    
    /**
     * Write back results from batch_data to original Entry objects
     * Called after batch dispatcher modifies the data
     */
    void write_back_results();
    
    /**
     * Check if batch needs flushing
     * @return true if batch is at or above max_batch_size
     */
    bool needs_flush() const { 
        return operations.size() >= max_batch_size; 
    }
    
    /**
     * Get current batch size
     * @return Number of operations currently in batch
     */
    size_t get_batch_size() const { 
        return operations.size(); 
    }
    
    /**
     * Get statistics about batch performance
     * @return BatchStats structure with performance metrics
     */
    const BatchStats& get_stats() const { 
        return stats; 
    }
    
    /**
     * Reset statistics
     */
    void reset_stats() {
        stats = BatchStats{0, 0, 0, 0};
    }
    
    /**
     * Destructor - ensures any pending operations are flushed
     */
    ~EcallBatchCollector();
};

#endif // ECALL_BATCH_COLLECTOR_H