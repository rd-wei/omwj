#ifndef PARSED_QUERY_H
#define PARSED_QUERY_H

#include <string>
#include <vector>
#include <sstream>
#include "../join/join_constraint.h"

/**
 * ParsedQuery - Represents a parsed SQL query
 * 
 * Holds the structured representation of a SQL query after parsing.
 * Currently focused on SELECT * queries with joins (TPC-H style).
 */
struct ParsedQuery {
    // SELECT clause
    std::vector<std::string> select_columns;  // ["*"] for SELECT *
    
    // FROM clause
    std::vector<std::string> tables;  // Table names
    
    // WHERE clause - separated into joins and filters
    std::vector<JoinConstraint> join_conditions;  // Join constraints (possibly merged)
    std::vector<std::string> filter_conditions;   // Non-join conditions (not yet supported)
    
    // Utility methods
    bool is_select_star() const {
        return select_columns.size() == 1 && select_columns[0] == "*";
    }
    
    size_t num_tables() const {
        return tables.size();
    }
    
    size_t num_joins() const {
        return join_conditions.size();
    }
    
    std::string to_string() const {
        std::stringstream ss;
        
        // SELECT
        ss << "SELECT ";
        for (size_t i = 0; i < select_columns.size(); i++) {
            if (i > 0) ss << ", ";
            ss << select_columns[i];
        }
        ss << "\n";
        
        // FROM
        ss << "FROM ";
        for (size_t i = 0; i < tables.size(); i++) {
            if (i > 0) ss << ", ";
            ss << tables[i];
        }
        ss << "\n";
        
        // WHERE
        if (!join_conditions.empty() || !filter_conditions.empty()) {
            ss << "WHERE ";
            bool first = true;
            
            // Join conditions
            for (const auto& join : join_conditions) {
                if (!first) ss << "\n  AND ";
                first = false;
                ss << join.to_string();
            }
            
            // Filter conditions (if any)
            for (const auto& filter : filter_conditions) {
                if (!first) ss << "\n  AND ";
                first = false;
                ss << filter;
            }
        }
        
        return ss.str();
    }
    
    /**
     * Validate the parsed query
     */
    bool is_valid() const {
        // Must have at least one table
        if (tables.empty()) return false;
        
        // Must have SELECT clause
        if (select_columns.empty()) return false;
        
        // If multiple tables, should have join conditions
        // (Not strictly enforced as could be cross product)
        
        return true;
    }
    
    /**
     * Clear all fields
     */
    void clear() {
        select_columns.clear();
        tables.clear();
        join_conditions.clear();
        filter_conditions.clear();
    }
};

#endif // PARSED_QUERY_H