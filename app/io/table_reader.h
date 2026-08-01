#ifndef EJ_TABLE_READER_H
#define EJ_TABLE_READER_H

/*
 * table_reader — load encrypted (or plaintext) CSV tables into flat entry_t arrays.
 *
 * No SGX dependency.  Reads CSV files written by the encrypt_tables tool:
 *   - header row: col0,col1,...,colN[,nonce]
 *   - data rows:  int32,int32,...,int32[,uint64_nonce]
 *
 * Fills entry_t.attributes[0..num_cols-1] with column values.
 * Sets is_encrypted=1 and nonce from the nonce column (if present).
 * All metadata fields (join_attr, local_mult, ...) are left as NULL_VALUE;
 * the enclave sets them when it starts running the join.
 */

#include "../../common/entry_t.h"
#include "../../common/ecall_types.h"
#include <string>
#include <vector>
#include <unordered_map>

struct LoadedTable {
    std::string      name;        /* derived from filename */
    table_desc_t     desc;        /* schema + row count (offset filled in later) */
    std::vector<entry_t> rows;   /* encrypted rows */
};

/*
 * Load all CSV files from a directory.
 * Returns one LoadedTable per file (filename stem → table name).
 * Throws std::runtime_error on I/O or parse failure.
 */
std::vector<LoadedTable> load_tables_from_dir(const std::string& dir_path);

/*
 * Load a single CSV file.
 * table_name may be empty; if so, it is derived from the filename.
 */
LoadedTable load_table_from_csv(const std::string& csv_path,
                                const std::string& table_name = "");

#endif /* EJ_TABLE_READER_H */
