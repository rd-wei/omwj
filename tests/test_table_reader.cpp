/*
 * test_table_reader — load a data directory and print a summary.
 *
 * Usage:  ./test_table_reader <data_dir>
 *
 * Output format (one line per table, sorted by name):
 *   <table_name>: <num_rows> rows, <num_cols> cols, encrypted=<0|1>
 *   first row: attr[0]=<v> attr[1]=<v> ... nonce=<n>
 *
 * Compare this against the original repo's output to verify correctness.
 */
#include <iostream>
#include <iomanip>
#include "../app/io/table_reader.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <data_dir>\n";
        return 1;
    }

    std::vector<LoadedTable> tables;
    try {
        tables = load_tables_from_dir(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    for (auto& t : tables) {
        std::cout << t.name << ": "
                  << t.desc.num_rows << " rows, "
                  << t.desc.num_cols << " cols, "
                  << "encrypted=" << (t.rows.empty() ? 0 : (int)t.rows[0].is_encrypted) << "\n";

        std::cout << "  cols:";
        for (uint32_t c = 0; c < t.desc.num_cols; c++)
            std::cout << " " << t.desc.col_names[c];
        std::cout << "\n";

        if (!t.rows.empty()) {
            const entry_t& e = t.rows[0];
            std::cout << "  row0:";
            for (uint32_t c = 0; c < t.desc.num_cols; c++)
                std::cout << " " << e.attributes[c];
            if (e.is_encrypted) std::cout << "  nonce=" << e.nonce;
            std::cout << "\n";
        }
    }
    return 0;
}
