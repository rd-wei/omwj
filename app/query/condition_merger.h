#ifndef CONDITION_MERGER_H
#define CONDITION_MERGER_H

#include "../join/join_constraint.h"
#include <string>

/**
 * ConditionMerger - Merges multiple join conditions on the same column pair
 * 
 * When a query has multiple inequalities between the same columns, we need
 * to compute their intersection to get the final constraint.
 * 
 * Example:
 *   A.x >= B.y - 100  →  A.x IN [B.y-100, +∞]
 *   A.x <= B.y + 1000 →  A.x IN [-∞, B.y+1000]
 *   Merged: A.x IN [B.y-100, B.y+1000]
 */
class ConditionMerger {
public:
    /**
     * Merge two join conditions on the same column pair by computing intersection
     * Returns empty optional if the intersection is empty (invalid)
     * 
     * Rules:
     * - Lower bound = max(c1.lower, c2.lower)
     * - Upper bound = min(c1.upper, c2.upper)
     * - For equal deviations, use stricter equality (NEQ > EQ)
     */
    static bool merge(
        const JoinConstraint& c1,
        const JoinConstraint& c2,
        JoinConstraint& result);
    
    /**
     * Check if two conditions can be merged (same tables and columns)
     */
    static bool can_merge(const JoinConstraint& c1, const JoinConstraint& c2);
    
    /**
     * Helper to compare bounds and select the stricter one
     * Returns true if bound1 is stricter (should be used)
     */
    static bool is_stricter_lower(int32_t dev1, equality_type_t eq1,
                                  int32_t dev2, equality_type_t eq2);
    
    static bool is_stricter_upper(int32_t dev1, equality_type_t eq1,
                                  int32_t dev2, equality_type_t eq2);
    
    /**
     * Check if the resulting range is valid (lower <= upper with proper equality)
     */
    static bool is_valid_range(int32_t lower_dev, equality_type_t lower_eq,
                               int32_t upper_dev, equality_type_t upper_eq);
};

#endif // CONDITION_MERGER_H