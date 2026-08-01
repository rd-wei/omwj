#ifndef COMPARATOR_CONVENTION_H
#define COMPARATOR_CONVENTION_H

/**
 * Comparator Convention for Non-Oblivious Merge Sort
 * 
 * All comparator functions follow the standard C convention:
 * 
 * int compare(entry_t* e1, entry_t* e2)
 * 
 * Returns:
 *   1 if e1 < e2 (e1 should come before e2 in sorted order)
 *   0 otherwise (e1 >= e2)
 * 
 * This convention allows for efficient sorting where:
 * - Ascending order: use comparator as-is
 * - Descending order: negate the result
 * 
 * For oblivious operations (compare-and-swap), the comparator
 * result determines whether entries should be swapped.
 * 
 * Example:
 *   compare_join_attr(e1, e2) returns 1 if e1->join_attr < e2->join_attr
 *   This produces ascending order by join_attr
 */

// Comparator function type
typedef int (*comparator_func_t)(entry_t*, entry_t*);

#endif // COMPARATOR_CONVENTION_H