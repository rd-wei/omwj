#ifndef JOIN_TREE_BUILDER_H
#define JOIN_TREE_BUILDER_H

#include <memory>
#include <map>
#include <vector>
#include <string>
#include <set>
#include "../query/parsed_query.h"
#include "join_tree_node.h"
#include "../data_structures/data_structures.h"

/**
 * JoinTreeBuilder - Constructs a join tree from a parsed SQL query
 * 
 * Takes a ParsedQuery and tables, builds a JoinTreeNode hierarchy that
 * represents the join structure. The tree encodes join order and constraints.
 * 
 * Algorithm:
 * 1. Select a root table (heuristic: first table or largest)
 * 2. Build tree by connecting tables based on join constraints
 * 3. Ensure all tables are connected (spanning tree)
 * 4. Set up parent-child constraints
 */
class JoinTreeBuilder {
private:
    /**
     * Find which join constraint connects two tables
     * Returns true if found, false otherwise
     */
    bool find_constraint_between(
        const std::string& table1,
        const std::string& table2,
        const std::vector<JoinConstraint>& constraints,
        JoinConstraint& result);
    
    /**
     * Build tree recursively from a node
     */
    void build_tree_recursive(
        JoinTreeNodePtr node,
        std::set<std::string>& visited_tables,
        const std::vector<std::string>& all_tables,
        const std::vector<JoinConstraint>& constraints,
        const std::map<std::string, Table>& table_map);
    
    /**
     * Get all tables connected to a given table via join constraints
     */
    std::vector<std::string> get_connected_tables(
        const std::string& table,
        const std::vector<JoinConstraint>& constraints);
    
public:
    /**
     * Build join tree from parsed query and table data
     * 
     * @param query Parsed SQL query with tables and join conditions
     * @param tables Map from table name to Table object with data
     * @return Root node of the constructed join tree
     */
    JoinTreeNodePtr build_from_query(
        const ParsedQuery& query,
        const std::map<std::string, Table>& tables);
    
    /**
     * Build join tree with specified root table
     * 
     * @param query Parsed SQL query
     * @param tables Table data
     * @param root_table Name of table to use as root
     * @return Root node of join tree
     */
    JoinTreeNodePtr build_from_query_with_root(
        const ParsedQuery& query,
        const std::map<std::string, Table>& tables,
        const std::string& root_table);
    
    /**
     * Validate that the join tree covers all tables and constraints
     */
    bool validate_tree(
        JoinTreeNodePtr root,
        const ParsedQuery& query);
};

#endif // JOIN_TREE_BUILDER_H