#ifndef EJ_DISTRIBUTE_H
#define EJ_DISTRIBUTE_H

#include "common/entry_t.h"
#include "common/ecall_types.h"
#include "trow.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One expanded tree table produced by distribute_expand(): a trow_t array
 * carrying only the table's own `width` columns (T1.5). */
typedef struct {
    uint8_t* rows;   /* malloc'd trow array; caller frees via free_expanded() */
    size_t   n;      /* number of rows (= join cardinality for every node) */
    int32_t  width;  /* valid attribute slots per row */
    size_t   stride; /* trow_stride(width) */
} expanded_table_t;

/*
 * distribute_expand — replicate every tree table's rows by final_mult.
 *
 * Requires top_down() to have run.  For each join tree node, produces an
 * expanded table of exactly output_size = sum(final_mult) rows, where each
 * input row appears final_mult times consecutively, ordered by original
 * row position (dst_idx = exclusive prefix sum of final_mult).
 *
 * out[] must have space for num_nodes entries; out[k] corresponds to
 * tree[k].  Returns 0 on success, -1 on allocation failure (out[] entries
 * already produced are freed).
 */
int distribute_expand(
    const entry_t*          rows,
    const table_desc_t*     descs,
    const join_node_desc_t* tree,
    size_t                  num_nodes,
    expanded_table_t*       out
);

/* Free all expanded tables produced by distribute_expand(). */
void free_expanded(expanded_table_t* out, size_t num_nodes);

#ifdef __cplusplus
}
#endif

#endif /* EJ_DISTRIBUTE_H */
