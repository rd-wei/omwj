#ifndef EJ_JOIN_TREE_SERIALIZER_H
#define EJ_JOIN_TREE_SERIALIZER_H

#include "../../common/ecall_types.h"
#include "../io/table_reader.h"
#include "../query/query_parser.h"
#include <vector>

/*
 * Convert a ParsedQuery + the loaded tables into a flat join_node_desc_t array.
 *
 * The array is in post-order (leaves before their parents).  The root node is
 * the last element (index == result.size()-1).
 *
 * Column names in the join conditions are resolved against table_desc_t.col_names
 * to produce integer attribute indices.
 *
 * `variant` selects one rooted orientation of the join tree.  The bottom-up /
 * top-down passes fix how a tree is evaluated, so unlike a binary-join engine we
 * have no parenthesisation freedom -- but the tree can still be rooted at any
 * table, and a node's children can be visited in any order, and both change the
 * work each pass does.  **Variant 0 is the historical tree** (root = first table
 * in the FROM clause, children in constraint order), so every previously
 * published number reproduces unchanged.  Variants are ordered root-major: all
 * child orderings for root 0, then root 1, and so on.
 *
 * Enumerating them is what lets us hold ourselves to the same blind-order
 * standard we apply to a baseline that ships no planner: choosing a root by
 * cardinality would be a data-dependent decision an oblivious engine cannot
 * make without leaking.
 *
 * Throws std::runtime_error if:
 *   - a table named in the query is absent from `tables`
 *   - a column named in a join condition is not in the referenced table
 *   - the join graph is disconnected
 *   - `variant` >= count_join_tree_variants()
 */
std::vector<join_node_desc_t> serialize_join_tree(
    const ParsedQuery&           query,
    const std::vector<LoadedTable>& tables,
    uint64_t                     variant = 0
);

/* Total number of distinct rooted orientations for this query. */
uint64_t count_join_tree_variants(
    const ParsedQuery&           query,
    const std::vector<LoadedTable>& tables
);

/* Per-root child-ordering counts, in FROM-clause order; sums to the total. */
std::vector<int32_t> join_tree_variant_shape(
    const ParsedQuery&           query,
    const std::vector<LoadedTable>& tables
);

#endif /* EJ_JOIN_TREE_SERIALIZER_H */
