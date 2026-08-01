#include "trow.h"
#include "common/constants.h"
#include <string.h>
#include <limits.h>

static int32_t g_t_width;
static size_t  g_t_stride;

void trow_set_sort_ctx(int32_t width, size_t stride) {
    g_t_width  = width;
    g_t_stride = stride;
}

/* SORT_PADDING rows sort after everything else (mirrors pad_adjust). */
static int pad_adjust(const trow_t* e1, const trow_t* e2, int normal) {
    int p1 = (e1->field_type == SORT_PADDING);
    int p2 = (e2->field_type == SORT_PADDING);
    if (p1 | p2) return p1 - p2;
    return normal;
}

int t_cmp_dist_padding_last(const void* a, const void* b) {
    const trow_t* e1 = (const trow_t*)a;
    const trow_t* e2 = (const trow_t*)b;
    int normal;
    int p1 = (e1->field_type == DIST_PADDING);
    int p2 = (e2->field_type == DIST_PADDING);
    if (p1 != p2)
        normal = p1 - p2;  /* padding last */
    else if (e1->original_index != e2->original_index)
        normal = (e1->original_index < e2->original_index) ? -1 : 1;
    else
        normal = 0;
    return pad_adjust(e1, e2, normal);
}

int t_cmp_join_then_other(const void* a, const void* b) {
    const trow_t* e1 = (const trow_t*)a;
    const trow_t* e2 = (const trow_t*)b;
    /* Constant access pattern: always compare join_attr and ALL g_t_width
     * attributes with no early exit or data-dependent branch, so the set of
     * attribute words read is independent of the values.  A data-dependent
     * early break would leak the within-row prefix-match length at
     * cache-line granularity (the shuffle only hides element order, not this).
     * Lexicographic result = sign of the first differing position; the
     * branchless accumulation keeps the first non-zero sign. */
    int cmp = (e1->join_attr > e2->join_attr) - (e1->join_attr < e2->join_attr);
    for (int32_t i = 0; i < g_t_width; i++) {
        int c = (e1->attributes[i] > e2->attributes[i])
              - (e1->attributes[i] < e2->attributes[i]);
        cmp += (cmp == 0) * c;   /* take c only while still undecided */
    }
    return pad_adjust(e1, e2, cmp);
}

int t_cmp_alignment_key(const void* a, const void* b) {
    /* Tie-breakers follow the verified reference implementation:
     * original_index groups a tuple's copies together, copy_index orders
     * within the group (canonical order from the pre-sort). */
    const trow_t* e1 = (const trow_t*)a;
    const trow_t* e2 = (const trow_t*)b;
    int normal;
    if (e1->alignment_key != e2->alignment_key)
        normal = (e1->alignment_key < e2->alignment_key) ? -1 : 1;
    else if (e1->original_index != e2->original_index)
        normal = (e1->original_index < e2->original_index) ? -1 : 1;
    else if (e1->copy_index != e2->copy_index)
        normal = (e1->copy_index < e2->copy_index) ? -1 : 1;
    else
        normal = 0;
    return pad_adjust(e1, e2, normal);
}

void trow_make_pad(void* elem) {
    trow_t* e = (trow_t*)elem;
    memset(elem, 0, g_t_stride);
    e->field_type     = SORT_PADDING;
    e->join_attr      = INT32_MAX;
    e->original_index = NULL_VALUE;
}

/* rrow_t versions: same canonical order and padding-last rule as the trow_t
 * accumulator comparator, on the 8-byte-header narrow row. */
static int rr_pad_adjust(const rrow_t* e1, const rrow_t* e2, int normal) {
    int p1 = (e1->field_type == SORT_PADDING);
    int p2 = (e2->field_type == SORT_PADDING);
    if (p1 | p2) return p1 - p2;
    return normal;
}

int rr_cmp_join_then_other(const void* a, const void* b) {
    const rrow_t* e1 = (const rrow_t*)a;
    const rrow_t* e2 = (const rrow_t*)b;
    /* Constant access pattern: always compare join_attr and all g_t_width
     * attributes, branchlessly (see t_cmp_join_then_other). */
    int cmp = (e1->join_attr > e2->join_attr) - (e1->join_attr < e2->join_attr);
    for (int32_t i = 0; i < g_t_width; i++) {
        int c = (e1->attributes[i] > e2->attributes[i])
              - (e1->attributes[i] < e2->attributes[i]);
        cmp += (cmp == 0) * c;
    }
    return rr_pad_adjust(e1, e2, cmp);
}

void rrow_make_pad(void* elem) {
    rrow_t* e = (rrow_t*)elem;
    memset(elem, 0, g_t_stride);
    e->field_type = SORT_PADDING;
    e->join_attr  = INT32_MAX;
}
