#ifndef TOP_DOWN_PHASE_H
#define TOP_DOWN_PHASE_H

#include "../join/join_tree_node.h"
#include "../data_structures/table.h"
#include "../join/join_constraint.h"
#include "sgx_eid.h"
#include <vector>

/**
 * Top-Down Phase of the oblivious multi-way join algorithm
 * 
 * After the bottom-up phase computes local multiplicities,
 * this phase propagates foreign multiplicities from root to leaves.
 * 
 * The final multiplicities (final_mult) represent the actual
 * number of times each tuple appears in the final join result.
 */
class TopDownPhase {
public:
    /**
     * Execute the top-down phase on the join tree
     * @param root Root of the join tree (with local_mult already computed)
     * @param eid Enclave ID for secure operations
     */
    static void Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid);

private:
    /**
     * Initialize root table for top-down phase
     * Sets final_mult = local_mult for root entries only
     * @param node Root node to initialize
     * @param eid Enclave ID
     */
    static void InitializeRootTable(JoinTreeNodePtr node, sgx_enclave_id_t eid);
    
    /**
     * Initialize foreign fields for a table
     * Sets foreign_sum = 0, local_weight = 0
     * @param node Node to initialize
     * @param eid Enclave ID
     */
    static void InitializeForeignFields(JoinTreeNodePtr node, sgx_enclave_id_t eid);
    
    /**
     * Create combined table for foreign computation
     * Similar to bottom-up but uses final_mult instead of local_mult
     * @param parent Parent table (with final_mult set)
     * @param child Child table (to receive foreign multiplicities)
     * @param constraint Join constraint between parent and child
     * @param eid Enclave ID
     * @return Combined table with SOURCE, START, END entries
     */
    static Table CombineTableForForeign(
        const Table& parent,
        const Table& child,
        const JoinConstraint& constraint,
        sgx_enclave_id_t eid);
    
    /**
     * Compute foreign multiplicities for child table
     * Updates child's final_mult based on parent's propagation
     * @param parent Parent table (already has final_mult)
     * @param child Child table (will receive final_mult)
     * @param constraint Join constraint
     * @param eid Enclave ID
     */
    static void ComputeForeignMultiplicities(
        Table& parent,
        Table& child,
        const JoinConstraint& constraint,
        sgx_enclave_id_t eid);
    
    /**
     * Pre-order traversal of join tree (root to leaves)
     * @param root Root node
     * @return Nodes in pre-order (root first, then children)
     */
    static std::vector<JoinTreeNodePtr> PreOrderTraversal(JoinTreeNodePtr root);
};

#endif // TOP_DOWN_PHASE_H