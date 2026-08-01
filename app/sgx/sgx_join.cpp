/*
 * sgx_join.cpp — host application for enclave_join.
 *
 * Usage:  ./sgx_join <query.sql> <data_dir> <output.csv>
 *
 * Loads encrypted (or plaintext) tables, parses the SQL, serialises the
 * join tree, hands everything to the enclave via the one big ecall, and
 * writes the streamed result tuples to the output CSV.
 */

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <vector>

#include <sgx_urts.h>
#include "Enclave_u.h"

#include "query/query_parser.h"
#include "io/table_reader.h"
#include "join/join_tree_serializer.h"
#include "entry_t.h"
#include "ecall_types.h"
#include "result_writer.h"

static const char* ENCLAVE_PATH = "enclave.signed.so";

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* One column of the enclave's result layout. */
struct ResultCol {
    std::string table;
    std::string name;
};

/*
 * Column layout of the enclave's align-concat result: the root's columns
 * first, then — iterating parents in pre-order — each parent's children's
 * columns in tree-array order (matches the enclave's top-down attach
 * order in align.c).
 */
static void preorder_nodes(const std::vector<join_node_desc_t>& tree,
                           size_t node, std::vector<size_t>& seq) {
    seq.push_back(node);
    for (size_t k = 0; k < tree.size(); k++)
        if (tree[k].parent_idx == (int32_t)node)
            preorder_nodes(tree, k, seq);
}

static std::vector<ResultCol> result_layout(
        const std::vector<join_node_desc_t>& tree,
        const std::vector<LoadedTable>&      tables) {
    std::vector<ResultCol> layout;
    auto add_table = [&](size_t node) {
        const LoadedTable& lt = tables[tree[node].table_idx];
        for (uint32_t c = 0; c < lt.desc.num_cols; c++)
            layout.push_back({lt.name, lt.desc.col_names[c]});
    };
    std::vector<size_t> seq;
    for (size_t k = 0; k < tree.size(); k++)
        if (tree[k].parent_idx < 0)
            preorder_nodes(tree, k, seq);
    if (seq.empty()) return layout;
    add_table(seq[0]);                       /* root's own columns */
    for (size_t v : seq)                     /* then children, per parent */
        for (size_t k = 0; k < tree.size(); k++)
            if (tree[k].parent_idx == (int32_t)v)
                add_table(k);
    return layout;
}

/* Resolve the SELECT list against the result layout. */
static std::vector<output_col_t> build_out_cols(
        const ParsedQuery&            query,
        const std::vector<ResultCol>& layout) {
    std::vector<output_col_t> out;

    auto add = [&](int32_t attr_idx, const std::string& name) {
        output_col_t oc;
        oc.attr_idx = attr_idx;
        std::snprintf(oc.col_name, sizeof(oc.col_name), "%s", name.c_str());
        out.push_back(oc);
    };

    if (query.is_select_star()) {
        for (size_t i = 0; i < layout.size(); i++)
            add((int32_t)i, layout[i].name);
        return out;
    }

    for (const auto& sel : query.select_columns) {
        /* Accept "table.col" or bare "col" (first layout match). */
        std::string tbl, col = sel;
        size_t dot = sel.find('.');
        if (dot != std::string::npos) {
            tbl = sel.substr(0, dot);
            col = sel.substr(dot + 1);
        }
        bool found = false;
        for (size_t i = 0; i < layout.size(); i++) {
            if (layout[i].name != col) continue;
            if (!tbl.empty() && layout[i].table != tbl) continue;
            add((int32_t)i, layout[i].name);
            found = true;
            break;
        }
        if (!found)
            throw std::runtime_error("SELECT column not found: " + sel);
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 4 || argc > 5) {
        fprintf(stderr,
                "Usage: %s <query.sql> <data_dir> <output.csv> [tree_variant]\n"
                "\n"
                "  tree_variant  which rooted orientation of the join tree to run.\n"
                "                0 (default) = root is the first table in the FROM\n"
                "                clause, children in constraint order.  Pass 'list'\n"
                "                to print how many variants the query has and exit.\n",
                argv[0]);
        return 1;
    }

    /* ── Parse SQL ── */
    std::string sql;
    try { sql = read_file(argv[1]); }
    catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what()); return 1;
    }

    QueryParser parser;
    ParsedQuery query;
    try { query = parser.parse(sql); }
    catch (const std::exception& e) {
        fprintf(stderr, "ERROR parsing SQL: %s\n", e.what()); return 1;
    }
    if (!query.is_valid()) {
        fprintf(stderr, "ERROR: invalid query\n"); return 1;
    }

    /* ── Load tables ── */
    std::vector<LoadedTable> tables;
    try { tables = load_tables_from_dir(argv[2]); }
    catch (const std::exception& e) {
        fprintf(stderr, "ERROR loading tables: %s\n", e.what()); return 1;
    }

    /* ── Serialise join tree ──
     *
     * The tree may be rooted at any table and a node's children visited in any
     * order; both change how much work the bottom-up / top-down passes do.
     * Variant 0 is the historical choice, so existing numbers reproduce.
     */
    uint64_t tree_variant = 0;
    if (argc == 5) {
        if (std::string(argv[4]) == "list") {
            uint64_t n = count_join_tree_variants(query, tables);
            printf("TREE_VARIANTS: %llu\n", (unsigned long long)n);
            std::vector<int32_t> shape = join_tree_variant_shape(query, tables);
            for (size_t i = 0; i < shape.size(); i++)
                printf("  root=%-12s child_orderings=%d\n",
                       query.tables[i].c_str(), shape[i]);
            return 0;
        }
        tree_variant = strtoull(argv[4], NULL, 10);
    }

    std::vector<join_node_desc_t> join_tree;
    try { join_tree = serialize_join_tree(query, tables, tree_variant); }
    catch (const std::exception& e) {
        fprintf(stderr, "ERROR serialising join tree: %s\n", e.what()); return 1;
    }
    printf("TREE_VARIANT: %llu of %llu (root=%s)\n",
           (unsigned long long)tree_variant,
           (unsigned long long)count_join_tree_variants(query, tables),
           tables[join_tree.back().table_idx].name.c_str());

    /* ── Output projection ── */
    std::vector<output_col_t> out_cols;
    std::vector<std::string>  out_names;
    try {
        std::vector<ResultCol> layout = result_layout(join_tree, tables);
        if (layout.size() > MAX_ATTRIBUTES) {
            throw std::runtime_error(
                "query output width " + std::to_string(layout.size()) +
                " exceeds MAX_ATTRIBUTES (" + std::to_string(MAX_ATTRIBUTES) + ")");
        }
        out_cols = build_out_cols(query, layout);
        for (const auto& oc : out_cols) out_names.push_back(oc.col_name);
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what()); return 1;
    }

    /* ── Build flat entry array + table_descs ── */
    std::vector<entry_t>      flat;
    std::vector<table_desc_t> descs;

    for (auto& lt : tables) {
        table_desc_t d = lt.desc;
        d.offset_rows = (uint32_t)flat.size();
        descs.push_back(d);
        for (const auto& row : lt.rows)
            flat.push_back(row);
    }

    /* ── Print summary ── */
    printf("query: %s\n", argv[1]);
    printf("tables (%zu):", tables.size());
    for (auto& lt : tables) printf(" %s(%u)", lt.name.c_str(), lt.desc.num_rows);
    printf("\ntotal rows: %zu\n", flat.size());
    printf("join tree nodes: %zu\n", join_tree.size());
    printf("output columns: %zu\n", out_cols.size());

    /* ── Open result writer ── */
    try { result_writer_open(argv[3], out_names); }
    catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what()); return 1;
    }

    /* ── Create enclave ── */
    auto clock_s = []() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    };

    double t_init0 = clock_s();
    sgx_enclave_id_t eid = 0;
    sgx_status_t     ret = sgx_create_enclave(
        ENCLAVE_PATH, SGX_DEBUG_FLAG, nullptr, nullptr, &eid, nullptr);
    if (ret != SGX_SUCCESS) {
        fprintf(stderr, "ERROR: sgx_create_enclave failed (0x%x)\n", ret);
        result_writer_close();
        return 1;
    }
    printf("ENCLAVE_INIT: %.3f s\n", clock_s() - t_init0);

    /* ── Call ecall_run_join ── */
    int32_t result_count = 0;
    sgx_status_t ecall_ret = SGX_SUCCESS;
    double t_ecall0 = clock_s();
    ret = ecall_run_join(
        eid, &ecall_ret,
        flat.data(),       flat.size(),
        descs.data(),      descs.size(),
        join_tree.data(),  join_tree.size(),
        out_cols.data(),   out_cols.size(),
        &result_count);
    printf("ECALL_TOTAL: %.3f s\n", clock_s() - t_ecall0);

    sgx_destroy_enclave(eid);
    result_writer_close();

    if (ret != SGX_SUCCESS || ecall_ret != SGX_SUCCESS) {
        fprintf(stderr, "ERROR: ecall_run_join failed (ret=0x%x ecall=0x%x)\n",
                ret, ecall_ret);
        return 1;
    }

    printf("result: %d rows written to %s\n", result_count, argv[3]);
    return 0;
}
