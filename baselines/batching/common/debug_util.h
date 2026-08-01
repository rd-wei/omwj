#ifndef DEBUG_UTIL_H
#define DEBUG_UTIL_H

#include <stdint.h>

// Debug levels - higher level includes all lower levels
#define DEBUG_LEVEL_NONE  0
#define DEBUG_LEVEL_ERROR 1
#define DEBUG_LEVEL_WARN  2
#define DEBUG_LEVEL_INFO  3
#define DEBUG_LEVEL_DEBUG 4
#define DEBUG_LEVEL_TRACE 5

// Set debug level via compilation flag, default to NONE for production
#ifndef DEBUG_LEVEL
    #ifdef DEBUG
        #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
    #else
        #define DEBUG_LEVEL DEBUG_LEVEL_NONE
    #endif
#endif

// Debug output configuration
#define DEBUG_OUTPUT_CONSOLE 0
#define DEBUG_OUTPUT_FILE    1
#define DEBUG_OUTPUT_BOTH    2

// Default to file output to avoid cluttering console
#ifndef DEBUG_OUTPUT_MODE
    #define DEBUG_OUTPUT_MODE DEBUG_OUTPUT_FILE
#endif

// Table dumping configuration
#ifndef DEBUG_DUMP_TABLES
    #define DEBUG_DUMP_TABLES 0  // Disable table dumping for production
#endif

// Debug format for table output
#define DEBUG_FORMAT_CSV  0
#define DEBUG_FORMAT_JSON 1

#ifndef DEBUG_TABLE_FORMAT
    #define DEBUG_TABLE_FORMAT DEBUG_FORMAT_CSV
#endif

// Column selection masks for debug output
// Individual field masks - for precise control
#define DEBUG_COL_ORIGINAL_INDEX   0x00000001
#define DEBUG_COL_LOCAL_MULT       0x00000002
#define DEBUG_COL_FINAL_MULT       0x00000004
#define DEBUG_COL_FOREIGN_SUM      0x00000008
#define DEBUG_COL_LOCAL_CUMSUM     0x00000010
#define DEBUG_COL_LOCAL_INTERVAL   0x00000020
#define DEBUG_COL_FOREIGN_INTERVAL 0x00000040
#define DEBUG_COL_LOCAL_WEIGHT     0x00000100
#define DEBUG_COL_COPY_INDEX       0x00000200
#define DEBUG_COL_ALIGNMENT_KEY    0x00000400
#define DEBUG_COL_DST_IDX          0x00000800
#define DEBUG_COL_INDEX            0x00001000
#define DEBUG_COL_FIELD_TYPE       0x00002000
#define DEBUG_COL_EQUALITY_TYPE    0x00004000
#define DEBUG_COL_JOIN_ATTR        0x00008000
#define DEBUG_COL_ALL_ATTRIBUTES   0x00010000  // Special flag for data columns

// Predefined combinations for common use cases
#define DEBUG_COL_MULTIPLICITIES   (DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT)
#define DEBUG_COL_BOTTOM_UP        (DEBUG_COL_LOCAL_MULT | DEBUG_COL_LOCAL_CUMSUM | DEBUG_COL_LOCAL_INTERVAL)
#define DEBUG_COL_TOP_DOWN         (DEBUG_COL_FINAL_MULT | DEBUG_COL_FOREIGN_SUM | DEBUG_COL_FOREIGN_INTERVAL | DEBUG_COL_LOCAL_WEIGHT)
#define DEBUG_COL_DISTRIBUTE       (DEBUG_COL_DST_IDX | DEBUG_COL_INDEX)
#define DEBUG_COL_ALIGNMENT        (DEBUG_COL_COPY_INDEX | DEBUG_COL_ALIGNMENT_KEY)
#define DEBUG_COL_ESSENTIAL        (DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE | DEBUG_COL_JOIN_ATTR)
#define DEBUG_COL_ALL_METADATA     0x0001FFFF  // All metadata fields (excluding ALL_ATTRIBUTES)

// Function declarations based on environment
#ifdef ENCLAVE_BUILD
    // Enclave environment - will use ocall
    void enclave_debug_print(uint32_t level, const char* file, int line, const char* fmt, ...);
    #define debug_print enclave_debug_print
#else
    // App environment - direct implementation
    #ifdef __cplusplus
    extern "C" {
    #endif
    void debug_print(uint32_t level, const char* file, int line, const char* fmt, ...);
    #ifdef __cplusplus
    }
    #endif
#endif

// Special macro for essential output (results, matches, times) - shows at ERROR level
// This ensures critical information is shown even with minimal debug output
#if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
    #define DEBUG_RESULT(fmt, ...) \
        printf(fmt "\n", ##__VA_ARGS__)
#else
    #define DEBUG_RESULT(fmt, ...) ((void)0)
#endif

// Macro definitions with zero overhead when disabled
#if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
    #define DEBUG_ERROR(fmt, ...) \
        debug_print(DEBUG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_ERROR(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_WARN
    #define DEBUG_WARN(fmt, ...) \
        debug_print(DEBUG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_WARN(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
    #define DEBUG_INFO(fmt, ...) \
        debug_print(DEBUG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_INFO(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG
    #define DEBUG_DEBUG(fmt, ...) \
        debug_print(DEBUG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_DEBUG(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_TRACE
    #define DEBUG_TRACE(fmt, ...) \
        debug_print(DEBUG_LEVEL_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
    #define DEBUG_ENTRY() DEBUG_TRACE("Entering %s", __FUNCTION__)
    #define DEBUG_EXIT() DEBUG_TRACE("Exiting %s", __FUNCTION__)
#else
    #define DEBUG_TRACE(fmt, ...) ((void)0)
    #define DEBUG_ENTRY() ((void)0)
    #define DEBUG_EXIT() ((void)0)
#endif

// Convenience macros for common debugging tasks
#if DEBUG_LEVEL > DEBUG_LEVEL_NONE
    #define DEBUG_ASSERT(cond, fmt, ...) \
        do { \
            if (!(cond)) { \
                DEBUG_ERROR("ASSERTION FAILED: " #cond); \
                DEBUG_ERROR(fmt, ##__VA_ARGS__); \
            } \
        } while(0)
    
    #define DEBUG_HEX_DUMP(label, data, len) \
        do { \
            DEBUG_TRACE("%s (%zu bytes):", label, (size_t)(len)); \
            for (size_t i = 0; i < (size_t)(len); i++) { \
                if (i % 16 == 0) DEBUG_TRACE("  %04zx: ", i); \
                DEBUG_TRACE("%02x ", ((uint8_t*)(data))[i]); \
                if (i % 16 == 15) DEBUG_TRACE("\n"); \
            } \
            if ((len) % 16 != 0) DEBUG_TRACE("\n"); \
        } while(0)
#else
    #define DEBUG_ASSERT(cond, fmt, ...) ((void)0)
    #define DEBUG_HEX_DUMP(label, data, len) ((void)0)
#endif

// Forward declarations for table debugging
#ifdef __cplusplus
#include <vector>
#include <string>
#include <memory>
class Table;
void debug_dump_table(const Table& table, const char* table_name, const char* phase = nullptr);
#endif

// Level to string conversion for output
static inline const char* debug_level_str(uint32_t level) {
    switch(level) {
        case DEBUG_LEVEL_ERROR: return "ERROR";
        case DEBUG_LEVEL_WARN:  return "WARN ";
        case DEBUG_LEVEL_INFO:  return "INFO ";
        case DEBUG_LEVEL_DEBUG: return "DEBUG";
        case DEBUG_LEVEL_TRACE: return "TRACE";
        default: return "UNKN ";
    }
}

// Table dumping support (only in app environment)
#ifndef ENCLAVE_BUILD
#ifdef __cplusplus

// Forward declarations
class Table;
class Entry;
class JoinTreeNode;
typedef std::shared_ptr<JoinTreeNode> JoinTreeNodePtr;

// Enum for metadata columns
enum MetadataColumn {
    META_INDEX,
    META_ORIG_IDX,
    META_LOCAL_MULT,
    META_FINAL_MULT,
    META_LOCAL_CUMSUM,
    META_LOCAL_INTERVAL,
    META_FOREIGN_SUM,
    META_FOREIGN_INTERVAL,
    META_LOCAL_WEIGHT,
    META_COPY_INDEX,
    META_ALIGN_KEY,
    META_DST_IDX,
    META_TABLE_IDX,
    META_JOIN_ATTR,
    META_FIELD_TYPE,
    META_EQ_TYPE,
    META_ENCRYPTED
};

// Debug session management
void debug_init_session(const char* session_name);
void debug_close_session();

// Table dumping functions
void debug_dump_table(const Table& table, const char* label, const char* step_name, uint32_t eid,
                      const std::vector<MetadataColumn>& columns = {}, bool include_attributes = true);
void debug_dump_entry(const Entry& entry, const char* label, uint32_t eid);
void debug_dump_selected_columns(const Table& table, const char* label, const char* step_name, 
                                 uint32_t eid, const std::vector<std::string>& columns);
void debug_dump_with_mask(const Table& table, const char* label, const char* step_name,
                          uint32_t eid, uint32_t column_mask);

// File output functions
void debug_to_file(const char* filename, const char* content);
void debug_append_to_file(const char* filename, const char* content);

// Encryption consistency assertions
uint8_t AssertConsistentEncryption(const Table& table);
void AssertTreeConsistentEncryption(JoinTreeNodePtr root);

// Convenience macro for table dumping
#if DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG && DEBUG_DUMP_TABLES
    #define DEBUG_TABLE(table, label, step) \
        debug_dump_table(table, label, step, global_eid)
#else
    #define DEBUG_TABLE(table, label, step) ((void)0)
#endif

#endif // __cplusplus
#endif // !ENCLAVE_BUILD

#endif // DEBUG_UTIL_H