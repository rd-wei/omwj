#include "../enclave_types.h"
#include "../../../common/comparator_convention.h"
#include "../../../common/batch_types.h"
#include <stdint.h>

/**
 * Oblivious boolean comparator functions for merge sort
 * 
 * These comparators return 1 if e1 < e2, 0 otherwise.
 * They use branchless arithmetic operations to maintain oblivious execution.
 * No data-dependent branches are used to prevent information leakage.
 * 
 * Each comparator extracts logic from the oblivious compare-and-swap versions
 * but returns the comparison result instead of performing swaps.
 */

/**
 * Helper: Oblivious sign function
 * Returns -1, 0, or 1 based on value comparison
 * Uses arithmetic operations only, no branches
 */
static inline int32_t oblivious_sign(int32_t val) {
    return (val > 0) - (val < 0);
}

/**
 * Helper: Get precedence for entry type combination
 * Same logic as in comparators.c, already oblivious
 */
static inline int32_t get_precedence(entry_type_t field_type, equality_type_t equality_type) {
    // Precedence ordering for correct join semantics:
    // (END, NEQ) -> 1    // Open end: exclude boundary, comes first
    // (START, EQ) -> 1   // Closed start: include boundary, comes first
    // (SOURCE, _) -> 2   // Source entries in middle
    // (END, EQ) -> 3     // Closed end: include boundary, comes last
    // (START, NEQ) -> 3  // Open start: exclude boundary, comes last
    
    int32_t is_start_neq = ((field_type == START) & (equality_type == NEQ));
    int32_t is_end_eq = ((field_type == END) & (equality_type == EQ));
    int32_t is_source = (field_type == SOURCE);
    int32_t is_start_eq = ((field_type == START) & (equality_type == EQ));
    int32_t is_end_neq = ((field_type == END) & (equality_type == NEQ));
    
    return 1 * (is_end_neq | is_start_eq) + 
           2 * is_source + 
           3 * (is_end_eq | is_start_neq);
}

/**
 * Helper: Adjust comparison result for SORT_PADDING entries
 * SORT_PADDING entries always sort to the end (are "larger")
 * Returns the final comparison result accounting for padding
 */
static inline int32_t adjust_for_padding(entry_t* e1, entry_t* e2, int32_t normal_result) {
    // Check if entries are SORT_PADDING
    int32_t is_padding1 = (e1->field_type == SORT_PADDING);
    int32_t is_padding2 = (e2->field_type == SORT_PADDING);
    
    // Calculate adjustments
    // If e1 is padding and e2 is not: e1 > e2 (return 1)
    // If e2 is padding and e1 is not: e1 < e2 (return -1)
    // Otherwise: use normal_result
    int32_t adjustment = is_padding1 - is_padding2;
    
    // If both or neither are padding (adjustment == 0), use normal_result
    // Otherwise use adjustment
    int32_t use_normal = (adjustment == 0);
    return use_normal * normal_result + (1 - use_normal) * adjustment;
}

/**
 * Compare by join attribute
 * Returns 1 if e1 < e2, 0 otherwise
 */
int compare_join_attr(entry_t* e1, entry_t* e2) {
    // Compare join attributes obliviously
    int32_t diff = e1->join_attr - e2->join_attr;
    int32_t cmp = oblivious_sign(diff);
    
    // Check if equal (without branching)
    int32_t is_equal = (cmp == 0);
    
    // Get precedence values
    int32_t p1 = get_precedence(e1->field_type, e1->equality_type);
    int32_t p2 = get_precedence(e2->field_type, e2->equality_type);
    int32_t prec_cmp = oblivious_sign(p1 - p2);
    
    // Combine: use join_attr comparison if not equal, else use precedence
    int32_t normal_result = (1 - is_equal) * cmp + is_equal * prec_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare for pairwise processing
 * Priority: 1) TARGET before SOURCE, 2) by original_index, 3) START before END
 */
int compare_pairwise(entry_t* e1, entry_t* e2) {
    // Check if entries are TARGET type (START or END) - oblivious
    int32_t is_target1 = ((e1->field_type == START) | (e1->field_type == END));
    int32_t is_target2 = ((e2->field_type == START) | (e2->field_type == END));
    
    // Priority 1: TARGET entries before SOURCE
    int32_t type_cmp = is_target2 - is_target1;  // Negative if e1 is TARGET
    
    // Priority 2: Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Priority 3: START before END for same index
    int32_t is_start1 = (e1->field_type == START);
    int32_t is_start2 = (e2->field_type == START);
    int32_t start_cmp = is_start2 - is_start1;  // Negative if e1 is START
    
    // Check if type priority is equal
    int32_t same_type = (type_cmp == 0);
    
    // Check if index is equal
    int32_t same_idx = (idx_cmp == 0);
    
    // Combine priorities obliviously
    int32_t priority2_result = same_idx * start_cmp + (1 - same_idx) * idx_cmp;
    int32_t normal_result = same_type * priority2_result + (1 - same_type) * type_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare with END entries first
 * Priority: 1) END before others, 2) by original_index
 */
int compare_end_first(entry_t* e1, entry_t* e2) {
    // Check if entries are END type - oblivious
    int32_t is_end1 = (e1->field_type == END);
    int32_t is_end2 = (e2->field_type == END);
    
    // Priority 1: END entries before all others
    int32_t type_cmp = is_end2 - is_end1;  // Negative if e1 is END
    
    // Priority 2: Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Check if type priority is equal
    int32_t same_type = (type_cmp == 0);
    
    // Combine priorities
    int32_t normal_result = same_type * idx_cmp + (1 - same_type) * type_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare by join attr then other attributes
 * Used for final output sorting in align phase
 */
int compare_join_then_other(entry_t* e1, entry_t* e2) {
    // Primary: join_attr comparison
    int32_t join_cmp = oblivious_sign(e1->join_attr - e2->join_attr);
    
    // Secondary: Compare attributes lexicographically
    // We need to compare all attributes obliviously
    int32_t attr_cmp = 0;
    int32_t found_diff = 0;
    
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        int32_t this_cmp = oblivious_sign(e1->attributes[i] - e2->attributes[i]);
        // Only use this comparison if we haven't found a difference yet
        attr_cmp = found_diff * attr_cmp + (1 - found_diff) * this_cmp;
        // Mark if we found a difference (obliviously)
        found_diff = found_diff | (this_cmp != 0);
    }
    
    // Use join_attr if different, else use attributes
    int32_t join_equal = (join_cmp == 0);
    int32_t normal_result = (1 - join_equal) * join_cmp + join_equal * attr_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare by original index
 */
int compare_original_index(entry_t* e1, entry_t* e2) {
    // Simple comparison by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, idx_cmp);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare by alignment key
 */
int compare_alignment_key(entry_t* e1, entry_t* e2) {
    // Simple comparison by alignment key
    int32_t key_cmp = oblivious_sign(e1->alignment_key - e2->alignment_key);
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, key_cmp);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare with padding last
 * SORT_PADDING and DIST_PADDING go to end
 */
int compare_padding_last(entry_t* e1, entry_t* e2) {
    // Check for padding types obliviously
    int32_t is_dist_padding1 = (e1->field_type == DIST_PADDING);
    int32_t is_dist_padding2 = (e2->field_type == DIST_PADDING);
    
    // Priority 1: Non-padding before padding
    int32_t type_priority = is_dist_padding1 - is_dist_padding2;  // Positive if e1 is padding
    
    // Priority 2: By original index  
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Check if type priority is equal
    int32_t same_type = (type_priority == 0);
    
    // Combine priorities
    int32_t normal_result = (1 - same_type) * type_priority + same_type * idx_cmp;
    
    // Adjust for SORT_PADDING entries (different from DIST_PADDING)
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Compare for distribute phase
 * Sort by dst_idx
 */
int compare_distribute(entry_t* e1, entry_t* e2) {
    // Simple comparison by dst_idx
    int32_t dst_cmp = oblivious_sign(e1->dst_idx - e2->dst_idx);
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, dst_cmp);
    
    // Return 1 if e1 < e2, 0 otherwise
    // Note: result > 0 means e1 > e2 (should swap) in the compare-and-swap logic
    // So we return 1 when result < 0 (e1 < e2)
    return (result < 0);
}

/**
 * Get comparator function by type
 * These comparators follow standard convention: return 1 if e1 < e2, 0 otherwise
 */
comparator_func_t get_merge_comparator(OpEcall type) {
    switch(type) {
        case OP_ECALL_COMPARATOR_JOIN_ATTR:
            return compare_join_attr;
        case OP_ECALL_COMPARATOR_PAIRWISE:
            return compare_pairwise;
        case OP_ECALL_COMPARATOR_END_FIRST:
            return compare_end_first;
        case OP_ECALL_COMPARATOR_JOIN_THEN_OTHER:
            return compare_join_then_other;
        case OP_ECALL_COMPARATOR_ORIGINAL_INDEX:
            return compare_original_index;
        case OP_ECALL_COMPARATOR_ALIGNMENT_KEY:
            return compare_alignment_key;
        case OP_ECALL_COMPARATOR_PADDING_LAST:
            return compare_padding_last;
        case OP_ECALL_COMPARATOR_DISTRIBUTE:
            return compare_distribute;
        default:
            return compare_join_attr;  // Default fallback
    }
}