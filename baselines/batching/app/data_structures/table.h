#ifndef APP_TABLE_H
#define APP_TABLE_H

#include <vector>
#include <string>
#include <functional>
#include "entry.h"
#include "sgx_urts.h"
#include "../../common/batch_types.h"

/**
 * Table Class - Manages collections of entries for oblivious multi-way band join
 * 
 * Provides batched operations for efficient SGX processing
 * that maintain fixed access patterns for secure computation in SGX enclaves.
 */
class Table {
private:
    std::vector<Entry> entries;
    std::string table_name;
    size_t num_columns;
    std::vector<std::string> schema_column_names;  // NEW: Table's schema for slim mode
    
public:
    // Constructor - requires schema
    Table(const std::string& name, const std::vector<std::string>& schema);
    
    // Entry management
    void add_entry(const Entry& entry);
    Entry& get_entry(size_t index);
    const Entry& get_entry(size_t index) const;
    void set_entry(size_t index, const Entry& entry);
    Entry& operator[](size_t index);
    const Entry& operator[](size_t index) const;
    size_t size() const;
    void clear();
    
    // Batch operations
    void set_all_field_type(entry_type_t type);
    void initialize_original_indices();
    void initialize_leaf_multiplicities();
    
    // Conversion for SGX processing
    std::vector<entry_t> to_entry_t_vector() const;
    void from_entry_t_vector(const std::vector<entry_t>& c_entries);
    
    // Table metadata
    void set_table_name(const std::string& name);
    std::string get_table_name() const;
    void set_num_columns(size_t n);
    size_t get_num_columns() const;
    
    // Schema management (for slim mode migration)
    void set_schema(const std::vector<std::string>& columns);
    std::vector<std::string> get_schema() const;
    size_t get_column_index(const std::string& col_name) const;
    bool has_column(const std::string& col_name) const;
    
    // Generate generic schema for combined tables
    static std::vector<std::string> generate_generic_schema(size_t num_columns);
    
    // Named attribute access using schema
    int32_t get_attribute(size_t row, const std::string& col_name) const;
    void set_attribute(size_t row, const std::string& col_name, int32_t value);
    
    // Iterator support
    std::vector<Entry>::iterator begin();
    std::vector<Entry>::iterator end();
    std::vector<Entry>::const_iterator begin() const;
    std::vector<Entry>::const_iterator end() const;
    
    // Encryption status
    enum EncryptionStatus {
        UNENCRYPTED,  // All entries have is_encrypted = false
        ENCRYPTED,    // All entries have is_encrypted = true
        MIXED         // Entries have different encryption states
    };
    EncryptionStatus get_encryption_status() const;
    
    // Note: Non-batched operations have been removed in favor of batched versions
    // which are more efficient for SGX by reducing ecall overhead
    
    // ========================================================================
    // Batched Operations - Reduce SGX overhead by batching ecalls
    // ========================================================================
    
    /**
     * BatchedMap: Apply transformation to each entry with batched ecalls
     * @param eid SGX enclave ID
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     * @return New table with transformed entries
     */
    Table batched_map(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params = nullptr) const;
    
    /**
     * BatchedLinearPass: Apply window function with batched ecalls
     * @param eid SGX enclave ID
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     */
    void batched_linear_pass(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params = nullptr);
    
    /**
     * BatchedParallelPass: Apply function to aligned pairs with batched ecalls
     * @param other Second table (must have same size)
     * @param eid SGX enclave ID
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     */
    void batched_parallel_pass(Table& other, sgx_enclave_id_t eid, OpEcall op_type, int32_t* params = nullptr);
    
    
    /**
     * ShuffleMergeSort: Two-phase oblivious sort
     * Phase 1: Oblivious shuffle using ShuffleManager (Waksman network)
     * Phase 2: Non-oblivious merge sort using MergeSortManager
     * @param eid SGX enclave ID
     * @param op_type Comparator operation type
     */
    void shuffle_merge_sort(sgx_enclave_id_t eid, OpEcall op_type);
    
    /**
     * BatchedDistributePass: Batched version of distribute_pass
     * @param eid SGX enclave ID
     * @param distance Distance between pairs to process
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     */
    void batched_distribute_pass(sgx_enclave_id_t eid, size_t distance, OpEcall op_type, int32_t* params = nullptr);
    
    /**
     * AddBatchedPadding: Add multiple padding entries using batched ecalls
     * @param count Number of padding entries to add
     * @param eid SGX enclave ID
     * @param encryption_status Encryption status to match table
     */
    void add_batched_padding(size_t count, sgx_enclave_id_t eid, uint8_t encryption_status, OpEcall padding_op = OP_ECALL_TRANSFORM_CREATE_DIST_PADDING);
    
    /**
     * PadToShuffleSize: Pad table to 2^a * k^b format for shuffle operations
     * @param eid SGX enclave ID
     */
    void pad_to_shuffle_size(sgx_enclave_id_t eid);
    
    /**
     * CalculateShufflePadding: Calculate target size for shuffle (2^a * k^b)
     * @param n Current size
     * @return Target padded size
     */
    static size_t calculate_shuffle_padding(size_t n);
    
    /**
     * IsValidShuffleSize: Check if size is valid 2^a * k^b format
     * @param n Size to check
     * @return true if valid shuffle size
     */
    static bool is_valid_shuffle_size(size_t n);
    
private:
    // Helper methods
    static void check_sgx_status(sgx_status_t status, const std::string& operation);
    
    // Helper to calculate next power of 2
    static size_t next_power_of_two(size_t n) {
        size_t power = 1;
        while (power < n) power *= 2;
        return power;
    }
};

#endif // APP_TABLE_H