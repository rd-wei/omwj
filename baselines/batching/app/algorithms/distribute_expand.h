#ifndef DISTRIBUTE_EXPAND_H
#define DISTRIBUTE_EXPAND_H

#include "../join/join_tree_node.h"
#include "../data_structures/table.h"
#include "sgx_eid.h"
#include <vector>

/**
 * Distribute-Expand Phase of the oblivious multi-way join algorithm
 * 
 * After computing final multiplicities, this phase expands each table
 * by creating exactly final_mult copies of each tuple.
 * 
 * The algorithm uses two main operations:
 * 1. Distribute: Place each tuple at its destination index
 * 2. Expand: Fill gaps with copies using linear pass
 * 
 * Based on the Krastnikov et al. ODBJ paper's distribute-expand technique.
 */
class DistributeExpand {
public:
    /**
     * Execute the distribute-expand phase on all tables in the join tree
     * @param root Root of the join tree with final_mult computed
     * @param eid Enclave ID for secure operations
     */
    static void Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid);

private:
    /**
     * Expand a single table according to final multiplicities
     * @param table Input table with final_mult values
     * @param eid Enclave ID
     * @return Expanded table with each tuple replicated final_mult times
     */
    static Table ExpandSingleTable(const Table& table, sgx_enclave_id_t eid);
    
    /**
     * Compute the output size for expansion
     * @param table Table with dst_idx computed
     * @param eid Enclave ID
     * @return Total size after expansion
     */
    static size_t ComputeOutputSize(const Table& table, sgx_enclave_id_t eid);
    
    /**
     * Perform the distribution phase using variable-distance passes
     * @param table Table with entries and padding
     * @param output_size Target size
     * @param eid Enclave ID
     */
    static void DistributePhase(Table& table, size_t output_size, sgx_enclave_id_t eid);
    
    /**
     * Perform the expansion phase to fill gaps
     * @param table Distributed table
     * @param eid Enclave ID
     */
    static void ExpansionPhase(Table& table, sgx_enclave_id_t eid);
    
    /**
     * Get all nodes from the join tree in pre-order
     * @param root Root node
     * @return Vector of all nodes
     */
    static std::vector<JoinTreeNodePtr> GetAllNodes(JoinTreeNodePtr root);
};

#endif // DISTRIBUTE_EXPAND_H