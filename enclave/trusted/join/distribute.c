#include "distribute.h"
#include "comparators.h"
#include "oblivious_sort.h"
#include "mem_track.h"
#include "common/constants.h"
#include <stdlib.h>
#include <string.h>

/*
 * Distribute-Expand: replicate each row final_mult times.
 *
 * Rows are converted from wire-format entry_t into width-trimmed trow_t
 * (T1.5) on entry — the pipeline downstream of here never needs the full
 * 64 attribute slots.
 *
 * Per table (mirrors the original ExpandSingleTable):
 *   1. Build trow work array; dst_idx = exclusive prefix sum of
 *      final_mult (original row order); output_size = total sum
 *   2. Mark rows with final_mult == 0 as DIST_PADDING (branchless)
 *   3. Sort padding-last (non-padding rows keep original-index order)
 *   4. Resize to output_size: truncate excess padding / append fresh padding
 *   5. Distribution network: distance-halving passes move each row's content
 *      to the slot matching its dst_idx (fixed pair schedule, oblivious
 *      swaps; slot index fields stay in place)
 *   6. Expansion: linear pass copies each row into the padding slots that
 *      follow it, so row r fills [dst_idx, dst_idx + final_mult)
 */

/* comparator_distribute semantics on trimmed rows. */
static void dist_pair(trow_t* e1, trow_t* e2, size_t stride) {
    int32_t idx1 = e1->index;
    int32_t idx2 = e2->index;
    int should_swap = (e1->dst_idx >= e2->index) &
                      (e1->field_type != DIST_PADDING);
    ej_oblivious_swap_g(e1, e2, stride, should_swap);
    e1->index = idx1;
    e2->index = idx2;
}

/* Fresh padding slot (mirrors transform_create_dist_padding). */
static void make_dist_padding(trow_t* e, size_t stride) {
    memset(e, 0, stride);
    e->field_type     = DIST_PADDING;
    e->final_mult     = 0;
    e->dst_idx        = -1;
    e->original_index = -1;
}

static int expand_single_table(const entry_t* in, size_t n, int32_t width,
                               expanded_table_t* out)
{
    size_t stride = trow_stride(width);
    out->rows   = NULL;
    out->n      = 0;
    out->width  = width;
    out->stride = stride;
    if (n == 0) return 0;

    /* Step 1: build the trimmed work array in original row order. */
    uint8_t* work = (uint8_t*)mt_alloc(n * stride);
    if (!work) return -1;
    int32_t dst = 0;
    for (size_t i = 0; i < n; i++) {
        trow_t* t = trow_at(work, i, stride);
        t->join_attr      = 0;
        t->field_type     = in[i].field_type;
        t->original_index = in[i].original_index;
        t->local_mult     = in[i].local_mult;
        t->foreign_sum    = in[i].foreign_sum;
        t->copy_index     = 0;
        t->alignment_key  = 0;
        t->final_mult     = in[i].final_mult;
        t->dst_idx        = dst;
        t->index          = 0;
        memcpy(t->attributes, in[i].attributes, 4u * (size_t)width);
        dst += t->final_mult;
    }
    size_t output_size = (size_t)dst;
    if (output_size == 0) {           /* no row survives the join */
        mt_free(work);
        return 0;
    }

    /* Step 2: mark zero-mult rows as padding (branchless). */
    for (size_t i = 0; i < n; i++) {
        trow_t* t = trow_at(work, i, stride);
        int32_t is_zero = (t->final_mult == 0);
        t->field_type = is_zero * DIST_PADDING + (1 - is_zero) * t->field_type;
    }

    /* Step 3: sort padding-last. */
    trow_set_sort_ctx(width, stride);
    if (ej_osort_g(work, n, stride, t_cmp_dist_padding_last,
                   trow_make_pad) != 0) {
        mt_free(work);
        return -1;
    }

    /* Step 4: resize to output_size. */
    uint8_t* arr = (uint8_t*)mt_alloc(output_size * stride);
    if (!arr) { mt_free(work); return -1; }
    size_t keep = (n < output_size) ? n : output_size;
    memcpy(arr, work, keep * stride);
    for (size_t i = keep; i < output_size; i++)
        make_dist_padding(trow_at(arr, i, stride), stride);
    mt_free(work);

    /* Slot indices for the distribution network. */
    for (size_t i = 0; i < output_size; i++)
        trow_at(arr, i, stride)->index = (int32_t)i;

    /* Step 5: distance-halving distribution network (right-to-left within
     * each pass, largest power-of-two distance first). */
    size_t distance = 1;
    while ((distance << 1) <= output_size) distance <<= 1;
    for (; distance >= 1; distance >>= 1) {
        for (size_t j = output_size - distance; j-- > 0; )
            dist_pair(trow_at(arr, j, stride),
                      trow_at(arr, j + distance, stride), stride);
    }

    /* Step 6: expansion — fill each padding slot from its predecessor.
     * Branchless word-wise select. */
    size_t words = stride / sizeof(uint64_t);
    for (size_t i = 1; i < output_size; i++) {
        trow_t* cur  = trow_at(arr, i, stride);
        trow_t* prev = trow_at(arr, i - 1, stride);
        int is_pad = (cur->field_type == DIST_PADDING);
        uint64_t mask = (uint64_t)-(uint64_t)is_pad;
        int32_t slot = cur->index;
        const uint64_t* src = (const uint64_t*)prev;
        uint64_t*       dstw = (uint64_t*)cur;
        for (size_t w = 0; w < words; w++)
            dstw[w] = (src[w] & mask) | (dstw[w] & ~mask);
        cur->index = slot;
    }

    out->rows = arr;
    out->n    = output_size;
    return 0;
}

int distribute_expand(
    const entry_t*          rows,
    const table_desc_t*     descs,
    const join_node_desc_t* tree,
    size_t                  num_nodes,
    expanded_table_t*       out)
{
    for (size_t k = 0; k < num_nodes; k++) {
        int32_t ti = tree[k].table_idx;
        if (expand_single_table(rows + descs[ti].offset_rows,
                                descs[ti].num_rows,
                                (int32_t)descs[ti].num_cols, &out[k]) != 0) {
            free_expanded(out, k);
            return -1;
        }
    }
    return 0;
}

void free_expanded(expanded_table_t* out, size_t num_nodes) {
    for (size_t k = 0; k < num_nodes; k++) {
        mt_free(out[k].rows);
        out[k].rows = NULL;
        out[k].n    = 0;
    }
}
