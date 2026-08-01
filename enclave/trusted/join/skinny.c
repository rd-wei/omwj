#include "skinny.h"
#include "common/constants.h"
#include <string.h>
#include <limits.h>

_Static_assert(sizeof(sk_rec_t) % 8 == 0,
               "sk_rec_t size must be a multiple of 8 for word-wise swap");

/* SORT_PADDING records sort after everything else (mirrors pad_adjust in
 * comparators.c). */
static int pad_adjust(const sk_rec_t* e1, const sk_rec_t* e2, int normal) {
    int p1 = (e1->field_type == SORT_PADDING);
    int p2 = (e2->field_type == SORT_PADDING);
    if (p1 | p2) return p1 - p2;
    return normal;
}

/* Precedence (Algorithm 513): 1: END_NEQ / START_EQ, 2: SOURCE,
 * 3: END_EQ / START_NEQ. */
static int get_prec(int32_t ft, int32_t et) {
    int end_neq   = (ft == END)   & (et == NEQ);
    int start_eq  = (ft == START) & (et == EQ);
    int src       = (ft == SOURCE);
    int end_eq    = (ft == END)   & (et == EQ);
    int start_neq = (ft == START) & (et == NEQ);
    return (end_neq | start_eq) + 2 * src + 3 * (end_eq | start_neq);
}

int sk_cmp_join_attr(const void* a, const void* b) {
    const sk_rec_t* e1 = (const sk_rec_t*)a;
    const sk_rec_t* e2 = (const sk_rec_t*)b;
    int normal;
    if (e1->join_attr != e2->join_attr)
        normal = (e1->join_attr < e2->join_attr) ? -1 : 1;
    else
        normal = get_prec(e1->field_type, e1->equality_type) -
                 get_prec(e2->field_type, e2->equality_type);
    return pad_adjust(e1, e2, normal);
}

int sk_cmp_pairwise(const void* a, const void* b) {
    const sk_rec_t* e1 = (const sk_rec_t*)a;
    const sk_rec_t* e2 = (const sk_rec_t*)b;
    int normal;
    int t1 = (e1->field_type == START) | (e1->field_type == END);
    int t2 = (e2->field_type == START) | (e2->field_type == END);
    if (t1 != t2)
        normal = t2 - t1;  /* target first: negative when e1 is target */
    else if (e1->original_index != e2->original_index)
        normal = (e1->original_index < e2->original_index) ? -1 : 1;
    else
        /* START before END for the same original_index */
        normal = (e2->field_type == START) - (e1->field_type == START);
    return pad_adjust(e1, e2, normal);
}

int sk_cmp_end_first(const void* a, const void* b) {
    const sk_rec_t* e1 = (const sk_rec_t*)a;
    const sk_rec_t* e2 = (const sk_rec_t*)b;
    int normal;
    int end1 = (e1->field_type == END);
    int end2 = (e2->field_type == END);
    if (end1 != end2)
        normal = end2 - end1;  /* END first: negative when e1 is END */
    else if (e1->original_index != e2->original_index)
        normal = (e1->original_index < e2->original_index) ? -1 : 1;
    else
        normal = 0;
    return pad_adjust(e1, e2, normal);
}

void sk_make_pad(void* elem) {
    sk_rec_t* e = (sk_rec_t*)elem;
    memset(e, 0, sizeof(sk_rec_t));
    e->field_type     = SORT_PADDING;
    e->join_attr      = INT32_MAX;
    e->original_index = NULL_VALUE;
}
