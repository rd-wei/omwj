#include "condition_merger.h"
#include <algorithm>

bool ConditionMerger::merge(
    const JoinConstraint& c1,
    const JoinConstraint& c2,
    JoinConstraint& result) {
    
    // Check if conditions can be merged
    if (!can_merge(c1, c2)) {
        return false;
    }
    
    // Extract bounds from both conditions
    int32_t dev1_a = c1.get_deviation1();
    int32_t dev2_a = c1.get_deviation2();
    equality_type_t eq1_a = c1.get_equality1();
    equality_type_t eq2_a = c1.get_equality2();
    
    int32_t dev1_b = c2.get_deviation1();
    int32_t dev2_b = c2.get_deviation2();
    equality_type_t eq1_b = c2.get_equality1();
    equality_type_t eq2_b = c2.get_equality2();
    
    // Compute intersection
    // Lower bound = max of lower bounds
    int32_t new_dev1;
    equality_type_t new_eq1;
    
    if (is_stricter_lower(dev1_a, eq1_a, dev1_b, eq1_b)) {
        new_dev1 = dev1_a;
        new_eq1 = eq1_a;
    } else {
        new_dev1 = dev1_b;
        new_eq1 = eq1_b;
    }
    
    // Upper bound = min of upper bounds
    int32_t new_dev2;
    equality_type_t new_eq2;
    
    if (is_stricter_upper(dev2_a, eq2_a, dev2_b, eq2_b)) {
        new_dev2 = dev2_a;
        new_eq2 = eq2_a;
    } else {
        new_dev2 = dev2_b;
        new_eq2 = eq2_b;
    }
    
    // Check if resulting range is valid
    if (!is_valid_range(new_dev1, new_eq1, new_dev2, new_eq2)) {
        return false;  // Empty intersection
    }
    
    // Create merged constraint
    result = JoinConstraint(
        c1.get_source_table(),
        c1.get_source_column(),
        c1.get_target_table(),
        c1.get_target_column(),
        new_dev1, new_eq1,
        new_dev2, new_eq2
    );
    
    return true;
}

bool ConditionMerger::can_merge(const JoinConstraint& c1, const JoinConstraint& c2) {
    // Same source and target tables and columns
    return c1.get_source_table() == c2.get_source_table() &&
           c1.get_source_column() == c2.get_source_column() &&
           c1.get_target_table() == c2.get_target_table() &&
           c1.get_target_column() == c2.get_target_column();
}

bool ConditionMerger::is_stricter_lower(int32_t dev1, equality_type_t eq1,
                                        int32_t dev2, equality_type_t eq2) {
    // For lower bounds, higher deviation is stricter
    if (dev1 > dev2) {
        return true;
    } else if (dev1 < dev2) {
        return false;
    } else {
        // Equal deviations - NEQ is stricter than EQ for lower bound
        // (a, b] is stricter than [a, b]
        return eq1 == NEQ && eq2 == EQ;
    }
}

bool ConditionMerger::is_stricter_upper(int32_t dev1, equality_type_t eq1,
                                        int32_t dev2, equality_type_t eq2) {
    // For upper bounds, lower deviation is stricter
    if (dev1 < dev2) {
        return true;
    } else if (dev1 > dev2) {
        return false;
    } else {
        // Equal deviations - NEQ is stricter than EQ for upper bound
        // [a, b) is stricter than [a, b]
        return eq1 == NEQ && eq2 == EQ;
    }
}

bool ConditionMerger::is_valid_range(int32_t lower_dev, equality_type_t lower_eq,
                                     int32_t upper_dev, equality_type_t upper_eq) {
    // Check for infinity values
    if (lower_dev == JOIN_ATTR_NEG_INF || upper_dev == JOIN_ATTR_POS_INF) {
        return true;  // Infinite ranges are always valid
    }
    
    // Lower must not exceed upper
    if (lower_dev > upper_dev) {
        return false;
    }
    
    // If equal deviations, check equality types
    if (lower_dev == upper_dev) {
        // Valid if both are EQ (single point) or special infinity case
        // Invalid if one is NEQ (empty interval)
        if (lower_eq == NEQ || upper_eq == NEQ) {
            // Empty interval unless it's an equality join
            return lower_eq == EQ && upper_eq == EQ;
        }
    }
    
    return true;
}