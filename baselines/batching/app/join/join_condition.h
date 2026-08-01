#ifndef APP_JOIN_CONDITION_H
#define APP_JOIN_CONDITION_H

#include <string>
#include <utility>
#include "../data_structures/entry.h"
#include "../../common/types_common.h"

/**
 * Join Condition Class - Represents join predicates as interval constraints
 * 
 * From thesis section 4.1.4 (Join Condition Encoding):
 * "Any join condition between columns can be expressed as an interval constraint.
 * Specifically, a condition between parent column v.join_attr and child column
 * c.join_attr can be parsed as: c.join_attr in v.join_attr + [x, y], where the
 * interval [x, y] may use open or closed boundaries and x, y in R U {±∞}.
 * 
 * Sample join predicates map to intervals as follows:
 * - Equality: v.join_attr = c.join_attr maps to c.join_attr in v.join_attr + [0, 0]
 * - Inequality: v.join_attr > c.join_attr maps to c.join_attr in v.join_attr + (-∞, 0)
 * - Band constraint: v.join_attr >= c.join_attr - 1 maps to c.join_attr in v.join_attr + [-1, ∞)
 * 
 * When multiple conditions constrain the same join, we compute their interval intersection.
 * For instance, combining v.join_attr > c.join_attr (yielding (-∞, 0)) with
 * v.join_attr <= c.join_attr + 1 (yielding [-1, ∞)) produces the final interval [-1, 0).
 */
class JoinCondition {
public:
    // Interval bounds for band join
    struct Bound {
        double deviation;           // Offset from join attribute
        equality_type_t equality;   // EQ for closed, NEQ for open
        
        Bound() : deviation(0.0), equality(NONE) {}
        Bound(double d, equality_type_t e) : deviation(d), equality(e) {}
    };
    
private:
    std::string parent_table;
    std::string child_table;
    std::string parent_column;
    std::string child_column;
    Bound lower_bound;    // Start boundary
    Bound upper_bound;    // End boundary
    
public:
    // Constructors
    JoinCondition();
    JoinCondition(const std::string& parent_tbl, const std::string& child_tbl,
                  const std::string& parent_col, const std::string& child_col,
                  const Bound& lower, const Bound& upper);
    
    // Factory methods for common conditions
    static JoinCondition equality(const std::string& parent_tbl, const std::string& child_tbl,
                                  const std::string& parent_col, const std::string& child_col);
    static JoinCondition band(const std::string& parent_tbl, const std::string& child_tbl,
                             const std::string& parent_col, const std::string& child_col,
                             double lower_offset, double upper_offset,
                             bool lower_inclusive = true, bool upper_inclusive = true);
    
    // Getters
    const Bound& get_lower_bound() const { return lower_bound; }
    const Bound& get_upper_bound() const { return upper_bound; }
    std::string get_parent_table() const { return parent_table; }
    std::string get_child_table() const { return child_table; }
    std::string get_parent_column() const { return parent_column; }
    std::string get_child_column() const { return child_column; }
    
    // Apply condition to create START/END entries
    std::pair<Entry, Entry> create_boundary_entries(const Entry& target_entry) const;
};

#endif // APP_JOIN_CONDITION_H