/*
 * test_query_parser — parse each SQL file and print the parse tree.
 * Run it on the same files as the original, diff the output.
 *
 * Usage:  ./test_query_parser <sql_file> [sql_file ...]
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "../app/query/query_parser.h"

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <sql_file> [sql_file ...]\n";
        return 1;
    }

    int failures = 0;
    for (int i = 1; i < argc; i++) {
        std::string path = argv[i];
        std::cout << "=== " << path << " ===\n";
        try {
            std::string sql = read_file(path);
            QueryParser parser;
            ParsedQuery q = parser.parse(sql);

            std::cout << "tables (" << q.tables.size() << "):";
            for (auto& t : q.tables) std::cout << " " << t;
            std::cout << "\n";

            std::cout << "join_conditions (" << q.join_conditions.size() << "):\n";
            for (auto& jc : q.join_conditions) {
                std::cout << "  " << jc.to_string() << "\n";
            }

            if (!q.filter_conditions.empty()) {
                std::cout << "filter_conditions (" << q.filter_conditions.size() << "):\n";
                for (auto& f : q.filter_conditions) std::cout << "  " << f << "\n";
            }

            std::cout << "valid: " << (q.is_valid() ? "yes" : "no") << "\n";
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            ++failures;
        }
        std::cout << "\n";
    }

    return failures ? 1 : 0;
}
