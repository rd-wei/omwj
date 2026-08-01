#include "table_reader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <climits>
#include <dirent.h>
#include <cstdlib>

/* ── helpers ────────────────────────────────────────────────────────────── */

static std::string stem(const std::string& path) {
    /* /foo/bar/customer.csv  →  customer */
    size_t slash = path.rfind('/');
    std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = file.rfind('.');
    return (dot == std::string::npos) ? file : file.substr(0, dot);
}

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) fields.push_back(tok);
    return fields;
}

static int32_t to_i32(const std::string& s) {
    /* strtol with range check; non-numeric → 0 */
    char* end;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return (int32_t)INT32_MIN;
    return (int32_t)v;
}

static uint64_t to_u64(const std::string& s) {
    char* end;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    return (uint64_t)v;
}

/* ── public API ─────────────────────────────────────────────────────────── */

LoadedTable load_table_from_csv(const std::string& path, const std::string& name_hint) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open: " + path);

    LoadedTable t;
    t.name = name_hint.empty() ? stem(path) : name_hint;

    /* ── parse header row ── */
    std::string header_line;
    if (!std::getline(f, header_line))
        throw std::runtime_error("empty file: " + path);

    auto cols = split_csv(header_line);
    bool has_nonce = (!cols.empty() && cols.back() == "nonce");

    /* Strip trailing \r */
    for (auto& c : cols) {
        if (!c.empty() && c.back() == '\r') c.pop_back();
    }
    int data_cols = (int)cols.size() - (has_nonce ? 1 : 0);
    if (data_cols <= 0 || data_cols > MAX_COLS)
        throw std::runtime_error("bad column count in: " + path);

    /* Fill table_desc column names */
    memset(&t.desc, 0, sizeof(t.desc));
    t.desc.num_cols   = (uint32_t)data_cols;
    t.desc.offset_rows = 0;  /* caller sets this when building the flat array */
    for (int i = 0; i < data_cols; i++) {
        std::string cname = cols[i];
        if (cname.size() >= MAX_COL_NAME) cname.resize(MAX_COL_NAME - 1);
        memcpy(t.desc.col_names[i], cname.c_str(), cname.size() + 1);
    }

    /* ── parse data rows ── */
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line == "\r") continue;

        auto fields = split_csv(line);
        for (auto& fld : fields)
            if (!fld.empty() && fld.back() == '\r') fld.pop_back();

        /* Skip sentinel/empty rows (all zeros or too few fields) */
        if ((int)fields.size() < data_cols) continue;

        entry_t e;
        memset(&e, 0, sizeof(e));

        /* Metadata defaults */
        e.field_type    = SOURCE;
        e.equality_type = NONE;
        e.join_attr     = NULL_VALUE;
        e.original_index = NULL_VALUE;
        e.local_mult    = NULL_VALUE;
        e.final_mult    = NULL_VALUE;
        e.foreign_sum   = NULL_VALUE;
        e.local_cumsum  = NULL_VALUE;
        e.local_interval = NULL_VALUE;
        e.foreign_interval = NULL_VALUE;
        e.local_weight  = NULL_VALUE;
        e.copy_index    = NULL_VALUE;
        e.alignment_key = NULL_VALUE;
        e.dst_idx       = NULL_VALUE;
        e.index         = NULL_VALUE;
        for (int i = 0; i < MAX_ATTRIBUTES; i++) e.attributes[i] = NULL_VALUE;

        /* Column data */
        for (int i = 0; i < data_cols; i++)
            e.attributes[i] = to_i32(fields[i]);

        /* Nonce / encryption flag */
        if (has_nonce && (int)fields.size() > data_cols) {
            e.nonce        = to_u64(fields[data_cols]);
            e.is_encrypted = 1;
        } else {
            e.nonce        = 0;
            e.is_encrypted = 0;
        }

        t.rows.push_back(e);
    }

    t.desc.num_rows = (uint32_t)t.rows.size();
    return t;
}

std::vector<LoadedTable> load_tables_from_dir(const std::string& dir_path) {
    DIR* d = opendir(dir_path.c_str());
    if (!d) throw std::runtime_error("cannot open directory: " + dir_path);

    std::vector<std::string> csv_paths;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string fname(ent->d_name);
        if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".csv")
            csv_paths.push_back(dir_path + "/" + fname);
    }
    closedir(d);

    std::sort(csv_paths.begin(), csv_paths.end());

    std::vector<LoadedTable> tables;
    for (auto& p : csv_paths)
        tables.push_back(load_table_from_csv(p));
    return tables;
}
