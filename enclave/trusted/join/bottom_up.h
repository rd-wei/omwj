#ifndef EJ_BOTTOM_UP_H
#define EJ_BOTTOM_UP_H

#include "common/entry_t.h"
#include "common/ecall_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * bottom_up — initialize all table rows and compute local_mult for every
 * parent–child edge in the join tree.
 *
 * After this call, each row r in table t has:
 *   r.local_mult = product over children c of
 *                  (sum of child_c.local_mult for rows matching r)
 *
 * Rows that match no child row get local_mult = 0.
 *
 * The join tree array must be in post-order (leaves before root).
 * Returns 0 on success, -1 on allocation failure.
 */
int bottom_up(
    entry_t*                rows,
    const table_desc_t*     descs,
    size_t                  num_tables,
    const join_node_desc_t* tree,
    size_t                  num_nodes
);

#ifdef __cplusplus
}
#endif

#endif /* EJ_BOTTOM_UP_H */
