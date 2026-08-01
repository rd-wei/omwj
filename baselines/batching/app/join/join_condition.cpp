#include "join_condition.h"

JoinCondition::JoinCondition() {
}

JoinCondition::JoinCondition(const std::string& parent_tbl, const std::string& child_tbl,
                           const std::string& parent_col, const std::string& child_col,
                           const Bound& lower, const Bound& upper)
    : parent_table(parent_tbl), child_table(child_tbl),
      parent_column(parent_col), child_column(child_col),
      lower_bound(lower), upper_bound(upper) {
}

JoinCondition JoinCondition::equality(const std::string& parent_tbl, const std::string& child_tbl,
                                     const std::string& parent_col, const std::string& child_col) {
    return JoinCondition(parent_tbl, child_tbl, parent_col, child_col,
                        Bound(0.0, EQ), Bound(0.0, EQ));
}

JoinCondition JoinCondition::band(const std::string& parent_tbl, const std::string& child_tbl,
                                 const std::string& parent_col, const std::string& child_col,
                                 double lower_offset, double upper_offset,
                                 bool lower_inclusive, bool upper_inclusive) {
    equality_type_t lower_eq = lower_inclusive ? EQ : NEQ;
    equality_type_t upper_eq = upper_inclusive ? EQ : NEQ;
    return JoinCondition(parent_tbl, child_tbl, parent_col, child_col,
                        Bound(lower_offset, lower_eq), Bound(upper_offset, upper_eq));
}

std::pair<Entry, Entry> JoinCondition::create_boundary_entries(const Entry& target_entry) const {
    Entry start_entry = target_entry;
    Entry end_entry = target_entry;
    
    // Set field types
    start_entry.field_type = START;
    end_entry.field_type = END;
    
    // Apply bounds
    start_entry.join_attr += static_cast<int32_t>(lower_bound.deviation);
    start_entry.equality_type = lower_bound.equality;
    
    end_entry.join_attr += static_cast<int32_t>(upper_bound.deviation);
    end_entry.equality_type = upper_bound.equality;
    
    return std::make_pair(start_entry, end_entry);
}