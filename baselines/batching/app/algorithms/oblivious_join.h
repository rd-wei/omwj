#ifndef OBLIVIOUS_JOIN_H
#define OBLIVIOUS_JOIN_H

#include <memory>
#include <vector>
#include "../data_structures/data_structures.h"
#include "../join/join_tree_node.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

/**
 * ObliviousJoin Class
 * 
 * Orchestrates the complete oblivious multi-way band join algorithm.
 * Combines all four phases to produce the final join result:
 * 
 * 1. Bottom-Up Phase: Compute local multiplicities
 * 2. Top-Down Phase: Compute final multiplicities  
 * 3. Distribute-Expand Phase: Replicate tuples
 * 4. Align-Concat Phase: Construct final result
 * 
 * From the thesis:
 * "The algorithm proceeds in four phases, each performing a specific
 * transformation on the join tree and its associated tables. The phases
 * work together to compute multiplicities, replicate tuples, and align
 * them to produce the correct join result while maintaining oblivious
 * access patterns throughout."
 */
class ObliviousJoin {
public:
    /**
     * Execute the complete oblivious join algorithm
     * 
     * @param root Root of the join tree with input tables
     * @param eid SGX enclave ID for secure operations
     * @return Final join result table (encrypted)
     */
    static Table Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid);
    
    /**
     * Execute join with debug session
     * Same as Execute but creates a debug session for intermediate results
     * 
     * @param root Root of the join tree with input tables
     * @param eid SGX enclave ID for secure operations
     * @param session_name Name for debug session
     * @return Final join result table (encrypted)
     */
    static Table ExecuteWithDebug(JoinTreeNodePtr root, 
                                   sgx_enclave_id_t eid,
                                   const std::string& session_name);

private:
    /**
     * Validate join tree structure
     * Ensures tree is properly formed with valid constraints
     * 
     * @param root Root of the join tree
     * @return true if valid, false otherwise
     */
    static bool ValidateJoinTree(JoinTreeNodePtr root);
    
    /**
     * Log join tree structure for debugging
     * 
     * @param root Root of the join tree
     * @param level Indentation level
     */
    static void LogJoinTree(JoinTreeNodePtr root, int level = 0);
    
    /**
     * Get statistics about the join operation
     * 
     * @param root Root of the join tree
     * @return String with statistics (table sizes, multiplicities, etc.)
     */
    static std::string GetJoinStatistics(JoinTreeNodePtr root);
};

#endif // OBLIVIOUS_JOIN_H