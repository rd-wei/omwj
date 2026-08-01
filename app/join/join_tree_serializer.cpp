#include "join_tree_serializer.h"
#include "join_constraint.h"
#include <map>
#include <set>
#include <stdexcept>
#include <cstring>

/* Return the attribute index of col_name in desc, or -1 if not found. */
static int32_t col_index(const table_desc_t& desc, const std::string& col_name) {
    for (uint32_t i = 0; i < desc.num_cols; i++) {
        if (col_name == desc.col_names[i])
            return (int32_t)i;
    }
    return -1;
}

/* Intermediate node collected during DFS before indices are known. */
struct NodeInfo {
    int32_t     table_idx;
    std::string parent_table;   /* empty string for root */
    int32_t     join_col;       /* -1 for root */
    int32_t     parent_join_col;/* -1 for root */
    int32_t     deviation1;
    int32_t     equality1;
    int32_t     deviation2;
    int32_t     equality2;
};

/* n! for the small child counts a join tree produces; saturates rather than
 * overflowing, which only ever makes the variant space look smaller. */
static uint64_t factorial(size_t n) {
    uint64_t f = 1;
    for (size_t i = 2; i <= n && f < (1ull << 40); i++) f *= i;
    return f;
}

/*
 * Reorder `v` into its `k`-th lexicographic permutation (k < v.size()!).
 * Used to enumerate the orderings of a node's children.
 */
template <typename T>
static void nth_permutation(std::vector<T>& v, uint64_t k) {
    std::vector<T> pool = v;
    v.clear();
    for (size_t n = pool.size(); n > 0; n--) {
        uint64_t f = factorial(n - 1);
        size_t pick = (size_t)(k / f);
        k %= f;
        v.push_back(pool[pick]);
        pool.erase(pool.begin() + (long)pick);
    }
}

/* One edge from the current node down to an unvisited neighbour. */
struct ChildEdge {
    int32_t        table_idx;
    JoinConstraint constraint;   /* oriented source = child, target = me */
};

/*
 * Post-order DFS: children are appended before the node itself.
 * `visited` prevents revisiting a table (cycles / already-processed tables).
 *
 * `perm_state` selects the ordering of children at each node: every node with
 * k > 1 children consumes a mixed-radix digit of base k!.  Passing 0 reproduces
 * constraint order exactly, i.e. the original behaviour.
 */
static void collect_postorder(
    int32_t                          table_idx,
    const std::string&               parent_table,
    int32_t                          join_col,
    int32_t                          parent_join_col,
    int32_t d1, int32_t e1, int32_t d2, int32_t e2,
    const std::vector<LoadedTable>&  tables,
    const std::vector<JoinConstraint>& constraints,
    std::set<int32_t>&               visited,
    std::vector<NodeInfo>&           nodes,
    uint64_t&                        perm_state
) {
    const std::string& my_name = tables[table_idx].name;

    /* Collect the unvisited neighbours in constraint order. */
    std::vector<ChildEdge> kids;
    for (const auto& c : constraints) {
        std::string other;
        bool i_am_source;

        if (c.get_source_table() == my_name) {
            other = c.get_target_table();
            i_am_source = true;
        } else if (c.get_target_table() == my_name) {
            other = c.get_source_table();
            i_am_source = false;
        } else {
            continue;
        }

        /* Find other table's index. */
        int32_t other_idx = -1;
        for (int32_t i = 0; i < (int32_t)tables.size(); i++) {
            if (tables[i].name == other) { other_idx = i; break; }
        }
        if (other_idx == -1 || visited.count(other_idx))
            continue;

        /*
         * Orient the constraint so that source = child (other) and
         * target = parent (me).  That matches the join_node_desc_t convention:
         *   child.attr[join_col] IN [parent.attr[parent_join_col] + d1, ... + d2]
         */
        kids.push_back({other_idx, i_am_source ? c.reverse() : c});
    }

    /* Pick this node's child ordering from the variant index. */
    if (kids.size() > 1) {
        uint64_t f = factorial(kids.size());
        nth_permutation(kids, perm_state % f);
        perm_state /= f;
    }

    for (const auto& kid : kids) {
        /* An earlier sibling's subtree may already have claimed this table. */
        if (visited.count(kid.table_idx))
            continue;

        int32_t cc = col_index(tables[kid.table_idx].desc,
                               kid.constraint.get_source_column());
        int32_t pc = col_index(tables[table_idx].desc,
                               kid.constraint.get_target_column());

        if (cc < 0)
            throw std::runtime_error(
                "Column '" + kid.constraint.get_source_column() +
                "' not found in table '" + tables[kid.table_idx].name + "'");
        if (pc < 0)
            throw std::runtime_error(
                "Column '" + kid.constraint.get_target_column() +
                "' not found in table '" + my_name + "'");

        visited.insert(kid.table_idx);
        collect_postorder(
            kid.table_idx, my_name, cc, pc,
            kid.constraint.get_deviation1(), kid.constraint.get_equality1(),
            kid.constraint.get_deviation2(), kid.constraint.get_equality2(),
            tables, constraints, visited, nodes, perm_state);
    }

    /* Emit self after all children. */
    nodes.push_back({table_idx, parent_table, join_col, parent_join_col,
                     d1, e1, d2, e2});
}

/*
 * Number of child-ordering variants for a given root: the product of k! over
 * every node with k children.  Computed by running the DFS once and counting.
 */
static uint64_t child_orderings_for_root(
    int32_t                            root_idx,
    const ParsedQuery&                 query,
    const std::vector<LoadedTable>&    tables
) {
    std::set<int32_t> visited;
    visited.insert(root_idx);
    std::vector<NodeInfo> nodes;

    /* Walk with perm_state = 0 and count the branch factors encountered.  The
     * factorials multiply, so a chain rooted at an endpoint yields 1. */
    uint64_t total = 1;
    std::vector<int32_t> frontier{root_idx};
    while (!frontier.empty()) {
        int32_t cur = frontier.back();
        frontier.pop_back();
        const std::string& my_name = tables[cur].name;
        size_t k = 0;
        for (const auto& c : query.join_conditions) {
            std::string other;
            if (c.get_source_table() == my_name)      other = c.get_target_table();
            else if (c.get_target_table() == my_name) other = c.get_source_table();
            else continue;
            int32_t oi = -1;
            for (int32_t i = 0; i < (int32_t)tables.size(); i++)
                if (tables[i].name == other) { oi = i; break; }
            if (oi < 0 || visited.count(oi)) continue;
            visited.insert(oi);
            frontier.push_back(oi);
            k++;
        }
        if (k > 1) total *= factorial(k);
    }
    (void)nodes;
    return total;
}

std::vector<int32_t> join_tree_variant_shape(
    const ParsedQuery&              query,
    const std::vector<LoadedTable>& tables
) {
    std::map<std::string, int32_t> name_to_idx;
    for (int32_t i = 0; i < (int32_t)tables.size(); i++)
        name_to_idx[tables[i].name] = i;

    std::vector<int32_t> per_root;
    for (const auto& t : query.tables)
        per_root.push_back(
            (int32_t)child_orderings_for_root(name_to_idx.at(t), query, tables));
    return per_root;
}

uint64_t count_join_tree_variants(
    const ParsedQuery&              query,
    const std::vector<LoadedTable>& tables
) {
    uint64_t n = 0;
    for (int32_t c : join_tree_variant_shape(query, tables))
        n += (uint64_t)c;
    return n;
}

std::vector<join_node_desc_t> serialize_join_tree(
    const ParsedQuery&              query,
    const std::vector<LoadedTable>& tables,
    uint64_t                        variant
) {
    if (query.tables.empty())
        throw std::runtime_error("query has no tables");

    /* Build name → index map for the loaded tables. */
    std::map<std::string, int32_t> name_to_idx;
    for (int32_t i = 0; i < (int32_t)tables.size(); i++)
        name_to_idx[tables[i].name] = i;

    for (const auto& t : query.tables)
        if (!name_to_idx.count(t))
            throw std::runtime_error("table '" + t + "' not found in loaded tables");

    /*
     * Variant 0 is the historical tree: root = first table in the FROM clause,
     * children in constraint order.  Higher variants walk the space of rooted
     * orientations -- first every child ordering for root 0, then root 1, and
     * so on -- so every published number is reproducible at variant 0.
     */
    size_t   root_pos  = 0;
    uint64_t perm_state = variant;
    {
        std::vector<int32_t> shape = join_tree_variant_shape(query, tables);
        while (root_pos < shape.size() && perm_state >= (uint64_t)shape[root_pos]) {
            perm_state -= (uint64_t)shape[root_pos];
            root_pos++;
        }
        if (root_pos >= shape.size())
            throw std::runtime_error(
                "join tree variant " + std::to_string(variant) +
                " is out of range (" + std::to_string(count_join_tree_variants(query, tables)) +
                " variants exist)");
    }

    int32_t root_idx = name_to_idx[query.tables[root_pos]];

    std::set<int32_t> visited;
    visited.insert(root_idx);

    std::vector<NodeInfo> nodes;
    collect_postorder(
        root_idx, /*parent_table=*/"", /*join_col=*/-1, /*parent_join_col=*/-1,
        0, NONE, 0, NONE,
        tables, query.join_conditions, visited, nodes, perm_state);

    /* Check that every query table was reached. */
    if (visited.size() != query.tables.size()) {
        throw std::runtime_error(
            "join graph is disconnected: reached " +
            std::to_string(visited.size()) + " of " +
            std::to_string(query.tables.size()) + " tables");
    }

    /*
     * Convert NodeInfo → join_node_desc_t.
     * We know each table appears exactly once, so we can map
     * table_idx → position in `nodes` to resolve parent_idx.
     */
    std::map<int32_t, int32_t> table_to_node_pos;
    for (int32_t i = 0; i < (int32_t)nodes.size(); i++)
        table_to_node_pos[nodes[i].table_idx] = i;

    std::vector<join_node_desc_t> result;
    result.reserve(nodes.size());

    for (const auto& n : nodes) {
        join_node_desc_t jn;
        memset(&jn, 0, sizeof(jn));
        jn.table_idx       = n.table_idx;
        jn.join_col        = n.join_col;
        jn.parent_join_col = n.parent_join_col;
        jn.deviation1      = n.deviation1;
        jn.equality1       = n.equality1;
        jn.deviation2      = n.deviation2;
        jn.equality2       = n.equality2;

        if (n.parent_table.empty()) {
            jn.parent_idx = -1;
        } else {
            int32_t parent_table_idx = name_to_idx.at(n.parent_table);
            jn.parent_idx = table_to_node_pos.at(parent_table_idx);
        }
        result.push_back(jn);
    }

    return result;
}
