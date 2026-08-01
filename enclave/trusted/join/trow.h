#ifndef EJ_TROW_H
#define EJ_TROW_H

/*
 * trow_t — width-trimmed internal row for the distribute-expand and
 * align-concat phases (T1.5 optimisation).
 *
 * The wire-format entry_t carries MAX_ATTRIBUTES=64 attribute slots
 * (336 bytes) regardless of the query's actual width.  Once inside the
 * pipeline, expanded tables and align accumulators only need the columns
 * the query uses, so they are stored as variable-stride rows: a fixed
 * 40-byte header plus exactly `width` int32 attributes, stride rounded up
 * to 8 bytes (word-wise oblivious swaps).  For the TPC-H test queries this
 * cuts the align/distribute working set 1.9-2.5x.
 *
 * Arrays of trow_t are indexed with trow_at(base, i, stride); the
 * flexible attribute array forbids normal pointer arithmetic.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t join_attr;
    int32_t field_type;      /* NULL_VALUE / DIST_PADDING / SORT_PADDING */
    int32_t original_index;
    int32_t local_mult;
    int32_t foreign_sum;
    int32_t copy_index;
    int32_t alignment_key;
    int32_t final_mult;
    int32_t dst_idx;
    int32_t index;
    int32_t attributes[];    /* exactly `width` valid slots */
} trow_t;

#define TROW_HEADER_BYTES ((size_t)offsetof(trow_t, attributes))

static inline size_t trow_stride(int32_t width) {
    return (TROW_HEADER_BYTES + 4u * (size_t)width + 7u) & ~(size_t)7u;
}

static inline trow_t* trow_at(void* base, size_t i, size_t stride) {
    return (trow_t*)((uint8_t*)base + i * stride);
}

static inline const trow_t* trow_cat(const void* base, size_t i, size_t stride) {
    return (const trow_t*)((const uint8_t*)base + i * stride);
}

/*
 * rrow_t — narrow "result row" for the align accumulator.
 *
 * The growing accumulator only ever reads the sort key (join_attr) and the
 * padding tag (field_type); the other 32 bytes of trow_t (original_index,
 * local_mult, foreign_sum, copy_index, alignment_key, final_mult, dst_idx,
 * index) are alignment/distribute scratch that only the child rows use.
 * Carrying them in the accumulator makes the oblivious shuffle swap 32 dead
 * bytes per row on every switch.  rrow_t drops them: an 8-byte header instead
 * of 40, cutting the shuffle's (bandwidth-bound) byte traffic on the
 * accumulator sorts.  Child rows stay trow_t (they need the scratch).
 */
typedef struct {
    int32_t join_attr;
    int32_t field_type;
    int32_t attributes[];    /* exactly `width` valid slots */
} rrow_t;

#define RROW_HEADER_BYTES ((size_t)offsetof(rrow_t, attributes))

static inline size_t rrow_stride(int32_t width) {
    return (RROW_HEADER_BYTES + 4u * (size_t)width + 7u) & ~(size_t)7u;
}
static inline rrow_t* rrow_at(void* base, size_t i, size_t stride) {
    return (rrow_t*)((uint8_t*)base + i * stride);
}
static inline const rrow_t* rrow_cat(const void* base, size_t i, size_t stride) {
    return (const rrow_t*)((const uint8_t*)base + i * stride);
}

/*
 * The qsort comparator interface carries no context, so the current row
 * width/stride are process globals (the enclave is single-threaded,
 * TCSNum=1).  Set before every ej_osort_g call on trow arrays.
 */
void trow_set_sort_ctx(int32_t width, size_t stride);

/* Non-DIST_PADDING before DIST_PADDING, then ascending original_index. */
int t_cmp_dist_padding_last(const void* a, const void* b);

/* Ascending join_attr, then the first `width` attributes lexicographically. */
int t_cmp_join_then_other(const void* a, const void* b);

/* Same order as t_cmp_join_then_other, but on the narrow rrow_t accumulator
 * (uses the shared sort-ctx width). */
int rr_cmp_join_then_other(const void* a, const void* b);

/* Sort padding for rrow_t arrays (uses the shared sort-ctx stride). */
void rrow_make_pad(void* elem);

/* Ascending alignment_key, then join_attr, then copy_index. */
int t_cmp_alignment_key(const void* a, const void* b);

/* Padding filler for ej_osort_g (uses the stride from trow_set_sort_ctx). */
void trow_make_pad(void* elem);

#ifdef __cplusplus
}
#endif

#endif /* EJ_TROW_H */
