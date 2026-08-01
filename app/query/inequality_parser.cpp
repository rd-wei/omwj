#include "inequality_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

std::string InequalityParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::pair<std::string, std::string> InequalityParser::parse_qualified_name(const std::string& name) {
    std::string trimmed = trim(name);
    size_t dot_pos = trimmed.find('.');
    
    if (dot_pos == std::string::npos) {
        // No table qualifier - should not happen in proper SQL
        return {"", trimmed};
    }
    
    std::string table = trimmed.substr(0, dot_pos);
    std::string column = trimmed.substr(dot_pos + 1);
    
    return {trim(table), trim(column)};
}

int32_t InequalityParser::parse_deviation(const std::string& expr) {
    std::string trimmed = trim(expr);
    
    // Look for + or - in the expression
    size_t plus_pos = trimmed.find('+');
    size_t minus_pos = trimmed.find('-');
    
    if (plus_pos == std::string::npos && minus_pos == std::string::npos) {
        // No deviation
        return 0;
    }
    
    // Find the operator position
    size_t op_pos;
    int sign = 1;
    
    if (plus_pos != std::string::npos) {
        op_pos = plus_pos;
        sign = 1;
    } else {
        op_pos = minus_pos;
        sign = -1;
    }
    
    // Extract the numeric part after the operator
    std::string num_str = trim(trimmed.substr(op_pos + 1));
    
    // Parse the number
    try {
        int32_t value = std::stoi(num_str);
        return sign * value;
    } catch (...) {
        // If parsing fails, assume no deviation
        return 0;
    }
}

bool InequalityParser::is_join_condition(const std::string& condition) {
    // A join condition must have at least two table.column references
    // Look for pattern: table1.col1 op table2.col2 [+/- value]
    
    std::string trimmed = trim(condition);
    
    // Count dots (.) to identify qualified column names
    int dot_count = 0;
    for (char c : trimmed) {
        if (c == '.') dot_count++;
    }
    
    // Need at least 2 qualified names for a join
    return dot_count >= 2;
}

JoinConstraint InequalityParser::operator_to_constraint(
    const std::string& left_table,
    const std::string& left_column,
    const std::string& right_table,
    const std::string& right_column,
    const std::string& op,
    int32_t deviation) {
    
    // left.col OP right.col + deviation
    // We need to express as: left.col IN [lower, upper]
    // where bounds are relative to right.col
    
    if (op == "=") {
        // Equality: left.col = right.col + deviation
        // → left.col IN [right.col + deviation, right.col + deviation]
        return JoinConstraint(
            left_table, left_column,
            right_table, right_column,
            deviation, EQ,
            deviation, EQ
        );
    }
    else if (op == ">=") {
        // left.col >= right.col + deviation
        // → left.col IN [right.col + deviation, +∞)
        return JoinConstraint(
            left_table, left_column,
            right_table, right_column,
            deviation, EQ,
            JOIN_ATTR_POS_INF, NEQ  // Infinity is always NEQ
        );
    }
    else if (op == ">") {
        // left.col > right.col + deviation
        // → left.col IN (right.col + deviation, +∞)
        return JoinConstraint(
            left_table, left_column,
            right_table, right_column,
            deviation, NEQ,  // Open lower bound
            JOIN_ATTR_POS_INF, NEQ  // Infinity is always NEQ
        );
    }
    else if (op == "<=") {
        // left.col <= right.col + deviation
        // → left.col IN (-∞, right.col + deviation]
        return JoinConstraint(
            left_table, left_column,
            right_table, right_column,
            JOIN_ATTR_NEG_INF, NEQ,  // Infinity is always NEQ
            deviation, EQ
        );
    }
    else if (op == "<") {
        // left.col < right.col + deviation
        // → left.col IN (-∞, right.col + deviation)
        return JoinConstraint(
            left_table, left_column,
            right_table, right_column,
            JOIN_ATTR_NEG_INF, NEQ,  // Infinity is always NEQ
            deviation, NEQ  // Open upper bound
        );
    }
    else if (op == "!=" || op == "<>") {
        // Not equal - this is tricky, might need special handling
        // For now, treat as invalid (return empty range)
        return JoinConstraint(
            left_table, left_column,
            right_table, right_column,
            1, EQ,
            0, EQ  // Invalid range [1, 0]
        );
    }
    
    // Unknown operator - return equality as default
    return JoinConstraint::equality(left_table, left_column, right_table, right_column);
}

bool InequalityParser::parse(const std::string& condition, JoinConstraint& result) {
    if (!is_join_condition(condition)) {
        return false;
    }
    
    std::string trimmed = trim(condition);
    
    // Find the comparison operator
    // Check for two-character operators first
    std::string op;
    size_t op_pos = std::string::npos;
    
    std::vector<std::string> operators = {"<=", ">=", "!=", "<>", "=", "<", ">"};
    
    for (const auto& test_op : operators) {
        size_t pos = trimmed.find(test_op);
        if (pos != std::string::npos) {
            op = test_op;
            op_pos = pos;
            break;
        }
    }
    
    if (op_pos == std::string::npos) {
        // No operator found
        return false;
    }
    
    // Split into left and right parts
    std::string left_part = trim(trimmed.substr(0, op_pos));
    std::string right_part = trim(trimmed.substr(op_pos + op.length()));
    
    // Parse left side (should be table.column)
    auto left_parsed = parse_qualified_name(left_part);
    const std::string& left_table = left_parsed.first;
    const std::string& left_column = left_parsed.second;
    if (left_table.empty() || left_column.empty()) {
        return false;
    }
    
    // Parse right side (table.column [+/- value])
    // First, extract the table.column part
    std::string right_qualified;
    
    // Find where the deviation starts (if any)
    size_t dev_start = right_part.find('+');
    if (dev_start == std::string::npos) {
        dev_start = right_part.find('-');
    }
    
    if (dev_start != std::string::npos) {
        // Check if this minus is part of the column name or a deviation
        // Look for a dot before the minus to distinguish
        size_t last_dot = right_part.rfind('.', dev_start);
        if (last_dot != std::string::npos) {
            // The - or + comes after a qualified name
            right_qualified = trim(right_part.substr(0, dev_start));
        } else {
            // No clear qualified name, might be malformed
            right_qualified = right_part;
            dev_start = std::string::npos;
        }
    } else {
        right_qualified = right_part;
    }
    
    auto right_parsed = parse_qualified_name(right_qualified);
    const std::string& right_table = right_parsed.first;
    const std::string& right_column = right_parsed.second;
    if (right_table.empty() || right_column.empty()) {
        return false;
    }
    
    // Parse deviation from the right part
    int32_t deviation = parse_deviation(right_part);
    
    // Create the constraint based on the operator
    result = operator_to_constraint(
        left_table, left_column,
        right_table, right_column,
        op, deviation
    );
    
    return true;
}