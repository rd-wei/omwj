#ifndef EJ_SKINNY_H
#define EJ_SKINNY_H

/*
 * sk_rec_t — the 32-byte working record for the bottom-up and top-down
 * multiplicity computations (T1 optimisation).
 *
 * Those phases never read entry_t.attributes[] after extracting the join
 * key, and their outputs are one int32 per base-table row applied back in
 * row order — so the combined boundary arrays never need to carry wide
 * rows at all.  Sorting 32-byte records instead of 336-byte entries cuts
 * the per-edge sort traffic ~10x and keeps the buffers EPC-resident far
 * longer.  Same oblivious machinery (Waksman shuffle + post-shuffle sort
 * via ej_osort_g), same leakage model.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t join_attr;
    int16_t field_type;      /* SOURCE / START / END / SORT_PADDING */
    int16_t equality_type;   /* NONE / EQ / NEQ */
    int32_t original_index;
    int32_t local_mult;
    int32_t acc;             /* BU: local_cumsum   | TD: foreign_sum      */
    int32_t interval;        /* BU: local_interval | TD: foreign_interval */
    int32_t weight;          /* TD: local_weight (running boundary sum)   */
    int32_t fmult;           /* TD: final_mult (SOURCE side)              */
} sk_rec_t;                  /* 32 bytes */

/* Comparators: identical ordering rules to the entry_t versions in
 * comparators.c (join_attr + boundary precedence; pairwise; END-first),
 * including SORT_PADDING-last adjustment. */
int sk_cmp_join_attr(const void* a, const void* b);
int sk_cmp_pairwise(const void* a, const void* b);
int sk_cmp_end_first(const void* a, const void* b);

/* Padding filler for ej_osort_g. */
void sk_make_pad(void* elem);

#ifdef __cplusplus
}
#endif

#endif /* EJ_SKINNY_H */
