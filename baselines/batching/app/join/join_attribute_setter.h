#ifndef JOIN_ATTRIBUTE_SETTER_H
#define JOIN_ATTRIBUTE_SETTER_H

#include <memory>
#include <string>
#include "join_tree_node.h"
#include "../../common/debug_util.h"
#include "sgx_urts.h"

/**
 * JoinAttributeSetter - Sets join_attr field for all entries in the join tree
 * 
 * After the join tree is built, each node knows its join column name,
 * but the Entry objects don't have their join_attr values set.
 * This utility populates join_attr from the appropriate column data.
 * 
 * IMPORTANT: For encrypted data, this uses ecalls to decrypt, set, and re-encrypt.
 */
class JoinAttributeSetter {
public:
    /**
     * Set join attributes for entire tree (DEPRECATED - use SetJoinAttributesForTable)
     * @param root The root of the join tree
     * @param eid Enclave ID for encrypted data operations
     */
    static void SetJoinAttributesForTree(JoinTreeNodePtr root, sgx_enclave_id_t eid);
    
    /**
     * Set join attributes for a specific table using a specific column
     * @param table The table to update
     * @param column_name The column to use as join_attr
     * @param eid Enclave ID for encrypted data operations
     */
    static void SetJoinAttributesForTable(Table& table, const std::string& column_name, sgx_enclave_id_t eid);

private:
    /**
     * Set join attributes for a single node
     * @param node The node to process
     * @param eid Enclave ID for encrypted data operations
     */
    static void SetJoinAttributesForNode(JoinTreeNodePtr node, sgx_enclave_id_t eid);
    
};

#endif // JOIN_ATTRIBUTE_SETTER_H