#ifndef ALIGN_CONCAT_H
#define ALIGN_CONCAT_H

#include <memory>
#include <vector>
#include "../data_structures/data_structures.h"
#include "../join/join_tree_node.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

/**
 * AlignConcat Class
 * 
 * Implements Phase 4 of the oblivious multi-way band join algorithm.
 * Constructs the final join result by aligning expanded tables and concatenating them.
 * 
 * From the thesis (Section 4.5):
 * "After expansion, tables must be aligned so that matching tuples appear in the same rows.
 * The parent table is sorted by join attributes (and secondarily by other attributes),
 * creating groups of identical tuples. Each group represents a distinct combination from
 * the parent table that will be matched with corresponding child tuples."
 * 
 * The alignment key formula: foreign_sum + (copy_index / local_mult)
 * ensures that every local_mult copies of a child tuple increment to the next parent group,
 * correctly distributing child copies across matching parent groups.
 */
class AlignConcat {
public:
    /**
     * Execute align-concat phase on the join tree
     * Constructs the final join result by aligning and concatenating tables
     * 
     * @param root Root of the join tree with expanded tables
     * @param eid SGX enclave ID for secure operations
     * @return Final join result table
     */
    static Table Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid);
    
    /**
     * Get sorting metrics collected during align-concat phase
     * 
     * @param total_time Total time spent sorting (output)
     * @param total_ecalls Total ecalls for sorting (output)
     * @param acc_time Time spent sorting accumulators (output)
     * @param child_time Time spent sorting children (output)
     * @param acc_ecalls Ecalls for accumulator sorts (output)
     * @param child_ecalls Ecalls for child sorts (output)
     */
    static void GetSortingMetrics(double& total_time, size_t& total_ecalls,
                                  double& acc_time, double& child_time,
                                  size_t& acc_ecalls, size_t& child_ecalls);
    
    /**
     * Reset sorting metrics to zero
     * Should be called before each join execution
     */
    static void ResetSortingMetrics();

private:
    /**
     * Recursively construct join result through the tree
     * Processes tree in pre-order, accumulating results
     * 
     * @param root Current root of subtree
     * @param eid SGX enclave ID
     * @return Accumulated join result for this subtree
     */
    static Table ConstructJoinResult(JoinTreeNodePtr root, sgx_enclave_id_t eid);
    
    /**
     * Align child table with accumulator and concatenate
     * Core operation that aligns two tables and merges them horizontally
     * 
     * Algorithm steps:
     * 1. Sort accumulator by join attr, then other attrs
     * 2. Compute copy indices for child via linear pass
     * 3. Compute alignment keys for child
     * 4. Sort child by alignment key
     * 5. Horizontal concatenation
     * 
     * @param accumulator Current accumulated result (parent)
     * @param child Child table to align and add
     * @param eid SGX enclave ID
     * @return Concatenated result table
     */
    static Table AlignAndConcatenate(const Table& accumulator, 
                                     const Table& child,
                                     sgx_enclave_id_t eid);
    
    /**
     * Pre-order traversal of join tree
     * Visits parent before children for result construction
     * 
     * @param root Root of subtree to traverse
     * @return Nodes in pre-order sequence
     */
    static std::vector<JoinTreeNodePtr> PreOrderTraversal(JoinTreeNodePtr root);
    
    /**
     * Compute copy indices for an expanded table
     * Sets copy_index from 0 to (final_mult-1) for each original tuple
     * 
     * Uses linear pass to track:
     * - Same original_index -> increment copy_index
     * - Different original_index -> reset to 0
     * 
     * @param table Expanded table to process
     * @param eid SGX enclave ID
     * @return Table with copy indices set
     */
    static Table ComputeCopyIndices(const Table& table, sgx_enclave_id_t eid);
    
    /**
     * Compute alignment keys for child table
     * Uses formula: foreign_sum + (copy_index / local_mult)
     * 
     * @param table Table with copy indices set
     * @param eid SGX enclave ID
     * @return Table with alignment keys computed
     */
    static Table ComputeAlignmentKeys(const Table& table, sgx_enclave_id_t eid);
};

#endif // ALIGN_CONCAT_H