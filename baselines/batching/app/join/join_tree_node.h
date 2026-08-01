#ifndef JOIN_TREE_NODE_H
#define JOIN_TREE_NODE_H

#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include "../data_structures/data_structures.h"
#include "join_constraint.h"

/**
 * JoinTreeNode Class
 * 
 * Represents a node in the join tree, where each node corresponds to one table.
 * The tree structure encodes the join order and relationships.
 * 
 * Key concepts from the thesis:
 * - Each node has exactly one table attached
 * - Parent-child relationships represent join conditions
 * - The root node has no parent (and no constraint_with_parent)
 * - Leaf nodes have no children
 * - The tree must be acyclic for the algorithm to work correctly
 * 
 * Algorithm phases operate on this tree:
 * - Bottom-up: Compute local multiplicities from leaves to root
 * - Top-down: Compute foreign multiplicities from root to leaves
 * - Distribute-expand: Expand tables based on multiplicities
 * - Alignment: Align tables for final concatenation
 */

class JoinTreeNode : public std::enable_shared_from_this<JoinTreeNode> {
private:
    // Node identity
    std::string table_name;
    std::string join_column;  // Column used for joining with parent
    
    // The actual table data at this node
    Table table_data;  // Direct member, not pointer
    
    // Tree structure
    std::vector<std::shared_ptr<JoinTreeNode>> children;
    std::weak_ptr<JoinTreeNode> parent;
    
    // Join relationship with parent (empty for root)
    JoinConstraint constraint_with_parent;
    
public:
    // Constructor
    JoinTreeNode(const std::string& name, const Table& table) 
        : table_name(name), table_data(table) {
        table_data.set_table_name(name);
    }
    
    JoinTreeNode(const std::string& name, const std::string& column, const Table& table)
        : table_name(name), join_column(column), table_data(table) {
        table_data.set_table_name(name);
    }
    
    // Tree structure management
    std::shared_ptr<JoinTreeNode> add_child(
        const std::string& child_table_name,
        const Table& child_table,
        const JoinConstraint& constraint) {
        
        auto child = std::make_shared<JoinTreeNode>(child_table_name, child_table);
        child->constraint_with_parent = constraint;
        child->parent = shared_from_this();
        children.push_back(child);
        return child;
    }
    
    std::shared_ptr<JoinTreeNode> add_child(
        std::shared_ptr<JoinTreeNode> child,
        const JoinConstraint& constraint) {
        
        child->constraint_with_parent = constraint;
        child->parent = shared_from_this();
        children.push_back(child);
        return child;
    }
    
    void set_parent(std::weak_ptr<JoinTreeNode> parent_node) {
        parent = parent_node;
    }
    
    // Table data access
    Table& get_table() { return table_data; }
    const Table& get_table() const { return table_data; }
    void set_table(const Table& table) { 
        table_data = table;
        table_data.set_table_name(table_name);
    }
    
    // Tree navigation
    const std::string& get_table_name() const { return table_name; }
    const std::string& get_join_column() const { return join_column; }
    void set_join_column(const std::string& column) { join_column = column; }
    
    std::shared_ptr<JoinTreeNode> get_parent() const { 
        return parent.lock(); 
    }
    
    const std::vector<std::shared_ptr<JoinTreeNode>>& get_children() const { 
        return children; 
    }
    
    std::vector<std::shared_ptr<JoinTreeNode>>& get_children() { 
        return children; 
    }
    
    // Algorithm support - get parameters for joining this node with parent
    ConstraintParam get_constraint_params_with_parent() const {
        if (is_root()) {
            throw std::runtime_error("Root node has no parent constraint");
        }
        return constraint_with_parent.get_params();
    }
    
    const JoinConstraint& get_constraint_with_parent() const {
        return constraint_with_parent;
    }
    
    void set_constraint_with_parent(const JoinConstraint& constraint) {
        constraint_with_parent = constraint;
    }
    
    // Get constraint for specific child
    JoinConstraint get_constraint_with_child(size_t child_index) const {
        if (child_index >= children.size()) {
            throw std::out_of_range("Child index out of range");
        }
        // Child's constraint is from child's perspective (child is source, parent is target)
        // Reverse it to get parent's perspective (parent is source, child is target)
        return children[child_index]->constraint_with_parent.reverse();
    }
    
    // Tree properties
    bool is_root() const { return parent.expired(); }
    bool is_leaf() const { return children.empty(); }
    size_t num_children() const { return children.size(); }
    size_t table_size() const { return table_data.size(); }
    
    // shared_from_this() is now inherited from std::enable_shared_from_this<JoinTreeNode>
    
    // Utility - print tree structure
    void print_tree(int depth = 0) const {
        // Print current node
        for (int i = 0; i < depth; ++i) {
            std::cout << "  ";
        }
        if (depth > 0) {
            std::cout << "└── ";
        }
        
        std::cout << table_name << " (" << table_size() << " rows)";
        
        if (!is_root()) {
            std::cout << " [" << constraint_with_parent.to_string() << "]";
        }
        std::cout << std::endl;
        
        // Print children
        for (const auto& child : children) {
            child->print_tree(depth + 1);
        }
    }
    
    // Collect all table names in subtree
    std::vector<std::string> get_all_table_names() const {
        std::vector<std::string> names;
        names.push_back(table_name);
        
        for (const auto& child : children) {
            auto child_names = child->get_all_table_names();
            names.insert(names.end(), child_names.begin(), child_names.end());
        }
        
        return names;
    }
};

// Typedef for cleaner code
using JoinTreeNodePtr = std::shared_ptr<JoinTreeNode>;

#endif // JOIN_TREE_NODE_H