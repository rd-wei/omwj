#ifndef EJ_ALIGN_H
#define EJ_ALIGN_H

#include "common/entry_t.h"
#include "common/ecall_types.h"
#include "distribute.h"
#include "trow.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The final join result: one width-trimmed trow per output tuple, with the
 * concatenated attribute columns of every tree table. */
typedef struct {
    uint8_t* rows;   /* malloc'd trow array; caller frees */
    size_t   n;      /* join cardinality */
    int32_t  width;  /* total number of valid attribute columns */
    size_t   stride; /* trow_stride(width) */
} join_result_t;

/*
 * align_concat — stitch the expanded tree tables into result tuples.
 *
 * Consumes the expanded tables from distribute_expand() (their row arrays
 * are freed as they are merged).  Column order: each node's own columns
 * followed by its children's subtree columns, children in tree-array order;
 * the root's accumulator is the final result.
 *
 * Returns 0 on success, -1 on allocation failure.
 */
int align_concat(
    expanded_table_t*       expanded,
    const table_desc_t*     descs,
    const join_node_desc_t* tree,
    size_t                  num_nodes,
    join_result_t*          result
);

#ifdef __cplusplus
}
#endif

#endif /* EJ_ALIGN_H */
