#ifndef EJ_ECALL_TYPES_H
#define EJ_ECALL_TYPES_H

/*
 * Types that cross the ecall boundary.
 *
 * These must be plain C structs with no pointers (SGX EDL restriction).
 * Both the host app and the enclave include this header.
 */

#include "constants.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Table descriptor — one per input table.
 * -------------------------------------------------------------------------
 * The host concatenates all encrypted entry_t rows for every table into one
 * flat buffer and passes it via ecall_run_join.  table_desc_t tells the
 * enclave where each table starts and how to interpret its columns.
 */
typedef struct {
    uint32_t offset_rows;              /* index of first row in the flat array */
    uint32_t num_rows;                 /* number of rows in this table */
    uint32_t num_cols;                 /* number of valid attribute slots */
    char     col_names[MAX_COLS][MAX_COL_NAME];  /* column names */
} table_desc_t;

/* -------------------------------------------------------------------------
 * Join node descriptor — one element of the serialised join tree.
 * -------------------------------------------------------------------------
 * The join tree is multi-ary: each node IS a table; children are joined tables.
 * The array is in post-order (leaves first, root last).
 *
 * For the root node: parent_idx == -1, join_col == -1, parent_join_col == -1.
 * For every other node, the join predicate is:
 *
 *   this.attr[join_col]  IN  [parent.attr[parent_join_col] + deviation1,
 *                             parent.attr[parent_join_col] + deviation2]
 *
 * Endpoints are inclusive when equality == EQ, exclusive when equality == NEQ.
 * Equality join: deviation1 == deviation2 == 0, both EQ.
 */
typedef struct {
    int32_t table_idx;        /* index into table_descs[] */
    int32_t parent_idx;       /* index of parent in this array; -1 for root */
    int32_t join_col;         /* attribute index in this table for join key */
    int32_t parent_join_col;  /* attribute index in parent table for join key */
    int32_t deviation1;       /* lower bound offset */
    int32_t equality1;        /* EQ or NEQ for lower bound */
    int32_t deviation2;       /* upper bound offset */
    int32_t equality2;        /* EQ or NEQ for upper bound */
} join_node_desc_t;

/* -------------------------------------------------------------------------
 * Column reference — selects one output column from the result.
 * -------------------------------------------------------------------------
 * output_col_t tells the enclave which attribute slot to copy into the
 * result rows for each requested column.
 */
typedef struct {
    int32_t attr_idx;                  /* index into entry_t.attributes[] */
    char    col_name[MAX_COL_NAME];    /* name to label the output column */
} output_col_t;

#endif /* EJ_ECALL_TYPES_H */
