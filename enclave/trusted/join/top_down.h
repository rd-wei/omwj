#ifndef EJ_TOP_DOWN_H
#define EJ_TOP_DOWN_H

#include "common/entry_t.h"
#include "common/ecall_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * top_down — propagate final multiplicities from the root down to the leaves.
 *
 * Requires bottom_up() to have run (local_mult populated).  After this call,
 * every row r in every tree table has:
 *   r.final_mult  = number of join output tuples that use r
 *   r.foreign_sum = starting output position for r's copies (used by align)
 *
 * Invariant: sum of final_mult over any single tree table equals the total
 * join cardinality.
 *
 * The join tree array must be in post-order; nodes are processed in reverse
 * array order so each parent's final_mult is computed before its children's.
 * Returns 0 on success, -1 on allocation failure.
 */
int top_down(
    entry_t*                rows,
    const table_desc_t*     descs,
    size_t                  num_tables,
    const join_node_desc_t* tree,
    size_t                  num_nodes
);

#ifdef __cplusplus
}
#endif

#endif /* EJ_TOP_DOWN_H */
