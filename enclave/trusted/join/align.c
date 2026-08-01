#include "align.h"
#include "oblivious_sort.h"
#include "mem_track.h"
#include "common/constants.h"
#include <stdlib.h>
#include <string.h>

/*
 * Align-Concat: stitch the expanded tables into result tuples.
 *
 * Every expanded table has exactly output_size rows (the join cardinality),
 * with each original row replicated final_mult times.  For each parent-child
 * edge the two tables are brought into the same row order and concatenated
 * horizontally:
 *
 *   accumulator: sort by join_attr (parent's join column with this child),
 *                ties by all attributes lexicographically — this is the
 *                output order that foreign_sum values were computed against
 *   child:       copy_index = position within each original row's block of
 *                copies; alignment_key = foreign_sum + copy_index/local_mult
 *                places each copy at its output row; sort by alignment_key
 *
 * After both sorts, row i of the child belongs to row i of the accumulator;
 * the concat merges both into a new accumulator with the combined width
 * (T1.5: strides carry only the columns in use).
 */

/* Convert an expanded table from the full trow_t layout to the narrow rrow_t
 * accumulator layout in place (drops the per-row scratch fields the
 * accumulator never uses).  Used once on the root's table. */
static int trow_table_to_rrow(expanded_table_t* t)
{
    size_t   new_stride = rrow_stride(t->width);
    uint8_t* buf = (uint8_t*)mt_alloc(t->n * new_stride);
    if (!buf) return -1;
    for (size_t i = 0; i < t->n; i++) {
        const trow_t* src = trow_cat(t->rows, i, t->stride);
        rrow_t*       dst = rrow_at(buf, i, new_stride);
        dst->join_attr  = src->join_attr;
        dst->field_type = src->field_type;
        memcpy(dst->attributes, src->attributes, 4u * (size_t)t->width);
    }
    mt_free(t->rows);
    t->rows   = buf;
    t->stride = new_stride;
    return 0;
}

/* Attach one node's (full trow_t) expanded table onto the GLOBAL accumulated
 * result (a narrow rrow_t table; frees the child's row array).
 * parent_col_global is the parent's join column offset within the result's
 * accumulated layout. */
static int attach_child(expanded_table_t* res, expanded_table_t* child,
                        int32_t parent_col_global, int32_t join_col)
{
    size_t n = res->n;

    /* Set join attributes on both sides for this edge. */
    for (size_t i = 0; i < n; i++) {
        rrow_t* t = rrow_at(res->rows, i, res->stride);
        t->join_attr = t->attributes[parent_col_global];
    }
    for (size_t i = 0; i < child->n; i++) {
        trow_t* t = trow_at(child->rows, i, child->stride);
        t->join_attr = t->attributes[join_col];
    }

    /* Result into canonical (edge join attr, all accumulated columns)
     * order. */
    trow_set_sort_ctx(res->width, res->stride);
    if (ej_osort_g(res->rows, n, res->stride,
                   rr_cmp_join_then_other, rrow_make_pad) != 0) return -1;

    /* No canonical pre-sort of the child is needed: in the top-down
     * traversal the child is always a node's OWN expanded table, whose
     * copies of a tuple are byte-identical (except the slot index, which
     * no comparator reads).  Expansion already leaves copies adjacent
     * (copy_index adjacency), and the alignment-key sort below is fully
     * deterministic via its (original_index, copy_index) tie-breakers. */

    /* copy_index counts within each block of a tuple's copies. */
    if (child->n > 0)
        trow_at(child->rows, 0, child->stride)->copy_index = 0;
    for (size_t i = 1; i < child->n; i++) {
        trow_t* cur  = trow_at(child->rows, i, child->stride);
        trow_t* prev = trow_at(child->rows, i - 1, child->stride);
        int32_t same = (cur->original_index == prev->original_index);
        cur->copy_index = same * (prev->copy_index + 1);
    }

    /* alignment_key = foreign_sum + copy_index / local_mult (safe divide). */
    for (size_t i = 0; i < child->n; i++) {
        trow_t* t = trow_at(child->rows, i, child->stride);
        int32_t safe_mult = t->local_mult + (t->local_mult == 0);
        t->alignment_key = t->foreign_sum + t->copy_index / safe_mult;
    }

    trow_set_sort_ctx(child->width, child->stride);
    if (ej_osort_g(child->rows, child->n, child->stride,
                   t_cmp_alignment_key, trow_make_pad) != 0) return -1;

    /* Horizontal concatenation into a new (narrow) result of combined width. */
    int32_t new_width  = res->width + child->width;
    size_t  new_stride = rrow_stride(new_width);
    uint8_t* merged = (uint8_t*)mt_alloc(n * new_stride);
    if (!merged) return -1;

    for (size_t i = 0; i < n; i++) {
        rrow_t*       dst = rrow_at(merged, i, new_stride);
        const rrow_t* a   = rrow_cat(res->rows, i, res->stride);
        const trow_t* c   = trow_cat(child->rows, i, child->stride);
        memcpy(dst, a, RROW_HEADER_BYTES);
        memcpy(dst->attributes, a->attributes, 4u * (size_t)res->width);
        memcpy(dst->attributes + res->width, c->attributes,
               4u * (size_t)child->width);
    }

    mt_free(res->rows);
    mt_free(child->rows);
    child->rows = NULL;
    child->n    = 0;
    res->rows   = merged;
    res->width  = new_width;
    res->stride = new_stride;
    return 0;
}

/* Emit the pre-order node sequence (root first, children in tree-array
 * order, depth-first). */
static void preorder(const join_node_desc_t* tree, size_t num_nodes,
                     size_t node, size_t* seq, size_t* count)
{
    seq[(*count)++] = node;
    for (size_t c = 0; c < num_nodes; c++) {
        if (tree[c].parent_idx == (int32_t)node)
            preorder(tree, num_nodes, c, seq, count);
    }
}

/*
 * Traversal follows the verified reference (and the paper's Algorithm 4):
 * the result accumulates TOP-DOWN from the root — every node's expanded
 * table aligns against the GLOBAL result, whose rows already carry all
 * previously attached columns and are re-sorted by the edge's parent join
 * attribute at full width.  (Folding subtrees bottom-up into local
 * accumulators — as both earlier C++ ports did — mispairs the copies of a
 * centre node: a non-root node with two or more children.)
 *
 * Attach order: for each parent in pre-order, attach its children in tree
 * order.  Column layout = attach order (root's columns first); the host
 * computes the same order for output projection.
 */
int align_concat(
    expanded_table_t*       expanded,
    const table_desc_t*     descs,
    const join_node_desc_t* tree,
    size_t                  num_nodes,
    join_result_t*          result)
{
    (void)descs;

    result->rows   = NULL;
    result->n      = 0;
    result->width  = 0;
    result->stride = 0;

    size_t root = num_nodes;
    for (size_t k = 0; k < num_nodes; k++)
        if (tree[k].parent_idx < 0) root = k;
    if (root == num_nodes) return -1;

    size_t* seq = (size_t*)mt_alloc(num_nodes * sizeof(size_t));
    int32_t* col_off = (int32_t*)mt_alloc(num_nodes * sizeof(int32_t));
    if (!seq || !col_off) { mt_free(seq); mt_free(col_off); return -1; }
    size_t count = 0;
    preorder(tree, num_nodes, root, seq, &count);

    /* Result starts as the root's expanded table, converted to the narrow
     * accumulator layout (the accumulator only needs join_attr + field_type,
     * so the per-row scratch is dropped before the wide oblivious sorts). */
    expanded_table_t res = expanded[root];
    expanded[root].rows = NULL;
    expanded[root].n    = 0;
    if (trow_table_to_rrow(&res) != 0) {
        mt_free(res.rows); mt_free(seq); mt_free(col_off); return -1;
    }
    col_off[root] = 0;

    for (size_t s = 0; s < count; s++) {
        size_t v = seq[s];
        for (size_t c = 0; c < num_nodes; c++) {
            if (tree[c].parent_idx != (int32_t)v) continue;
            col_off[c] = res.width;
            if (attach_child(&res, &expanded[c],
                             col_off[v] + tree[c].parent_join_col,
                             tree[c].join_col) != 0) {
                mt_free(res.rows);
                mt_free(seq);
                mt_free(col_off);
                return -1;
            }
        }
    }

    mt_free(seq);
    mt_free(col_off);
    result->rows   = res.rows;
    result->n      = res.n;
    result->width  = res.width;
    result->stride = res.stride;
    return 0;
}
