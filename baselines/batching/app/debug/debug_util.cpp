#include "debug_util.h"
#include "types_common.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <cassert>
#include <functional>
#include "../data_structures/data_structures.h"
#include "../join/join_tree_node.h"
#include "../crypto/crypto_utils.h"
#include "Enclave_u.h"

// Thread-safe output mutex
static std::mutex debug_mutex;
// Separate mutex for file dump operations to avoid deadlock
static std::mutex dump_mutex;

// Debug session state
static std::string debug_session_name;
static std::string debug_session_dir;
static std::ofstream debug_log_file;
static bool debug_session_active = false;

// ANSI color codes for terminal output
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_RESET   "\033[0m"

// Get color for debug level
static const char* get_level_color(uint32_t level) {
    switch(level) {
        case DEBUG_LEVEL_ERROR: return COLOR_RED;
        case DEBUG_LEVEL_WARN:  return COLOR_YELLOW;
        case DEBUG_LEVEL_INFO:  return COLOR_GREEN;
        case DEBUG_LEVEL_DEBUG: return COLOR_BLUE;
        case DEBUG_LEVEL_TRACE: return COLOR_MAGENTA;
        default: return COLOR_RESET;
    }
}

// Get short filename (last component only)
static const char* get_short_filename(const char* file) {
    const char* last_slash = strrchr(file, '/');
    return last_slash ? last_slash + 1 : file;
}

// Main debug print function for app environment
extern "C" void debug_print(uint32_t level, const char* file, int line, const char* fmt, ...) {
    if (level > DEBUG_LEVEL) return;
    
    std::lock_guard<std::mutex> lock(debug_mutex);
    
    // Get current time
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    // Format the message
    char message[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    // Output to file if in file mode
    if ((DEBUG_OUTPUT_MODE == DEBUG_OUTPUT_FILE || DEBUG_OUTPUT_MODE == DEBUG_OUTPUT_BOTH) 
        && debug_session_active && debug_log_file.is_open()) {
        debug_log_file << "[" << time_str << "]"
                      << "[" << debug_level_str(level) << "]"
                      << "[" << get_short_filename(file) << ":" << line << "] "
                      << message << std::endl;
        debug_log_file.flush();
    }
    
    // Always output warnings and errors to console, regardless of DEBUG_OUTPUT_MODE
    // Other levels follow DEBUG_OUTPUT_MODE setting
    bool output_to_console = (level <= DEBUG_LEVEL_WARN) || 
                             (DEBUG_OUTPUT_MODE == DEBUG_OUTPUT_CONSOLE || DEBUG_OUTPUT_MODE == DEBUG_OUTPUT_BOTH);
    
    if (output_to_console) {
        FILE* stream = (level <= DEBUG_LEVEL_ERROR) ? stderr : stdout;
        
        // Print header with color
        fprintf(stream, "%s[%s][%s][%s:%d] ", 
                get_level_color(level),
                time_str,
                debug_level_str(level),
                get_short_filename(file),
                line);
        
        // Print the message
        fprintf(stream, "%s", message);
        
        // Reset color and newline
        fprintf(stream, "%s\n", COLOR_RESET);
        fflush(stream);
    }
}

// OCALL handler for enclave debug output
extern "C" void ocall_debug_print(uint32_t level, const char* file, int line, const char* message) {
    // Just forward to the main debug_print with the pre-formatted message
    debug_print(level, file, line, "%s", message);
}

// Debug session management
void debug_init_session(const char* session_name) {
#if DEBUG_LEVEL > DEBUG_LEVEL_NONE
    std::lock_guard<std::mutex> lock(debug_mutex);
    
    if (debug_session_active) {
        debug_close_session();
    }
    
    // Create timestamp for session
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
    
    // Set up session directory with timestamp first for easier sorting.
    // Repo-relative (the app runs from the repo root); DEBUG builds only.
    debug_session_name = session_name;
    debug_session_dir = std::string("debug/") + timestamp + "_" + session_name;
    
    // Create session directory using POSIX mkdir
    mkdir(debug_session_dir.c_str(), 0777);
    
    // Open main log file
    std::string log_path = debug_session_dir + "/debug.log";
    debug_log_file.open(log_path, std::ios::out);
    
    if (debug_log_file.is_open()) {
        debug_session_active = true;
        debug_log_file << "=== Debug Session Started ===" << std::endl;
        debug_log_file << "Session: " << session_name << std::endl;
        debug_log_file << "Time: " << timestamp << std::endl;
        debug_log_file << "===========================" << std::endl;
        debug_log_file.flush();
    }
#else
    // Debug disabled - do nothing
    (void)session_name;
#endif
}

void debug_close_session() {
#if DEBUG_LEVEL > DEBUG_LEVEL_NONE
    std::lock_guard<std::mutex> lock(debug_mutex);
    
    if (debug_session_active && debug_log_file.is_open()) {
        debug_log_file << "=== Debug Session Ended ===" << std::endl;
        debug_log_file.close();
    }
    
    debug_session_active = false;
    debug_session_name.clear();
    debug_session_dir.clear();
#endif
}

// File output functions
void debug_to_file(const char* filename, const char* content) {
    if (!debug_session_active) return;
    
    std::lock_guard<std::mutex> lock(debug_mutex);
    std::string filepath = debug_session_dir + "/" + filename;
    std::ofstream file(filepath, std::ios::out);
    if (file.is_open()) {
        file << content;
        file.close();
    }
}

void debug_append_to_file(const char* filename, const char* content) {
    if (!debug_session_active) return;
    
    std::lock_guard<std::mutex> lock(debug_mutex);
    std::string filepath = debug_session_dir + "/" + filename;
    std::ofstream file(filepath, std::ios::app);
    if (file.is_open()) {
        file << content;
        file.close();
    }
}

// Helper function to decrypt an entry for debugging
static Entry decrypt_entry_for_debug(const Entry& entry, uint32_t eid) {
    if (!entry.is_encrypted || eid == 0) {
        return entry;
    }
    
    Entry decrypted = entry;
    CryptoUtils::decrypt_entry(decrypted, eid);
    return decrypted;
}

// Table dumping functions
void debug_dump_table(const Table& table, const char* label, const char* step_name, uint32_t eid,
                      const std::vector<MetadataColumn>& columns, bool include_attributes) {
    if (!debug_session_active || !DEBUG_DUMP_TABLES) return;
    
    std::string filename_str;
    {
        std::lock_guard<std::mutex> lock(dump_mutex);
        
        // Generate filename
        std::stringstream filename;
        filename << step_name << "_" << label << ".csv";
        filename_str = filename.str();
        
        // Open file
        std::string filepath = debug_session_dir + "/" + filename_str;
        std::ofstream file(filepath, std::ios::out);
        
        if (!file.is_open()) return;
        
        // Write header based on requested columns
        bool first = true;
        for (const auto& col : columns) {
            if (!first) file << ",";
            first = false;
            switch (col) {
                case META_INDEX: file << "Index"; break;
                case META_ORIG_IDX: file << "OrigIdx"; break;
                case META_LOCAL_MULT: file << "LocalMult"; break;
                case META_FINAL_MULT: file << "FinalMult"; break;
                case META_LOCAL_CUMSUM: file << "LocalCumsum"; break;
                case META_LOCAL_INTERVAL: file << "LocalInterval"; break;
                case META_FOREIGN_SUM: file << "ForeignSum"; break;
                case META_FOREIGN_INTERVAL: file << "ForeignInterval"; break;
                case META_LOCAL_WEIGHT: file << "LocalWeight"; break;
                case META_COPY_INDEX: file << "CopyIndex"; break;
                case META_ALIGN_KEY: file << "AlignKey"; break;
                case META_DST_IDX: file << "DstIdx"; break;
                case META_TABLE_IDX: file << "TableIdx"; break;
                case META_JOIN_ATTR: file << "JoinAttr"; break;
                case META_FIELD_TYPE: file << "FieldType"; break;
                case META_EQ_TYPE: file << "EqType"; break;
                case META_ENCRYPTED: file << "Encrypted"; break;
            }
        }
        
        // If no columns specified, use default set
        if (columns.empty()) {
            file << "Index,OrigIdx,LocalMult,FinalMult,LocalCumsum,LocalInterval,ForeignSum,ForeignCumsum,ForeignInterval,LocalWeight,DstIdx,TableIdx,JoinAttr,FieldType,EqType,Encrypted";
        }
        
        // Add data columns if requested
        if (include_attributes && table.size() > 0) {
            // Use Table schema for column headers
            std::vector<std::string> column_headers = table.get_schema();
            if (column_headers.empty()) {
                // Table must have schema set
                DEBUG_WARN("Table has no schema set, cannot write column headers");
            }
            for (const auto& col_name : column_headers) {
                file << "," << col_name;
            }
        }
        file << std::endl;
        
        // Write data rows
        for (size_t i = 0; i < table.size(); i++) {
            Entry entry = decrypt_entry_for_debug(table[i], eid);
            
            // Write requested columns
            bool first_col = true;
            for (const auto& col : columns) {
                if (!first_col) file << ",";
                first_col = false;
                switch (col) {
                    case META_INDEX: file << i; break;
                    case META_ORIG_IDX: file << entry.original_index; break;
                    case META_LOCAL_MULT: file << entry.local_mult; break;
                    case META_FINAL_MULT: file << entry.final_mult; break;
                    case META_LOCAL_CUMSUM: file << entry.local_cumsum; break;
                    case META_LOCAL_INTERVAL: file << entry.local_interval; break;
                    case META_FOREIGN_SUM: file << entry.foreign_sum; break;
                    case META_FOREIGN_INTERVAL: file << entry.foreign_interval; break;
                    case META_LOCAL_WEIGHT: file << entry.local_weight; break;
                    case META_COPY_INDEX: file << entry.copy_index; break;
                    case META_ALIGN_KEY: file << entry.alignment_key; break;
                    case META_DST_IDX: file << entry.dst_idx; break;
                    case META_TABLE_IDX: file << entry.index; break;
                    case META_JOIN_ATTR: file << entry.join_attr; break;
                    case META_FIELD_TYPE: {
                        // Convert field type to readable string
                        switch(entry.field_type) {
                            case 0: file << "UNKNOWN"; break;
                            case 1: file << "SOURCE"; break;
                            case 2: file << "START"; break;
                            case 3: file << "END"; break;
                            case 4: file << "SORT_PADDING"; break;
                            case 5: file << "DIST_PADDING"; break;
                            default: file << "TYPE_" << static_cast<int>(entry.field_type); break;
                        }
                        break;
                    }
                    case META_EQ_TYPE: {
                        // Convert equality type to readable string
                        switch(entry.equality_type) {
                            case 0: file << "NONE"; break;
                            case 1: file << "EQ"; break;
                            case 2: file << "NEQ"; break;
                            default: file << "EQ_" << static_cast<int>(entry.equality_type); break;
                        }
                        break;
                    }
                    case META_ENCRYPTED: file << (entry.is_encrypted ? "Y" : "N"); break;
                }
            }
            
            // If no columns specified, write all
            if (columns.empty()) {
                file << i << ","
                     << entry.original_index << ","
                     << entry.local_mult << ","
                     << entry.final_mult << ","
                     << entry.local_cumsum << ","
                     << entry.local_interval << ","
                     << entry.foreign_sum << ","
                     << "0" << ","  // foreign_cumsum removed
                     << entry.foreign_interval << ","
                     << entry.local_weight << ","
                     << entry.dst_idx << ","
                     << entry.index << ","
                     << entry.join_attr << ","
                     << static_cast<int>(entry.field_type) << ","
                     << static_cast<int>(entry.equality_type) << ","
                     << (entry.is_encrypted ? "Y" : "N");
            }
            
            // Add data values if requested
            if (include_attributes) {
                for (int32_t val : entry.attributes) {
                    file << "," << val;
                }
            }
            
            file << std::endl;
        }
        
        file.close();
    } // Lock released here
    
    // Also log to main debug log (no longer holding the lock)
    DEBUG_INFO("Dumped table '%s' at step '%s': %zu entries to %s", 
               label, step_name, table.size(), filename_str.c_str());
}

void debug_dump_selected_columns(const Table& table, const char* label, const char* step_name, 
                                 uint32_t eid, const std::vector<std::string>& columns) {
    DEBUG_INFO("debug_dump_selected_columns START: label=%s, table_size=%zu", label, table.size());
    
    if (!debug_session_active || !DEBUG_DUMP_TABLES) {
        DEBUG_INFO("debug_dump_selected_columns EXIT EARLY: active=%d, dump=%d", 
                   debug_session_active, DEBUG_DUMP_TABLES);
        return;
    }
    
    std::string filename_str;
    {
        // Use dump_mutex instead of debug_mutex to avoid deadlock with DEBUG_INFO
        std::lock_guard<std::mutex> lock(dump_mutex);
        
        // Generate filename
        std::stringstream filename;
        filename << step_name << "_" << label << "_selected.csv";
        filename_str = filename.str();
        
        // Open file
        std::string filepath = debug_session_dir + "/" + filename_str;
        DEBUG_INFO("Opening file: %s", filepath.c_str());
        std::ofstream file(filepath, std::ios::out);
        
        if (!file.is_open()) {
            DEBUG_INFO("Failed to open file");
            return;
        }
        
        // Write header with selected columns
        file << "Index";
        for (const auto& col : columns) {
            file << "," << col;
        }
        file << std::endl;
        
        DEBUG_INFO("Starting to write %zu rows", table.size());
        
        // Write data rows
        for (size_t i = 0; i < table.size(); i++) {
            if (i % 100 == 0) {
                DEBUG_INFO("Writing row %zu/%zu", i, table.size());
            }
            Entry entry = decrypt_entry_for_debug(table[i], eid);
            file << i;
            
            // Output selected columns
            for (const auto& col : columns) {
                // Check for special metadata columns
                if (col == "original_index") {
                    file << "," << entry.original_index;
                } else if (col == "local_mult") {
                    file << "," << entry.local_mult;
                } else if (col == "final_mult") {
                    file << "," << entry.final_mult;
                } else if (col == "field_type") {
                    // Convert field_type to string
                    const char* type_str = "UNKNOWN";
                    switch (entry.field_type) {
                        case SORT_PADDING: type_str = "SORT_PADDING"; break;
                        case SOURCE: type_str = "SOURCE"; break;
                        case START: type_str = "START"; break;
                        case END: type_str = "END"; break;
                        case TARGET: type_str = "TARGET"; break;
                        case DIST_PADDING: type_str = "DIST_PADDING"; break;
                        default: 
                            file << "," << static_cast<int32_t>(entry.field_type);
                            continue;
                    }
                    file << "," << type_str;
                } else if (col == "equality_type") {
                    file << "," << entry.equality_type;
                } else if (col == "join_attr") {
                    file << "," << entry.join_attr;
                } else if (col == "dst_idx") {
                    file << "," << entry.dst_idx;
                } else if (col == "local_cumsum") {
                    file << "," << entry.local_cumsum;
                } else if (col == "local_interval") {
                    file << "," << entry.local_interval;
                } else if (col == "foreign_sum") {
                    file << "," << entry.foreign_sum;
                } else if (col == "ALL_ATTRIBUTES") {
                    // Special keyword to dump all attribute values
                    // Use table schema to determine actual attributes
                    std::vector<std::string> schema = table.get_schema();
                    for (size_t j = 0; j < schema.size() && j < MAX_ATTRIBUTES; j++) {
                        file << "," << schema[j] << "=" << entry.attributes[j];
                    }
                } else {
                    // Check if it's a data column by name
                    bool found = false;
                    
                    // Try to find column using Table schema first
                    try {
                        size_t col_idx = table.get_column_index(col);
                        if (col_idx < MAX_ATTRIBUTES) {
                            file << "," << entry.attributes[col_idx];
                            found = true;
                        }
                    } catch (const std::runtime_error& e) {
                        // Column not found in table schema
                        found = false;
                    }
                    
                    if (!found) {
                        file << ",N/A";
                    }
                }
            }
            file << std::endl;
        }
        
        file.close();
        DEBUG_INFO("File closed successfully");
        
        // Log the dump
        DEBUG_DEBUG("Dumped selected columns to %s (size=%zu)", filename_str.c_str(), table.size());
    }
    
    DEBUG_INFO("debug_dump_selected_columns COMPLETE: label=%s", label);
}

void debug_dump_entry(const Entry& entry, const char* label, uint32_t eid) {
    if (!debug_session_active) return;
    
    Entry decrypted = decrypt_entry_for_debug(entry, eid);
    
    std::stringstream ss;
    ss << "Entry " << label << ": ";
    ss << "orig_idx=" << decrypted.original_index;
    ss << ", local_mult=" << decrypted.local_mult;
    ss << ", join_attr=" << decrypted.join_attr;
    ss << ", type=" << static_cast<int>(decrypted.field_type);
    ss << ", eq=" << static_cast<int>(decrypted.equality_type);
    
    // Print all attributes (cannot determine actual count without schema)
    ss << ", data=[";
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (i > 0) ss << ",";
        ss << decrypted.attributes[i];
    }
    ss << "]";
    
    DEBUG_DEBUG("%s", ss.str().c_str());
}

// Implementation for debug_dump_with_mask
void debug_dump_with_mask(const Table& table, const char* label, const char* step_name,
                          uint32_t eid, uint32_t column_mask) {
    if (!debug_session_active || !DEBUG_DUMP_TABLES) return;
    
    // Convert mask to MetadataColumn vector
    std::vector<MetadataColumn> columns;
    if (column_mask & DEBUG_COL_INDEX) columns.push_back(META_INDEX);
    if (column_mask & DEBUG_COL_ORIGINAL_INDEX) columns.push_back(META_ORIG_IDX);
    if (column_mask & DEBUG_COL_LOCAL_MULT) columns.push_back(META_LOCAL_MULT);
    if (column_mask & DEBUG_COL_FINAL_MULT) columns.push_back(META_FINAL_MULT);
    if (column_mask & DEBUG_COL_LOCAL_CUMSUM) columns.push_back(META_LOCAL_CUMSUM);
    if (column_mask & DEBUG_COL_LOCAL_INTERVAL) columns.push_back(META_LOCAL_INTERVAL);
    if (column_mask & DEBUG_COL_FOREIGN_SUM) columns.push_back(META_FOREIGN_SUM);
    if (column_mask & DEBUG_COL_FOREIGN_INTERVAL) columns.push_back(META_FOREIGN_INTERVAL);
    if (column_mask & DEBUG_COL_LOCAL_WEIGHT) columns.push_back(META_LOCAL_WEIGHT);
    if (column_mask & DEBUG_COL_COPY_INDEX) columns.push_back(META_COPY_INDEX);
    if (column_mask & DEBUG_COL_ALIGNMENT_KEY) columns.push_back(META_ALIGN_KEY);
    if (column_mask & DEBUG_COL_DST_IDX) columns.push_back(META_DST_IDX);
    if (column_mask & DEBUG_COL_FIELD_TYPE) columns.push_back(META_FIELD_TYPE);
    if (column_mask & DEBUG_COL_EQUALITY_TYPE) columns.push_back(META_EQ_TYPE);
    if (column_mask & DEBUG_COL_JOIN_ATTR) columns.push_back(META_JOIN_ATTR);
    
    // Check if we should include attributes
    bool include_attributes = (column_mask & DEBUG_COL_ALL_ATTRIBUTES) != 0;
    
    // Call the main debug_dump_table function
    debug_dump_table(table, label, step_name, eid, columns, include_attributes);
}

// ===================================================================
// Encryption Consistency Assertions
// ===================================================================

uint8_t AssertConsistentEncryption(const Table& table) {
    if (table.size() == 0) {
        return 0;  // Empty table defaults to unencrypted
    }
    
    uint8_t first_status = table[0].is_encrypted;
    for (size_t i = 1; i < table.size(); i++) {
        if (table[i].is_encrypted != first_status) {
            DEBUG_ERROR("ASSERTION FAILED: Table '%s' has mixed encryption state at index %zu (first=%d, current=%d)",
                       table.get_table_name().c_str(), i, first_status, table[i].is_encrypted);
            assert(false && "Table has mixed encryption state!");
        }
    }
    return first_status;
}

void AssertTreeConsistentEncryption(JoinTreeNodePtr root) {
    std::function<void(JoinTreeNodePtr)> check_node = [&](JoinTreeNodePtr n) {
        AssertConsistentEncryption(n->get_table());
        for (auto& child : n->get_children()) {
            check_node(child);
        }
    };
    check_node(root);
}