#include "../enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "../crypto/crypto_helpers.h"
#include "../../common/debug_util.h"
#include <stdint.h>

/**
 * Window functions for oblivious processing
 * Using crypto helpers to handle decrypt-modify-encrypt pattern
 * All functions are implemented with oblivious (branchless) operations
 * to prevent information leakage through memory access patterns
 */

// Operation functions for window operations
void window_set_original_index_op(entry_t* e1, entry_t* e2) {
    e2->original_index = e1->original_index + 1;
}

/**
 * Set original index for window processing
 * Sets consecutive indices as the window slides through the table
 */
void window_set_original_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_set_original_index_op);
}

void update_target_multiplicity_op(entry_t* source, entry_t* target) {
    // Multiply target's local_mult by computed interval from source (combined table)
    target->local_mult = target->local_mult * source->local_interval;
    
    // Debug: Check for negative multiplicity
    // Note: Cannot use DEBUG_ENCLAVE in enclave code - would need OCALL for logging
}

/**
 * Update target multiplicity (Algorithm 503)
 * Pure arithmetic - already oblivious
 * Parameters: source (with intervals), target (to update)
 */
void update_target_multiplicity(entry_t* source, entry_t* target) {
    apply_to_decrypted_pair(source, target, update_target_multiplicity_op);
}

void update_target_final_multiplicity_op(entry_t* source, entry_t* target) {
    // Propagate foreign intervals from source to compute target's final multiplicities
    target->final_mult = source->foreign_interval * target->local_mult;
    target->foreign_sum = source->foreign_sum;  // For alignment
}

/**
 * Update target final multiplicity (Algorithm 623)
 * Pure arithmetic - already oblivious
 * Parameters: source (with foreign intervals), target (to update)
 */
void update_target_final_multiplicity(entry_t* source, entry_t* target) {
    apply_to_decrypted_pair(source, target, update_target_final_multiplicity_op);
}

void window_compute_local_sum_op(entry_t* e1, entry_t* e2) {
    // Check if e2 (window[1]) is SOURCE type (obliviously)
    int32_t is_source = (e2->field_type == SOURCE);
    
    // Check if e1 is START with NEQ (open boundary)
    // If so, SOURCE at the same join_attr should NOT be counted
    int32_t is_start_neq = (e1->field_type == START) & (e1->equality_type == NEQ);
    int32_t same_join_attr = (e1->join_attr == e2->join_attr);
    int32_t skip_source = is_start_neq & same_join_attr & is_source;
    
    // Add local_mult only if SOURCE and not skipped
    uint32_t old_cumsum __attribute__((unused)) = (uint32_t)e2->local_cumsum;
    e2->local_cumsum = e1->local_cumsum + (is_source * (1 - skip_source) * e2->local_mult);
    
    // Debug log to verify the operation
    DEBUG_DEBUG("compute_local_sum: e1(type=%d,eq=%d,join=%d,cumsum=%u) e2(type=%d,mult=%u,join=%d) skip=%d result=%u->%u",
        e1->field_type, e1->equality_type, e1->join_attr, e1->local_cumsum, 
        e2->field_type, e2->local_mult, e2->join_attr,
        skip_source, old_cumsum, e2->local_cumsum);
}

/**
 * Window compute local sum (Algorithm 424)
 * Oblivious conversion: use arithmetic instead of branching
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_local_sum(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_compute_local_sum_op);
}

void window_compute_local_interval_op(entry_t* e1, entry_t* e2) {
    // Check if we have a START/END pair (obliviously)
    int32_t is_start = (e1->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_pair = is_start * is_end;
    
    // Compute interval difference
    int32_t interval = e2->local_cumsum - e1->local_cumsum;
    uint32_t old_interval __attribute__((unused)) = (uint32_t)e2->local_interval;
    
    // Debug: Check for negative interval
    // Note: Cannot use DEBUG_ENCLAVE in enclave code - would need OCALL for logging
    
    // Set interval only if we have a pair, otherwise preserve existing value
    e2->local_interval = (is_pair * interval) + ((1 - is_pair) * e2->local_interval);
    
    // Debug log
    DEBUG_DEBUG("compute_interval: e1(type=%d,cumsum=%u) e2(type=%d,cumsum=%u) is_pair=%d interval=%d result=%u->%u",
        e1->field_type, e1->local_cumsum,
        e2->field_type, e2->local_cumsum,
        is_pair, interval, old_interval, e2->local_interval);
}

/**
 * Window compute local interval (Algorithm 467)
 * Oblivious conversion: use conditional arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_local_interval(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_compute_local_interval_op);
}

void window_compute_foreign_sum_op(entry_t* e1, entry_t* e2) {
    // Determine entry type (obliviously)
    int32_t is_start = (e2->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_source = (e2->field_type == SOURCE);
    
    // For START/END entries from child, use local_mult
    // For SOURCE entries from parent, use final_mult
    // Note: mult_to_use removed as it's not used in the oblivious computation
    
    // Check if e1 is START with NEQ (open boundary)
    // If so, SOURCE at the same join_attr should NOT be counted
    int32_t is_start_neq = (e1->field_type == START) & (e1->equality_type == NEQ);
    int32_t same_join_attr = (e1->join_attr == e2->join_attr);
    int32_t skip_source = is_start_neq & same_join_attr & is_source;
    
    // Calculate weight delta: START adds, END subtracts, SOURCE no change
    int32_t weight_delta = (is_start * e2->local_mult) - 
                          (is_end * e2->local_mult);
    
    // Update local weight
    e2->local_weight = e1->local_weight + weight_delta;
    
    // Calculate foreign delta for SOURCE entries (skip if at NEQ boundary)
    // SOURCE entries (parent) contribute final_mult / local_weight
    // Avoid division by zero
    int32_t safe_weight = e2->local_weight ? e2->local_weight : 1;
    int32_t foreign_delta = is_source * (1 - skip_source) * (e2->final_mult / safe_weight);
    
    // Update foreign sum (accumulator)
    e2->foreign_sum = e1->foreign_sum + foreign_delta;
}

/**
 * Window compute foreign sum (Algorithm 590)
 * Oblivious conversion: convert 3-way branch to arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_foreign_sum(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_compute_foreign_sum_op);
}

void window_compute_foreign_interval_op(entry_t* e1, entry_t* e2) {
    // Check if we have a START/END pair (obliviously)
    int32_t is_start = (e1->field_type == START);
    int32_t is_end = (e2->field_type == END);
    int32_t is_pair = is_start * is_end;
    
    // Compute foreign interval difference using foreign_sum
    int32_t interval = e2->foreign_sum - e1->foreign_sum;
    
    // Set interval only if we have a pair
    e2->foreign_interval = (is_pair * interval) + 
                          ((1 - is_pair) * e2->foreign_interval);
    
    // CRITICAL: Copy START's foreign_sum to END when we have a pair
    // This ensures END has the correct foreign_sum for later propagation to SOURCE entries
    e2->foreign_sum = (is_pair * e1->foreign_sum) + 
                      ((1 - is_pair) * e2->foreign_sum);
}

/**
 * Window compute foreign interval (Algorithm 609)
 * Oblivious conversion: use conditional arithmetic
 * e1 is window[0], e2 is window[1] in the sliding window
 */
void window_compute_foreign_interval(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_compute_foreign_interval_op);
}

void window_propagate_foreign_interval_op(entry_t* e1, entry_t* e2) {
    // Check entry types
    int32_t is_source2 = (e2->field_type == SOURCE);
    int32_t is_end1 = (e1->field_type == END);
    
    // If e1 is END and e2 is SOURCE, propagate the foreign_interval
    // Otherwise keep e2's existing values
    int32_t should_propagate = is_end1 * is_source2;
    
    e2->foreign_interval = (should_propagate * e1->foreign_interval) + 
                          ((1 - should_propagate) * e2->foreign_interval);
    e2->foreign_sum = (should_propagate * e1->foreign_sum) + 
                     ((1 - should_propagate) * e2->foreign_sum);
}

/**
 * Propagate foreign interval from END entries to SOURCE entries
 * After computing intervals for START/END pairs, propagate to SOURCE entries
 */
void window_propagate_foreign_interval(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_propagate_foreign_interval_op);
}

// ============================================================================
// Distribute-Expand Window Functions
// ============================================================================

/**
 * Compute destination index as cumulative sum of final_mult
 */
void window_compute_dst_idx_op(entry_t* e1, entry_t* e2) {
    // e2's dst_idx = e1's dst_idx + e1's final_mult
    e2->dst_idx = e1->dst_idx + e1->final_mult;
}

void window_compute_dst_idx(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_compute_dst_idx_op);
}

/**
 * Set sequential index values
 */
void window_increment_index_op(entry_t* e1, entry_t* e2) {
    // e2's index = e1's index + 1
    e2->index = e1->index + 1;
}

void window_increment_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_increment_index_op);
}

/**
 * Expansion: copy non-empty entries to fill padding slots
 */
void window_expand_copy_op(entry_t* e1, entry_t* e2) {
    // Check if e2 is DIST_PADDING (obliviously)
    int is_padding = (e2->field_type == DIST_PADDING);
    
    // Save e2's index (always done to maintain oblivious pattern)
    int32_t saved_index = e2->index;
    
    // Create temporary copy of e1
    entry_t temp = *e1;
    
    // Obliviously copy entire struct byte-by-byte
    // If is_padding=1, copy from temp (e1); if is_padding=0, keep e2
    uint8_t* dst = (uint8_t*)e2;
    uint8_t* src_temp = (uint8_t*)&temp;
    uint8_t* src_orig = (uint8_t*)e2;
    
    for (size_t i = 0; i < sizeof(entry_t); i++) {
        dst[i] = (uint8_t)(is_padding * src_temp[i] + (1 - is_padding) * src_orig[i]);
    }
    
    // Restore e2's index (always)
    e2->index = saved_index;
    
    // Note: copy_index computation removed - this is handled in alignment phase
}

void window_expand_copy(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_expand_copy_op);
}

// ============================================================================
// Align-Concat Window Functions
// ============================================================================

/**
 * Update copy index based on original index
 * If same original index, increment from previous
 * If different, reset to 0
 */
void window_update_copy_index_op(entry_t* e1, entry_t* e2) {
    // Check if same original tuple (obliviously)
    int is_same = (e1->original_index == e2->original_index);
    
    // If same, increment; if different, reset to 0
    e2->copy_index = is_same * (e1->copy_index + 1) + (1 - is_same) * 0;
}

void window_update_copy_index(entry_t* e1, entry_t* e2) {
    apply_to_decrypted_pair(e1, e2, window_update_copy_index_op);
}

/**
 * Concatenate attributes from right entry into left entry
 * This adds all attributes from right to left, preserving left's existing attributes
 */
void concat_attributes_op(entry_t* left, entry_t* right, int32_t left_attr_count, int32_t right_attr_count) {
    // Use the provided attribute counts
    
    // Copy attributes from right to left (after left's existing attributes)
    int total_attrs = left_attr_count + right_attr_count;
    if (total_attrs > MAX_ATTRIBUTES) {
        total_attrs = MAX_ATTRIBUTES;  // Limit to max
    }
    
    for (int i = 0; i < right_attr_count && (left_attr_count + i) < MAX_ATTRIBUTES; i++) {
        // Copy attribute value only (column_names no longer exist in entry_t)
        left->attributes[left_attr_count + i] = right->attributes[i];
    }
}

// Note: concat_attributes wrapper removed as we now call concat_attributes_op 
// directly from batch dispatcher with attribute count parameters