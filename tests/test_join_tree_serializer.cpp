/*
 * test_join_tree_serializer — parse SQL + load tables, serialise the join tree,
 * print the flat node array.
 *
 * Usage:  ./test_join_tree_serializer <sql_file> <data_dir>
 *
 * Output (one line per node, leaves first):
 *   node[i]: table=<name> parent=<j|-1> join_col=<c>(<name>) parent_col=<c>(<name>)
 *            deviation=[<d1>,<d2>] equality=[<e1>,<e2>]
 *
 * Cross-check:
 *   - Number of nodes == number of tables in query.
 *   - Root node (last) has parent_idx == -1.
 *   - Every non-root node's parent_idx is valid and < its own index (post-order).
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "../app/query/query_parser.h"
#include "../app/io/table_reader.h"
#include "../app/join/join_tree_serializer.h"
#include "../common/constants.h"

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static const char* eq_str(int32_t e) {
    if (e == EQ)  return "EQ";
    if (e == NEQ) return "NEQ";
    return "NONE";
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <sql_file> <data_dir>\n";
        return 1;
    }

    std::string sql_file = argv[1];
    std::string data_dir = argv[2];

    /* Parse query. */
    std::string sql;
    try { sql = read_file(sql_file); }
    catch (const std::exception& e) {
        std::cerr << "ERROR reading SQL: " << e.what() << "\n";
        return 1;
    }

    QueryParser parser;
    ParsedQuery q;
    try { q = parser.parse(sql); }
    catch (const std::exception& e) {
        std::cerr << "ERROR parsing SQL: " << e.what() << "\n";
        return 1;
    }

    if (!q.is_valid()) {
        std::cerr << "ERROR: query is invalid\n";
        return 1;
    }

    std::cout << "query tables (" << q.tables.size() << "):";
    for (auto& t : q.tables) std::cout << " " << t;
    std::cout << "\n";

    /* Load tables. */
    std::vector<LoadedTable> tables;
    try { tables = load_tables_from_dir(data_dir); }
    catch (const std::exception& e) {
        std::cerr << "ERROR loading tables: " << e.what() << "\n";
        return 1;
    }

    /* Serialise. */
    std::vector<join_node_desc_t> tree;
    try { tree = serialize_join_tree(q, tables); }
    catch (const std::exception& e) {
        std::cerr << "ERROR serialising join tree: " << e.what() << "\n";
        return 1;
    }

    /* Print. */
    std::cout << "\njoin tree (" << tree.size() << " nodes, post-order):\n";
    int errors = 0;
    for (int i = 0; i < (int)tree.size(); i++) {
        const join_node_desc_t& n = tree[i];

        /* Resolve table name and column names for display. */
        const LoadedTable& lt = tables[n.table_idx];
        std::string join_col_name  = (n.join_col  >= 0) ? lt.desc.col_names[n.join_col]  : "-";
        std::string par_col_name;
        if (n.parent_idx >= 0) {
            const LoadedTable& plt = tables[tree[n.parent_idx].table_idx];
            par_col_name = (n.parent_join_col >= 0) ? plt.desc.col_names[n.parent_join_col] : "-";
        } else {
            par_col_name = "-";
        }

        std::cout << "  node[" << i << "]: table=" << lt.name
                  << " parent=" << n.parent_idx
                  << " join_col=" << n.join_col << "(" << join_col_name << ")"
                  << " parent_col=" << n.parent_join_col << "(" << par_col_name << ")"
                  << " deviation=[" << n.deviation1 << "," << n.deviation2 << "]"
                  << " equality=[" << eq_str(n.equality1) << "," << eq_str(n.equality2) << "]\n";

        /* Verify post-order invariant: parent comes after child. */
        if (n.parent_idx != -1 && n.parent_idx <= i) {
            std::cout << "    ERROR: parent_idx " << n.parent_idx
                      << " is not after child " << i << " (post-order violated)\n";
            errors++;
        }
    }

    /* Root must be last with parent_idx == -1. */
    if (!tree.empty()) {
        const auto& root = tree.back();
        if (root.parent_idx != -1) {
            std::cout << "ERROR: last node (root) has parent_idx=" << root.parent_idx
                      << " (expected -1)\n";
            errors++;
        }
        std::cout << "\nroot: table=" << tables[root.table_idx].name << "\n";
    }

    if ((int)tree.size() != (int)q.tables.size()) {
        std::cout << "ERROR: node count " << tree.size()
                  << " != table count " << q.tables.size() << "\n";
        errors++;
    }

    std::cout << (errors == 0 ? "\nOK\n" : "\nFAIL\n");
    return errors ? 1 : 0;
}
