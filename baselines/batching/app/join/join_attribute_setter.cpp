#include "join_attribute_setter.h"
#include "Enclave_u.h"
#include "batch_types.h"
#include <stdexcept>
#include <sstream>

void JoinAttributeSetter::SetJoinAttributesForTree(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    DEBUG_DEBUG("Setting join attributes for tree rooted at %s", root->get_table_name().c_str());
    
    // Process current node
    SetJoinAttributesForNode(root, eid);
    
    // Recursively process children
    for (auto& child : root->get_children()) {
        SetJoinAttributesForTree(child, eid);
    }
}

void JoinAttributeSetter::SetJoinAttributesForNode(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    std::string join_column = node->get_join_column();
    
    // Root node might not have a join column set
    if (join_column.empty() && !node->is_root()) {
        DEBUG_WARN("Node %s has no join column set", node->get_table_name().c_str());
        return;
    }
    
    // For root, we might need to set join column from first constraint
    if (join_column.empty() && node->is_root() && !node->get_children().empty()) {
        // Get the first child's constraint
        const JoinConstraint& child_constraint = node->get_children()[0]->get_constraint_with_parent();
        join_column = child_constraint.get_target_column();
        node->set_join_column(join_column);
        DEBUG_INFO("Set root node %s join column to %s", 
                   node->get_table_name().c_str(), join_column.c_str());
    }
    
    // If still no join column, skip
    if (join_column.empty()) {
        DEBUG_DEBUG("Node %s has no join column, skipping", node->get_table_name().c_str());
        return;
    }
    
    // Get the table and update each entry
    Table& table = node->get_table();
    DEBUG_INFO("Setting join_attr for %zu entries in %s using column %s",
               table.size(), node->get_table_name().c_str(), join_column.c_str());
    
    // Get column index from table schema
    if (table.size() == 0) {
        DEBUG_WARN("Table %s is empty, cannot set join attributes", node->get_table_name().c_str());
        return;
    }
    
    // Get column index from table schema
    int32_t column_index = -1;
    try {
        size_t idx = table.get_column_index(join_column);
        column_index = static_cast<int32_t>(idx);
        DEBUG_DEBUG("Found column %s at index %d using table schema", join_column.c_str(), column_index);
    } catch (const std::runtime_error& e) {
        DEBUG_ERROR("Column %s not found in table %s: %s", 
                    join_column.c_str(), node->get_table_name().c_str(), e.what());
        return;
    }
    
    DEBUG_INFO("Column %s is at index %d", join_column.c_str(), column_index);
    
    // Use batched operation to set join_attr for all entries
    int32_t params[4] = {column_index, 0, 0, 0};
    Table updated = table.batched_map(eid, OP_ECALL_TRANSFORM_SET_JOIN_ATTR, params);
    
    // Replace the table's entries with the updated ones
    table = updated;
    
    // Debug: Print first entry's join_attr after update
    if (table.size() > 0) {
        DEBUG_DEBUG("First entry after: join_attr=%d from column %s (index %d)",
                   table[0].join_attr, join_column.c_str(), column_index);
        DEBUG_INFO("First entry attributes for verification:");
        std::vector<std::string> schema = table.get_schema();
        for (size_t j = 0; j < schema.size() && j < MAX_ATTRIBUTES; j++) {
            DEBUG_INFO("  attr[%zu]=%d (column: %s)", j, table[0].attributes[j],
                      schema[j].c_str());
        }
    }
}

void JoinAttributeSetter::SetJoinAttributesForTable(Table& table, const std::string& column_name, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        DEBUG_WARN("Table is empty, cannot set join attributes");
        return;
    }
    
    // Get column index from table schema
    int32_t column_index = -1;
    
    try {
        size_t idx = table.get_column_index(column_name);
        column_index = static_cast<int32_t>(idx);
        DEBUG_DEBUG("Found column %s at index %d using table schema", column_name.c_str(), column_index);
    } catch (const std::runtime_error& e) {
        DEBUG_ERROR("Column %s not found in table schema: %s", column_name.c_str(), e.what());
        return;
    }
    
    DEBUG_INFO("Setting join_attr for %zu entries using column %s (index %d)",
               table.size(), column_name.c_str(), column_index);
    
    // Use batched operation to set join_attr for all entries
    int32_t params[4] = {column_index, 0, 0, 0};
    Table updated = table.batched_map(eid, OP_ECALL_TRANSFORM_SET_JOIN_ATTR, params);
    
    // Replace the table's entries with the updated ones
    table = updated;
}