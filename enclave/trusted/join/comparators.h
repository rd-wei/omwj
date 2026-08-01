#ifndef EJ_COMPARATORS_H
#define EJ_COMPARATORS_H

/*
 * Oblivious swap primitive shared by the distribution network and the
 * Waksman shuffle.  Comparators live next to their record types:
 * skinny.h (bottom-up/top-down) and trow.h (distribute/align).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Branchless word-wise swap: exchanges size_bytes (must be a multiple
 * of 8) between a and b iff should_swap is non-zero, touching the same
 * bytes either way. */
void ej_oblivious_swap_g(void* a, void* b, size_t size_bytes, int should_swap);

#ifdef __cplusplus
}
#endif

#endif /* EJ_COMPARATORS_H */
