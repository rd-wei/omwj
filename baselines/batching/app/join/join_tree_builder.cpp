#include "join_tree_builder.h"
#include <queue>
#include <set>
#include <iostream>
#include <stdexcept>

JoinTreeNodePtr JoinTreeBuilder::build_from_query(
    const ParsedQuery& query,
    const std::map<std::string, Table>& tables) {
    
    // Default: use first table as root
    if (query.tables.empty()) {
        throw std::runtime_error("No tables in query");
    }
    
    return build_from_query_with_root(query, tables, query.tables[0]);
}

JoinTreeNodePtr JoinTreeBuilder::build_from_query_with_root(
    const ParsedQuery& query,
    const std::map<std::string, Table>& tables,
    const std::string& root_table) {
    
    // Validate inputs
    if (tables.find(root_table) == tables.end()) {
        throw std::runtime_error("Root table not found in table map: " + root_table);
    }
    
    // Create root node
    auto root = std::make_shared<JoinTreeNode>(root_table, tables.at(root_table));
    
    // Track visited tables
    std::set<std::string> visited;
    visited.insert(root_table);
    
    // Build tree recursively
    build_tree_recursive(root, visited, query.tables, query.join_conditions, tables);
    
    // Validate that all tables are in the tree
    if (visited.size() != query.tables.size()) {
        std::cerr << "Warning: Not all tables connected in join tree. ";
        std::cerr << "Connected: " << visited.size() << "/" << query.tables.size() << std::endl;
        
        // Add disconnected tables as direct children of root (cross product)
        for (const auto& table_name : query.tables) {
            if (visited.find(table_name) == visited.end()) {
                if (tables.find(table_name) != tables.end()) {
                    // Create a default equality constraint (or could use cross product)
                    // For now, we'll skip adding disconnected tables
                    std::cerr << "Skipping disconnected table: " << table_name << std::endl;
                }
            }
        }
    }
    
    return root;
}

void JoinTreeBuilder::build_tree_recursive(
    JoinTreeNodePtr node,
    std::set<std::string>& visited_tables,
    const std::vector<std::string>& all_tables,
    const std::vector<JoinConstraint>& constraints,
    const std::map<std::string, Table>& table_map) {
    
    // Get all tables connected to current node's table
    std::vector<std::string> connected = get_connected_tables(node->get_table_name(), constraints);
    
    for (const auto& connected_table : connected) {
        // Skip if already visited
        if (visited_tables.find(connected_table) != visited_tables.end()) {
            continue;
        }
        
        // Find constraint between current node and connected table
        JoinConstraint constraint;
        bool found = find_constraint_between(
            node->get_table_name(), 
            connected_table, 
            constraints,
            constraint);
        
        if (!found) {
            continue;  // No direct constraint found
        }
        
        // Ensure constraint is from child's perspective (child is source)
        // If current node's table is the source, reverse the constraint
        if (constraint.get_source_table() == node->get_table_name()) {
            constraint = constraint.reverse();
        }
        
        // Check if connected table exists in table map
        if (table_map.find(connected_table) == table_map.end()) {
            std::cerr << "Warning: Table not found in map: " << connected_table << std::endl;
            continue;
        }
        
        // Create child node
        auto child = std::make_shared<JoinTreeNode>(
            connected_table, 
            table_map.at(connected_table));
        
        // Set join column on child (from constraint)
        child->set_join_column(constraint.get_source_column());
        
        // Add as child with constraint
        node->add_child(child, constraint);
        
        // Mark as visited
        visited_tables.insert(connected_table);
        
        // Recursively build subtree
        build_tree_recursive(child, visited_tables, all_tables, constraints, table_map);
    }
}

bool JoinTreeBuilder::find_constraint_between(
    const std::string& table1,
    const std::string& table2,
    const std::vector<JoinConstraint>& constraints,
    JoinConstraint& result) {
    
    for (const auto& constraint : constraints) {
        // Check if this constraint connects the two tables
        if ((constraint.get_source_table() == table1 && constraint.get_target_table() == table2) ||
            (constraint.get_source_table() == table2 && constraint.get_target_table() == table1)) {
            result = constraint;
            return true;
        }
    }
    
    return false;
}

std::vector<std::string> JoinTreeBuilder::get_connected_tables(
    const std::string& table,
    const std::vector<JoinConstraint>& constraints) {
    
    std::vector<std::string> result;
    
    for (const auto& constraint : constraints) {
        if (constraint.get_source_table() == table) {
            result.push_back(constraint.get_target_table());
        } else if (constraint.get_target_table() == table) {
            result.push_back(constraint.get_source_table());
        }
    }
    
    return result;
}

bool JoinTreeBuilder::validate_tree(
    JoinTreeNodePtr root,
    const ParsedQuery& query) {
    
    // Collect all table names in tree
    std::vector<std::string> tree_tables = root->get_all_table_names();
    std::set<std::string> tree_table_set(tree_tables.begin(), tree_tables.end());
    
    // Check if all query tables are in tree
    for (const auto& table : query.tables) {
        if (tree_table_set.find(table) == tree_table_set.end()) {
            std::cerr << "Table missing from tree: " << table << std::endl;
            return false;
        }
    }
    
    // Check if tree has extra tables
    if (tree_tables.size() != query.tables.size()) {
        std::cerr << "Tree has different number of tables than query" << std::endl;
        return false;
    }
    
    return true;
}