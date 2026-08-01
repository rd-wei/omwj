#include "bottom_up.h"
#include "skinny.h"
#include "oblivious_sort.h"
#include "mem_track.h"
#include "common/constants.h"
#include <stdlib.h>
#include <string.h>

/* ---- Core per-edge computation ---- */

/*
 * compute_local_mult — for one parent–child edge in the join tree:
 * multiply each parent row's local_mult by the count of child rows
 * whose join attribute falls in the range defined by the constraint.
 *
 * Runs entirely on 32-byte sk_rec_t records (T1): the phase never reads
 * attributes[] beyond the join key, and its output is one multiplier per
 * parent row applied back in row order.
 *
 * Algorithm (mirrors the original's 9 steps):
 *   1. Build combined array: nc SOURCE (child) + np START + np END (parent)
 *      with acc (= local_cumsum) initialised to local_mult
 *   2. Sort by join_attr + precedence
 *   3. Linear pass: accumulate acc (SOURCE only, skip NEQ boundary)
 *   4. Sort pairwise: TARGET first, by original_index, START before END
 *   5. Linear pass: interval = acc[END] - acc[START] per pair
 *   6. Sort END-first by original_index
 *   7. First np records are END entries in parent order:
 *      parent[i].local_mult *= combined[i].interval
 */
static int compute_local_mult(
    entry_t* parent, size_t np,
    entry_t* child,  size_t nc,
    int32_t parent_join_col,
    int32_t join_col,
    int32_t dev1, int32_t eq1,
    int32_t dev2, int32_t eq2)
{
    if (np == 0) return 0;

    size_t total = nc + 2 * np;
    sk_rec_t* comb = (sk_rec_t*)mt_alloc(total * sizeof(sk_rec_t));
    if (!comb) return -1;

    /* Step 1: populate combined array (temporaries initialised inline). */
    for (size_t i = 0; i < nc; i++) {           /* SOURCE — child rows */
        sk_rec_t* s = &comb[i];
        s->join_attr      = child[i].attributes[join_col];
        s->field_type     = SOURCE;
        s->equality_type  = NONE;
        s->original_index = child[i].original_index;
        s->local_mult     = child[i].local_mult;
        s->acc            = s->local_mult;
        s->interval       = 0;
        s->weight         = 0;
        s->fmult          = 0;
    }
    for (size_t i = 0; i < np; i++) {           /* START — parent rows */
        sk_rec_t* s = &comb[nc + i];
        s->join_attr      = parent[i].attributes[parent_join_col] + dev1;
        s->field_type     = START;
        s->equality_type  = (int16_t)eq1;
        s->original_index = parent[i].original_index;
        s->local_mult     = parent[i].local_mult;
        s->acc            = s->local_mult;
        s->interval       = 0;
        s->weight         = 0;
        s->fmult          = 0;
    }
    for (size_t i = 0; i < np; i++) {           /* END — parent rows */
        sk_rec_t* s = &comb[nc + np + i];
        s->join_attr      = parent[i].attributes[parent_join_col] + dev2;
        s->field_type     = END;
        s->equality_type  = (int16_t)eq2;
        s->original_index = parent[i].original_index;
        s->local_mult     = parent[i].local_mult;
        s->acc            = s->local_mult;
        s->interval       = 0;
        s->weight         = 0;
        s->fmult          = 0;
    }

    /* Step 2: sort by join_attr + precedence. */
    if (ej_osort_g(comb, total, sizeof(sk_rec_t),
                   sk_cmp_join_attr, sk_make_pad) != 0) goto oom;

    /* Step 3: cumulative sums.  Accumulate SOURCE.local_mult; skip if
     * immediately after START_NEQ at the same join_attr. */
    for (size_t i = 1; i < total; i++) {
        int32_t is_src    = (comb[i].field_type == SOURCE);
        int32_t prev_sneq = (comb[i-1].field_type == START) &
                            (comb[i-1].equality_type == NEQ);
        int32_t same_attr = (comb[i-1].join_attr == comb[i].join_attr);
        int32_t skip      = prev_sneq & same_attr & is_src;
        comb[i].acc = comb[i-1].acc + is_src * (1 - skip) * comb[i].local_mult;
    }

    /* Step 4: sort pairwise. */
    if (ej_osort_g(comb, total, sizeof(sk_rec_t),
                   sk_cmp_pairwise, sk_make_pad) != 0) goto oom;

    /* Step 5: interval for each (START, END) pair. */
    for (size_t i = 1; i < total; i++) {
        int32_t is_pair  = (comb[i-1].field_type == START) &
                           (comb[i].field_type   == END);
        int32_t interval = comb[i].acc - comb[i-1].acc;
        comb[i].interval = is_pair * interval +
                           (1 - is_pair) * comb[i].interval;
    }

    /* Step 6: sort END-first by original_index. */
    if (ej_osort_g(comb, total, sizeof(sk_rec_t),
                   sk_cmp_end_first, sk_make_pad) != 0) goto oom;

    /* Step 7: first np records are END entries in parent-row order. */
    for (size_t i = 0; i < np; i++) {
        parent[i].local_mult *= comb[i].interval;
    }

    mt_free(comb);
    return 0;

oom:
    mt_free(comb);
    return -1;
}

/* ---- Public entry point ---- */

int bottom_up(
    entry_t*                rows,
    const table_desc_t*     descs,
    size_t                  num_tables,
    const join_node_desc_t* tree,
    size_t                  num_nodes)
{
    /* Initialise all rows across all tables (mirrors the original's
     * METADATA_ALL init; field_type must not be left as SORT_PADDING). */
    for (size_t t = 0; t < num_tables; t++) {
        uint32_t start = descs[t].offset_rows;
        uint32_t n     = descs[t].num_rows;
        for (uint32_t r = 0; r < n; r++) {
            entry_t* e = &rows[start + r];
            e->field_type       = NULL_VALUE;
            e->equality_type    = NULL_VALUE;
            e->original_index   = (int32_t)r;
            e->local_mult       = 1;
            e->final_mult       = 0;
            e->foreign_sum      = 0;
            e->local_cumsum     = 0;
            e->local_interval   = 0;
            e->foreign_interval = 0;
            e->local_weight     = 0;
            e->copy_index       = 0;
            e->alignment_key    = 0;
            e->dst_idx          = 0;
            e->index            = 0;
        }
    }

    /* Process each non-root node in post-order (array order guarantees this). */
    for (size_t n = 0; n < num_nodes; n++) {
        if (tree[n].parent_idx < 0) continue;  /* root — nothing to do */

        int32_t node_pi  = tree[n].parent_idx;
        int32_t node_ti  = tree[n].table_idx;
        int32_t parent_ti = tree[node_pi].table_idx;

        entry_t* parent = &rows[descs[parent_ti].offset_rows];
        size_t   np     = descs[parent_ti].num_rows;
        entry_t* child  = &rows[descs[node_ti].offset_rows];
        size_t   nc     = descs[node_ti].num_rows;

        if (compute_local_mult(
                parent, np, child, nc,
                tree[n].parent_join_col, tree[n].join_col,
                tree[n].deviation1, tree[n].equality1,
                tree[n].deviation2, tree[n].equality2) != 0)
            return -1;
    }
    return 0;
}
