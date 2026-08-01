#ifndef APP_TYPES_H
#define APP_TYPES_H

/**
 * Types Convenience Header
 * 
 * This header provides backward compatibility by including all data structure headers.
 * Classes are now separated into their own files for better organization:
 * - Entry class: entry.h/cpp
 * - Table class: table.h/cpp
 * - JoinCondition class: join_condition.h/cpp
 */

/**
 * Table Type Definitions and Schema Evolution
 * 
 * From thesis section 4.1.2 (Table Type Definitions):
 * "Following Krastnikov et al.'s terminology, we distinguish between different types
 * of tables based on their state in the algorithm:
 * 
 * - INPUT TABLES: Original unmodified tables {R1, R2, ..., Rk} as provided to the algorithm
 * - AUGMENTED TABLES: Input tables extended with persistent multiplicity metadata
 * - COMBINED TABLES: Arrays of entries from multiple augmented tables with temporary metadata,
 *   sorted by join attribute for dual-entry processing
 * - EXPANDED TABLES: Augmented tables where each tuple appears exactly final_mult times
 * - ALIGNED TABLES: Expanded tables reordered to enable correct concatenation for join result
 * 
 * Table Schema Evolution (from Table 4.1):
 * +----------+------------------+------------------+------------------+
 * | Type     | Original Attrs   | Persistent Meta  | Temporary Meta   |
 * +----------+------------------+------------------+------------------+
 * | R_input  | {a1, a2, ..., an}| No              | No               |
 * | R_aug    | {a1, a2, ..., an}| Yes             | No               |
 * | R_comb   | {type,a,data}    | Yes             | Yes              |
 * | R_exp    | {a1, a2, ..., an}| Yes             | No               |
 * | R_align  | {a1, a2, ..., an}| Yes             | No               |
 * +----------+------------------+------------------+------------------+
 * 
 * Persistent Meta: field_index, local_mult, final_mult, foreign_sum
 * Temporary Meta: local_cumsum OR foreign_sum (not both simultaneously)
 * 
 * Note: Combined tables have a special dual-entry structure where original
 * attributes are transformed into {field_type, join_attr, field_data} format"
 */

// Include all data structure headers
#include "entry.h"
#include "table.h"
#include "../join/join_condition.h"

#endif // APP_TYPES_H