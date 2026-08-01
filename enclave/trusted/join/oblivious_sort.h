#ifndef EJ_OBLIVIOUS_SORT_H
#define EJ_OBLIVIOUS_SORT_H

#include "common/entry_t.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fills one element with sort padding (must compare after all real
 * elements under the comparator in use). */
typedef void (*ej_make_pad_fn)(void* elem);

/*
 * ej_osort_g — data-oblivious sort for arrays of any element type
 * (shuffle-then-sort paradigm, mirroring the original's
 * shuffle_merge_sort):
 *
 *   1. Pad the array to a power of two using make_pad (every comparator
 *      must send padding after all real elements).
 *   2. Waksman permutation network with secret random switch bits — fixed
 *      access pattern, applies a secret random permutation.
 *   3. In-place qsort.  After the secret shuffle, any comparison sort's
 *      access pattern is distributed independently of the input order
 *      (equal-key multiplicities excepted, as in the original).
 *   4. Copy the first n (real) elements back.
 *
 * elem_size must be a multiple of 8 (word-wise oblivious swaps).
 * Returns 0 on success, -1 on allocation/randomness failure (array left
 * unsorted in that case; callers must propagate the error).
 */
int ej_osort_g(void* arr, size_t n, size_t elem_size,
               int (*cmp)(const void*, const void*),
               ej_make_pad_fn make_pad);

#ifdef __cplusplus
}
#endif

#endif /* EJ_OBLIVIOUS_SORT_H */
