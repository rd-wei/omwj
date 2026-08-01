#include "top_down.h"
#include "skinny.h"
#include "oblivious_sort.h"
#include "mem_track.h"
#include "common/constants.h"
#include <stdlib.h>
#include <string.h>

/* ---- Core per-edge computation ---- */

/*
 * compute_final_mult — for one parent–child edge: distribute the parent's
 * final_mult down to the child rows it joins with.
 *
 * Roles are flipped relative to bottom-up: the parent rows become SOURCE
 * (they carry final_mult), and the child rows become START/END boundaries
 * built from the REVERSED constraint:
 *   original:  child.attr ∈ [parent.attr + dev1, parent.attr + dev2]
 *   reversed:  parent.attr ∈ [child.attr - dev2, child.attr - dev1]
 * with the equality tags travelling with their deviations (eq2 with -dev2).
 *
 * Runs entirely on 32-byte sk_rec_t records (T1); field mapping:
 *   acc = foreign_sum, interval = foreign_interval,
 *   weight = local_weight, fmult = final_mult.
 *
 * Algorithm (mirrors bottom-up's steps with foreign_* fields):
 *   1. Build combined array: np SOURCE (parent) + nc START + nc END (child)
 *      with weight initialised to local_mult
 *   2. Sort by join_attr + precedence
 *   3. Linear pass: maintain running weight (+ at START, - at END);
 *      SOURCE adds fmult / weight to acc (skip at NEQ boundary),
 *      splitting the parent's output share among covering child rows
 *   4. Sort pairwise: TARGET first, by original_index, START before END
 *   5. Linear pass: interval = acc[END] - acc[START] per pair;
 *      copy START's acc to END
 *   6. Sort END-first by original_index
 *   7. First nc records are END entries in child-row order:
 *      child.final_mult = interval * child.local_mult;
 *      child.foreign_sum = acc
 */
static int compute_final_mult(
    entry_t* parent, size_t np,
    entry_t* child,  size_t nc,
    int32_t parent_join_col,
    int32_t join_col,
    int32_t dev1, int32_t eq1,
    int32_t dev2, int32_t eq2)
{
    if (nc == 0) return 0;

    /* Reversed constraint: upper becomes lower, negated. */
    int32_t rdev1 = -dev2, req1 = eq2;
    int32_t rdev2 = -dev1, req2 = eq1;

    size_t total = np + 2 * nc;
    sk_rec_t* comb = (sk_rec_t*)mt_alloc(total * sizeof(sk_rec_t));
    if (!comb) return -1;

    /* Step 1: populate combined array (temporaries initialised inline). */
    for (size_t i = 0; i < np; i++) {       /* SOURCE — parent rows */
        sk_rec_t* s = &comb[i];
        s->join_attr      = parent[i].attributes[parent_join_col];
        s->field_type     = SOURCE;
        s->equality_type  = NONE;
        s->original_index = parent[i].original_index;
        s->local_mult     = parent[i].local_mult;
        s->acc            = 0;
        s->interval       = 0;
        s->weight         = s->local_mult;
        s->fmult          = parent[i].final_mult;
    }
    for (size_t i = 0; i < nc; i++) {       /* START — child rows */
        sk_rec_t* s = &comb[np + i];
        s->join_attr      = child[i].attributes[join_col] + rdev1;
        s->field_type     = START;
        s->equality_type  = (int16_t)req1;
        s->original_index = child[i].original_index;
        s->local_mult     = child[i].local_mult;
        s->acc            = 0;
        s->interval       = 0;
        s->weight         = s->local_mult;
        s->fmult          = child[i].final_mult;
    }
    for (size_t i = 0; i < nc; i++) {       /* END — child rows */
        sk_rec_t* s = &comb[np + nc + i];
        s->join_attr      = child[i].attributes[join_col] + rdev2;
        s->field_type     = END;
        s->equality_type  = (int16_t)req2;
        s->original_index = child[i].original_index;
        s->local_mult     = child[i].local_mult;
        s->acc            = 0;
        s->interval       = 0;
        s->weight         = s->local_mult;
        s->fmult          = child[i].final_mult;
    }

    /* Step 2: sort by join_attr + precedence. */
    if (ej_osort_g(comb, total, sizeof(sk_rec_t),
                   sk_cmp_join_attr, sk_make_pad) != 0) goto oom;

    /* Step 3: foreign sums.  weight tracks the total local_mult of child
     * ranges covering the current position; each SOURCE (parent) row
     * splits its final_mult evenly by that weight.  Skip SOURCE
     * immediately after START_NEQ at the same join_attr. */
    for (size_t i = 1; i < total; i++) {
        sk_rec_t* e1 = &comb[i - 1];
        sk_rec_t* e2 = &comb[i];

        int32_t is_start  = (e2->field_type == START);
        int32_t is_end    = (e2->field_type == END);
        int32_t is_src    = (e2->field_type == SOURCE);
        int32_t prev_sneq = (e1->field_type == START) & (e1->equality_type == NEQ);
        int32_t same_attr = (e1->join_attr == e2->join_attr);
        int32_t skip      = prev_sneq & same_attr & is_src;

        e2->weight = e1->weight +
                     is_start * e2->local_mult - is_end * e2->local_mult;

        int32_t safe_weight = e2->weight ? e2->weight : 1;
        int32_t delta       = is_src * (1 - skip) * (e2->fmult / safe_weight);
        e2->acc = e1->acc + delta;
    }

    /* Step 4: sort pairwise. */
    if (ej_osort_g(comb, total, sizeof(sk_rec_t),
                   sk_cmp_pairwise, sk_make_pad) != 0) goto oom;

    /* Step 5: interval for each (START, END) pair; copy START's acc to END
     * so the child inherits its range start. */
    for (size_t i = 1; i < total; i++) {
        sk_rec_t* e1 = &comb[i - 1];
        sk_rec_t* e2 = &comb[i];

        int32_t is_pair  = (e1->field_type == START) & (e2->field_type == END);
        int32_t interval = e2->acc - e1->acc;

        e2->interval = is_pair * interval + (1 - is_pair) * e2->interval;
        e2->acc      = is_pair * e1->acc  + (1 - is_pair) * e2->acc;
    }

    /* Step 6: sort END-first by original_index. */
    if (ej_osort_g(comb, total, sizeof(sk_rec_t),
                   sk_cmp_end_first, sk_make_pad) != 0) goto oom;

    /* Step 7: first nc records are END entries in child-row order. */
    for (size_t i = 0; i < nc; i++) {
        child[i].final_mult  = comb[i].interval * child[i].local_mult;
        child[i].foreign_sum = comb[i].acc;
    }

    mt_free(comb);
    return 0;

oom:
    mt_free(comb);
    return -1;
}

/* ---- Public entry point ---- */

int top_down(
    entry_t*                rows,
    const table_desc_t*     descs,
    size_t                  num_tables,
    const join_node_desc_t* tree,
    size_t                  num_nodes)
{
    (void)num_tables;

    /* Initialise the root table: final_mult = local_mult. */
    for (size_t n = 0; n < num_nodes; n++) {
        if (tree[n].parent_idx >= 0) continue;
        int32_t  ti    = tree[n].table_idx;
        uint32_t start = descs[ti].offset_rows;
        uint32_t cnt   = descs[ti].num_rows;
        for (uint32_t r = 0; r < cnt; r++) {
            entry_t* e = &rows[start + r];
            e->final_mult       = e->local_mult;
            e->foreign_sum      = 0;
            e->foreign_interval = 0;
            e->local_weight     = 0;
        }
    }

    /* Process edges parent-before-child.  The tree array is in post-order
     * (parent_idx > node index), so reverse array order visits every parent
     * before any of its children. */
    for (size_t k = num_nodes; k-- > 0; ) {
        if (tree[k].parent_idx < 0) continue;  /* root — already initialised */

        int32_t node_ti   = tree[k].table_idx;
        int32_t parent_ti = tree[tree[k].parent_idx].table_idx;

        entry_t* parent = &rows[descs[parent_ti].offset_rows];
        size_t   np     = descs[parent_ti].num_rows;
        entry_t* child  = &rows[descs[node_ti].offset_rows];
        size_t   nc     = descs[node_ti].num_rows;

        if (compute_final_mult(
                parent, np, child, nc,
                tree[k].parent_join_col, tree[k].join_col,
                tree[k].deviation1, tree[k].equality1,
                tree[k].deviation2, tree[k].equality2) != 0)
            return -1;
    }
    return 0;
}
