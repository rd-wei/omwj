#ifndef BOTTOM_UP_PHASE_H
#define BOTTOM_UP_PHASE_H

#include <memory>
#include <vector>
#include "../data_structures/data_structures.h"
#include "../join/join_tree_node.h"
#include "../join/join_constraint.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

/**
 * BottomUpPhase Class
 * 
 * Implements Phase 1 of the oblivious multi-way band join algorithm.
 * Computes local multiplicities (α_local) by traversing the join tree in post-order.
 * 
 * From the thesis (Section 4.3):
 * "The bottom-up phase computes local multiplicities by traversing the join tree T 
 * in post-order. For leaf nodes, we initialize each tuple t ∈ R_leaf with t.local_mult = 1.
 * For non-leaf nodes, the algorithm processes each parent-child pair (v, c) where v is 
 * the parent and c ∈ children(v)."
 * 
 * Key property after completion:
 * For each tuple t in table R_v at node v, t.local_mult equals the number of times
 * t appears in the join result of the subtree rooted at v.
 */
class BottomUpPhase {
public:
    /**
     * Execute bottom-up phase on the join tree
     * Modifies tables in place to set local_mult values
     * 
     * @param root Root of the join tree
     * @param eid SGX enclave ID for secure operations
     */
    static void Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid);

private:
    /**
     * Initialize all tables with metadata columns
     * Adds metadata fields and sets original indices
     * 
     * @param node Current node in tree traversal
     * @param eid SGX enclave ID
     */
    static void InitializeAllTables(JoinTreeNodePtr node, sgx_enclave_id_t eid);
    
    /**
     * Compute local multiplicities for one parent-child pair
     * Updates parent's local_mult based on matching child tuples
     * 
     * @param parent Parent table (target in dual-entry technique)
     * @param child Child table (source in dual-entry technique)
     * @param constraint Join constraint between parent and child
     * @param eid SGX enclave ID
     */
    static void ComputeLocalMultiplicities(
        Table& parent, 
        Table& child,
        const JoinConstraint& constraint,
        sgx_enclave_id_t eid);
    
    /**
     * Create combined table with START/END boundary entries
     * Implements the dual-entry technique for band joins
     * 
     * From the thesis (Algorithm 360):
     * Creates two boundary markers for each target tuple that mark where 
     * the matching range begins and ends, then combines with source tuples.
     * 
     * @param target Target table (parent)
     * @param source Source table (child)
     * @param constraint Join constraint with deviation and equality parameters
     * @param eid SGX enclave ID
     * @return Combined table with SOURCE, START, and END entries
     */
    static Table CombineTable(
        const Table& target,
        const Table& source, 
        const JoinConstraint& constraint,
        sgx_enclave_id_t eid);
    
    /**
     * Post-order traversal of join tree
     * Visits children before parents to compute multiplicities bottom-up
     * 
     * @param root Root of subtree to traverse
     * @return Nodes in post-order sequence
     */
    static std::vector<JoinTreeNodePtr> PostOrderTraversal(JoinTreeNodePtr root);
};

#endif // BOTTOM_UP_PHASE_H