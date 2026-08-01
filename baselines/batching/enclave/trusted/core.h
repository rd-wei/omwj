#ifndef ENCLAVE_CORE_H
#define ENCLAVE_CORE_H

#include "enclave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transform functions for Map operations
 */
void transform_set_local_mult_one(entry_t* entry);
void transform_set_local_mult_one_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_add_metadata(entry_t* entry);
void transform_add_metadata_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_set_index(entry_t* entry, uint32_t index);
void transform_set_index_op(entry_t* entry, uint32_t index);  // Raw operation without decrypt/encrypt
void transform_init_local_temps(entry_t* entry);
void transform_init_local_temps_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_init_final_mult(entry_t* entry);
void transform_init_final_mult_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_init_foreign_temps(entry_t* entry);
void transform_init_foreign_temps_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_to_source(entry_t* entry);
void transform_to_source_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_to_start(entry_t* entry, int32_t deviation, equality_type_t equality);
void transform_to_start_op(entry_t* entry, int32_t deviation, equality_type_t equality);  // Raw operation without decrypt/encrypt
void transform_to_end(entry_t* entry, int32_t deviation, equality_type_t equality);
void transform_to_end_op(entry_t* entry, int32_t deviation, equality_type_t equality);  // Raw operation without decrypt/encrypt
void transform_set_sort_padding(entry_t* entry);
void transform_set_sort_padding_op(entry_t* entry);  // Raw operation without decrypt/encrypt

/**
 * Distribute-expand transform functions
 */
void transform_init_dst_idx(entry_t* entry);
void transform_init_dst_idx_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_init_index(entry_t* entry);
void transform_init_index_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_mark_zero_mult_padding(entry_t* entry);
void transform_mark_zero_mult_padding_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_create_dist_padding(entry_t* entry);
void transform_create_dist_padding_op(entry_t* entry);  // Raw operation without decrypt/encrypt

/**
 * Window functions for oblivious processing
 */
void window_set_original_index(entry_t* e1, entry_t* e2);
void window_set_original_index_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void update_target_multiplicity(entry_t* source, entry_t* target);
void update_target_multiplicity_op(entry_t* source, entry_t* target);  // Raw operation without decrypt/encrypt
void update_target_final_multiplicity(entry_t* source, entry_t* target);
void update_target_final_multiplicity_op(entry_t* source, entry_t* target);  // Raw operation without decrypt/encrypt
void window_compute_local_sum(entry_t* e1, entry_t* e2);
void window_compute_local_sum_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void window_compute_local_interval(entry_t* e1, entry_t* e2);
void window_compute_local_interval_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void window_compute_foreign_sum(entry_t* e1, entry_t* e2);
void window_compute_foreign_sum_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void window_compute_foreign_interval(entry_t* e1, entry_t* e2);
void window_compute_foreign_interval_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void window_propagate_foreign_interval(entry_t* e1, entry_t* e2);
void window_propagate_foreign_interval_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt

/**
 * Distribute-expand window functions
 */
void window_compute_dst_idx(entry_t* e1, entry_t* e2);
void window_compute_dst_idx_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void window_increment_index(entry_t* e1, entry_t* e2);
void window_increment_index_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void window_expand_copy(entry_t* e1, entry_t* e2);
void window_expand_copy_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt

/**
 * Comparator functions for oblivious sorting
 * These modify the entries in-place by swapping if needed
 */
void comparator_join_attr(entry_t* e1, entry_t* e2);
void comparator_join_attr_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_pairwise(entry_t* e1, entry_t* e2);
void comparator_pairwise_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_end_first(entry_t* e1, entry_t* e2);
void comparator_end_first_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_join_then_other(entry_t* e1, entry_t* e2);
void comparator_join_then_other_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_original_index(entry_t* e1, entry_t* e2);
void comparator_original_index_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_alignment_key(entry_t* e1, entry_t* e2);
void comparator_alignment_key_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_padding_last(entry_t* e1, entry_t* e2);
void comparator_padding_last_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
void comparator_distribute(entry_t* e1, entry_t* e2);
void comparator_distribute_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt

/**
 * Oblivious swap utility
 */
void oblivious_swap(entry_t* e1, entry_t* e2, int should_swap);

/**
 * Utility functions
 */
int32_t obtain_output_size(const entry_t* last_entry);

/**
 * Align-Concat phase functions
 */
void transform_init_copy_index(entry_t* entry);
void transform_init_copy_index_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void transform_compute_alignment_key(entry_t* entry);
void transform_compute_alignment_key_op(entry_t* entry);  // Raw operation without decrypt/encrypt
void window_update_copy_index(entry_t* e1, entry_t* e2);
void window_update_copy_index_op(entry_t* e1, entry_t* e2);  // Raw operation without decrypt/encrypt
// concat_attributes wrapper removed - we call concat_attributes_op directly with parameters
void concat_attributes_op(entry_t* left, entry_t* right, int32_t left_attr_count, int32_t right_attr_count);  // Raw operation without decrypt/encrypt

/**
 * Join attribute setting
 */
void transform_set_join_attr(entry_t* entry, int32_t column_index);
void transform_set_join_attr_op(entry_t* entry, int32_t column_index);  // Raw operation without decrypt/encrypt

/**
 * Initialize metadata fields to NULL_VALUE based on field mask
 * Use METADATA_* constants from enclave_types.h for the mask
 */
void transform_init_metadata_null(entry_t* entry, uint32_t field_mask);
void transform_init_metadata_null_op(entry_t* entry, uint32_t field_mask);  // Raw operation without decrypt/encrypt

#ifdef __cplusplus
}
#endif

#endif // ENCLAVE_CORE_H