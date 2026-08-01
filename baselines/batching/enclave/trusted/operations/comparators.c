#include "../enclave_types.h"
#include "../crypto/crypto_helpers.h"
#include <stdint.h>
#include <string.h>

/**
 * Comparator functions for oblivious sorting
 * All comparators use oblivious (branchless) operations to prevent
 * information leakage through memory access patterns
 * 
 * NOTE: Unlike the thesis algorithms which return -1/0/1, our implementation
 * directly performs oblivious swaps in-place. This is more efficient for 
 * oblivious execution as it avoids branching in the sorting algorithm.
 * The oblivious_swap always executes (with a mask determining if values 
 * actually change), maintaining constant memory access patterns.
 * 
 * All comparators follow the pattern:
 * 1. Compute comparison result
 * 2. Call oblivious_swap(e1, e2, should_swap) where should_swap = (e1 > e2)
 */

/**
 * Helper: Oblivious ternary functions
 * Returns condition * true_val + (1 - condition) * false_val
 * Ensures branchless execution for constant-time operations
 */
static inline int32_t oblivious_ternary(int32_t condition, int32_t true_val, int32_t false_val) {
    return condition * true_val + (1 - condition) * false_val;
}

static inline uint8_t oblivious_ternary_u8(int32_t condition, uint8_t true_val, uint8_t false_val) {
    return (uint8_t)(condition * true_val + (1 - condition) * false_val);
}

/**
 * Helper: Oblivious sign function
 * Returns -1, 0, or 1 based on value comparison
 */
static inline int32_t oblivious_sign(int32_t val) {
    return (val > 0) - (val < 0);
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
    
    // If both are padding or both are not, use normal result
    // If e1 is padding and e2 is not, e1 > e2 (return 1)
    // If e2 is padding and e1 is not, e1 < e2 (return -1)
    int32_t both_not_padding = (1 - is_padding1) & (1 - is_padding2);
    
    return both_not_padding * normal_result + 
           (1 - both_not_padding) * (is_padding1 - is_padding2);
}

/**
 * Helper: Get precedence for entry type combination (Algorithm 513)
 * Precedence ordering ensures correct join semantics
 */
static inline int32_t get_precedence(entry_type_t field_type, equality_type_t equality_type) {
    // Correct precedence table for proper boundary handling:
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
 * Oblivious swap function for entry_t
 * Swaps two entries if should_swap is non-zero
 */
void oblivious_swap(entry_t* e1, entry_t* e2, int should_swap) {
    // Create mask: all 1s if should_swap, all 0s otherwise
    uint8_t mask = oblivious_ternary_u8((should_swap != 0), 0xFF, 0x00);

    // XOR swap with mask
    uint8_t* p1 = (uint8_t*)e1;
    uint8_t* p2 = (uint8_t*)e2;

    for (size_t i = 0; i < sizeof(entry_t); i++) {
        uint8_t diff = (p1[i] ^ p2[i]) & mask;
        p1[i] ^= diff;
        p2[i] ^= diff;
    }
}

/**
 * Core operation for comparator by join attribute (Algorithm 399)
 * Primary: join_attr, Secondary: entry type precedence
 */
void comparator_join_attr_op(entry_t* e1, entry_t* e2) {
    // Compare join attributes (both are int32_t)
    int32_t diff = e1->join_attr - e2->join_attr;
    int32_t cmp = oblivious_sign(diff);
    
    // Check if equal
    int32_t is_equal = (cmp == 0);
    
    // Get precedence values
    int32_t p1 = get_precedence(e1->field_type, e1->equality_type);
    int32_t p2 = get_precedence(e2->field_type, e2->equality_type);
    int32_t prec_cmp = oblivious_sign(p1 - p2);
    
    // Combine: use join_attr comparison if not equal, else use precedence
    int32_t normal_result = (1 - is_equal) * cmp + is_equal * prec_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by join attribute with decrypt/encrypt wrapper
 */
void comparator_join_attr(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_join_attr_op);
}

/**
 * Core operation for comparator for pairwise processing (Algorithm 438)
 * Priority: 1) TARGET before SOURCE, 2) by original_index, 3) START before END
 */
void comparator_pairwise_op(entry_t* e1, entry_t* e2) {
    // Check if entries are TARGET type (START or END)
    int32_t is_target1 = ((e1->field_type == START) | (e1->field_type == END));
    int32_t is_target2 = ((e2->field_type == START) | (e2->field_type == END));
    
    // Priority 1: TARGET entries before SOURCE
    int32_t type_priority = is_target2 - is_target1;  // Negative if e1 is target
    
    // Priority 2: Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Priority 3: START before END for same index
    int32_t is_start1 = (e1->field_type == START);
    int32_t is_start2 = (e2->field_type == START);
    int32_t start_first = is_start2 - is_start1;  // Positive if e1 is START and e2 is END (need swap to put START first)
    
    // Check if priorities are equal at each level
    int32_t same_priority = (type_priority == 0);
    int32_t same_index = (idx_cmp == 0);
    
    // Combine priorities hierarchically
    int32_t normal_result = (1 - same_priority) * type_priority +
                           same_priority * (1 - same_index) * idx_cmp +
                           same_priority * same_index * start_first;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator for pairwise processing with decrypt/encrypt wrapper
 */
void comparator_pairwise(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_pairwise_op);
}

/**
 * Core operation for comparator with END entries first (Algorithm 479)
 * Priority: 1) END before others, 2) by original_index
 */
void comparator_end_first_op(entry_t* e1, entry_t* e2) {
    // Check if entries are END type
    int32_t is_end1 = (e1->field_type == END);
    int32_t is_end2 = (e2->field_type == END);
    
    // Priority 1: END entries before all others
    int32_t type_priority = is_end2 - is_end1;  // Negative if e1 is END
    
    // Priority 2: Compare by original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Check if type priority is equal
    int32_t same_type = (type_priority == 0);
    
    // Combine priorities
    int32_t normal_result = (1 - same_type) * type_priority + same_type * idx_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator with END entries first with decrypt/encrypt wrapper
 */
void comparator_end_first(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_end_first_op);
}

/**
 * Core operation for comparator by join attribute then all attributes (Algorithm 697)
 * Primary: join_attr, Secondary: all attributes in sequence
 */
void comparator_join_then_other_op(entry_t* e1, entry_t* e2) {
    // Compare join attributes (both are int32_t)
    int32_t diff = e1->join_attr - e2->join_attr;
    int32_t cmp = oblivious_sign(diff);
    
    // If join attributes are not equal, use that comparison
    int32_t join_not_equal = (cmp != 0);
    
    // If join attributes are equal, compare all attributes
    int32_t attr_cmp = 0;
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        int32_t attr_diff = e1->attributes[i] - e2->attributes[i];
        int32_t curr_cmp = oblivious_sign(attr_diff);
        
        // Only update attr_cmp if we haven't found a difference yet
        int32_t is_first_diff = (attr_cmp == 0) & (curr_cmp != 0);
        attr_cmp = attr_cmp + is_first_diff * curr_cmp;
    }
    
    // Combine: use join_attr comparison if not equal, else use attributes comparison
    int32_t normal_result = join_not_equal * cmp + (1 - join_not_equal) * attr_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by join attribute then all attributes with decrypt/encrypt wrapper
 */
void comparator_join_then_other(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_join_then_other_op);
}

/**
 * Core operation for comparator by original index only
 * Simple comparison for maintaining original order
 */
void comparator_original_index_op(entry_t* e1, entry_t* e2) {
    // Compare original indices
    int32_t normal_result = oblivious_sign(e1->original_index - e2->original_index);
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by original index only with decrypt/encrypt wrapper
 */
void comparator_original_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_original_index_op);
}

/**
 * Core operation for comparator by alignment key (Algorithm 715)
 * Used in final alignment phase
 * Priority: 1) alignment_key, 2) join_attr, 3) copy_index
 */
void comparator_alignment_key_op(entry_t* e1, entry_t* e2) {
    // Primary: Compare alignment keys
    int32_t align_cmp = oblivious_sign(e1->alignment_key - e2->alignment_key);
    
    // Check if alignment keys are equal
    int32_t align_equal = (align_cmp == 0);
    
    // Tie-breaker 1: Compare join attributes
    int32_t join_cmp = oblivious_sign(e1->join_attr - e2->join_attr);
    
    // Check if join attributes are also equal
    int32_t join_equal = (join_cmp == 0);
    
    // Tie-breaker 2: Compare copy indices
    int32_t copy_cmp = oblivious_sign(e1->copy_index - e2->copy_index);
    
    // Combine: use alignment_key if not equal, else use join_attr, else use copy_index
    int32_t normal_result = (1 - align_equal) * align_cmp + 
                           align_equal * (1 - join_equal) * join_cmp +
                           align_equal * join_equal * copy_cmp;
    
    // Adjust for SORT_PADDING entries
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator by alignment key with decrypt/encrypt wrapper
 */
void comparator_alignment_key(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_alignment_key_op);
}

/**
 * Core operation for comparator to put DIST_PADDING entries last
 * Sorts non-padding entries before padding entries
 */
void comparator_padding_last_op(entry_t* e1, entry_t* e2) {
    // Check if entries are DIST_PADDING
    int32_t is_padding1 = (e1->field_type == DIST_PADDING);
    int32_t is_padding2 = (e2->field_type == DIST_PADDING);
    
    // Priority 1: Non-padding before padding
    int32_t type_priority = is_padding1 - is_padding2;  // Positive if e1 is padding
    
    // Priority 2: By original index
    int32_t idx_cmp = oblivious_sign(e1->original_index - e2->original_index);
    
    // Check if type priority is equal
    int32_t same_type = (type_priority == 0);
    
    // Combine priorities
    int32_t normal_result = (1 - same_type) * type_priority + same_type * idx_cmp;
    
    // Adjust for SORT_PADDING entries (different from DIST_PADDING)
    int32_t result = adjust_for_padding(e1, e2, normal_result);
    
    // Swap if e1 > e2
    oblivious_swap(e1, e2, result > 0);
}

/**
 * Comparator to put DIST_PADDING entries last with decrypt/encrypt wrapper
 */
void comparator_padding_last(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_padding_last_op);
}

/**
 * Core operation for comparator for distribution phase
 * Checks if e1's dst_idx >= e2's index AND e1 is not DIST_PADDING
 * Swaps content while preserving index
 */
void comparator_distribute_op(entry_t* e1, entry_t* e2) {
    // Check if we should swap: e1.dst_idx >= e2.index AND e1 is not DIST_PADDING
    int32_t dst_condition = (e1->dst_idx >= e2->index);
    int32_t not_padding = (e1->field_type != DIST_PADDING);
    int32_t should_swap = dst_condition & not_padding;
    
    // Perform oblivious swap of content while preserving index field
    // Save original indices
    int32_t idx1 = e1->index;
    int32_t idx2 = e2->index;
    
    // Swap entire entries
    oblivious_swap(e1, e2, should_swap);
    
    // Restore original indices (always, obliviously)
    e1->index = idx1;
    e2->index = idx2;
}

/**
 * Comparator for distribution phase with decrypt/encrypt wrapper
 */
void comparator_distribute(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, comparator_distribute_op);
}

