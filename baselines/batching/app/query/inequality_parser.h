#ifndef INEQUALITY_PARSER_H
#define INEQUALITY_PARSER_H

#include "../join/join_constraint.h"
#include <string>
#include <vector>
#include <regex>

/**
 * InequalityParser - Parses individual SQL inequality conditions into JoinConstraints
 * 
 * Handles patterns like:
 *   - A.x = B.y                      → Equality join
 *   - A.x >= B.y - 100               → Lower bound constraint
 *   - A.x <= B.y + 50                → Upper bound constraint  
 *   - A.x > B.y                      → One-sided range (open)
 *   - A.x < B.y + 10                 → One-sided range (open)
 *   - A.x != B.y                     → Not equal (special handling needed)
 * 
 * Returns one-sided constraints that need to be merged by ConditionMerger
 */
class InequalityParser {
public:
    /**
     * Parse a single inequality condition from SQL WHERE clause
     * Returns true if successful, false if not a valid join condition
     */
    static bool parse(const std::string& condition, JoinConstraint& result);
    
    /**
     * Check if a condition is a join condition (involves two tables)
     */
    static bool is_join_condition(const std::string& condition);
    
    /**
     * Extract table and column from a qualified name (e.g., "supplier.S_NATIONKEY")
     * Returns {table, column} pair
     */
    static std::pair<std::string, std::string> parse_qualified_name(const std::string& name);
    
    /**
     * Parse deviation value from expression (e.g., "B.y + 100" returns 100)
     * Returns 0 if no deviation
     */
    static int32_t parse_deviation(const std::string& expr);
    
private:
    /**
     * Trim whitespace from both ends of a string
     */
    static std::string trim(const std::string& str);
    
    /**
     * Convert operator string to appropriate constraint bounds
     * For example:
     *   ">=" → lower bound with EQ
     *   "<"  → upper bound with NEQ
     */
    static JoinConstraint operator_to_constraint(
        const std::string& left_table,
        const std::string& left_column,
        const std::string& right_table,
        const std::string& right_column,
        const std::string& op,
        int32_t deviation
    );
};

#endif // INEQUALITY_PARSER_H