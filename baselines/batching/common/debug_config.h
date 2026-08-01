#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Debug output modes
#define DEBUG_OUTPUT_CONSOLE 0
#define DEBUG_OUTPUT_FILE    1
#define DEBUG_OUTPUT_BOTH    2

// Debug table formats
#define DEBUG_FORMAT_CSV     0
#define DEBUG_FORMAT_JSON    1
#define DEBUG_FORMAT_BINARY  2

// Debug levels - higher level includes all lower levels
#define DEBUG_LEVEL_NONE     0
#define DEBUG_LEVEL_ERROR    1
#define DEBUG_LEVEL_WARN     2
#define DEBUG_LEVEL_INFO     3
#define DEBUG_LEVEL_DEBUG    4
#define DEBUG_LEVEL_TRACE    5

/**
 * Debug configuration structure
 * Controls all aspects of debug output and behavior
 */
typedef struct DebugConfig {
    // Global settings
    uint32_t level;              // Overall debug level (DEBUG_LEVEL_*)
    uint32_t output_mode;        // Output destination (DEBUG_OUTPUT_*)
    
    // Phase-specific debug flags
    struct {
        bool bottom_up;          // Enable bottom-up phase debugging
        bool top_down;           // Enable top-down phase debugging
        bool distribute;         // Enable distribute-expand debugging
        bool alignment;          // Enable align-concat debugging
        bool oblivious_sort;     // Enable sorting operation debugging
        bool comparators;        // Enable comparator debugging
        bool window_ops;         // Enable window operation debugging
        bool encryption;         // Enable encryption/decryption debugging
    } phases;
    
    // Table dumping configuration
    struct {
        bool enabled;            // Master switch for table dumps
        uint32_t format;         // Output format (DEBUG_FORMAT_*)
        uint32_t max_rows;       // Limit rows in dumps (0 = unlimited)
        bool show_encrypted;     // Show encrypted status in dumps
        
        // Stage-specific dump control
        struct {
            bool inputs;         // Dump input tables
            bool after_sort;     // Dump after sorting operations
            bool after_cumsum;   // Dump after cumsum operations
            bool after_interval; // Dump after interval computation
            bool after_pairwise; // Dump after pairwise operations
            bool after_truncate; // Dump after truncation
            bool after_expand;   // Dump after expansion
            bool outputs;        // Dump final outputs
        } stages;
        
        // Column selection presets
        struct {
            bool use_presets;    // Use preset column masks
            uint32_t bottom_up_mask;  // Columns for bottom-up dumps
            uint32_t top_down_mask;   // Columns for top-down dumps
            uint32_t distribute_mask; // Columns for distribute dumps
            uint32_t alignment_mask;  // Columns for alignment dumps
        } columns;
    } tables;
    
    // Performance monitoring
    struct {
        bool enabled;            // Enable performance tracking
        bool per_phase;          // Track per-phase timing
        bool per_operation;      // Track individual operations
        bool memory_usage;       // Track memory consumption
        bool enclave_transitions; // Track ecall/ocall overhead
    } perf;
    
    // Session configuration
    struct {
        bool auto_session;       // Automatically start/end sessions
        bool timestamp_files;    // Add timestamps to output filenames
        bool create_subdirs;     // Create subdirectories for each session
        char session_prefix[64]; // Prefix for session names
        char output_dir[256];    // Base directory for debug output
    } session;
    
} DebugConfig;

// Default configuration initializer
#define DEBUG_CONFIG_DEFAULT { \
    .level = DEBUG_LEVEL_NONE, \
    .output_mode = DEBUG_OUTPUT_FILE, \
    .phases = { \
        .bottom_up = false, \
        .top_down = false, \
        .distribute = false, \
        .alignment = false, \
        .oblivious_sort = false, \
        .comparators = false, \
        .window_ops = false, \
        .encryption = false \
    }, \
    .tables = { \
        .enabled = false, \
        .format = DEBUG_FORMAT_CSV, \
        .max_rows = 0, \
        .show_encrypted = false, \
        .stages = { \
            .inputs = false, \
            .after_sort = false, \
            .after_cumsum = false, \
            .after_interval = false, \
            .after_pairwise = false, \
            .after_truncate = false, \
            .after_expand = false, \
            .outputs = false \
        }, \
        .columns = { \
            .use_presets = true, \
            .bottom_up_mask = 0, \
            .top_down_mask = 0, \
            .distribute_mask = 0, \
            .alignment_mask = 0 \
        } \
    }, \
    .perf = { \
        .enabled = false, \
        .per_phase = false, \
        .per_operation = false, \
        .memory_usage = false, \
        .enclave_transitions = false \
    }, \
    .session = { \
        .auto_session = true, \
        .timestamp_files = true, \
        .create_subdirs = true, \
        .session_prefix = "debug", \
        .output_dir = "./debug" \
    } \
}

// Development configuration with common debug settings
#define DEBUG_CONFIG_DEVELOPMENT { \
    .level = DEBUG_LEVEL_INFO, \
    .output_mode = DEBUG_OUTPUT_FILE, \
    .phases = { \
        .bottom_up = true, \
        .top_down = true, \
        .distribute = true, \
        .alignment = true, \
        .oblivious_sort = false, \
        .comparators = false, \
        .window_ops = false, \
        .encryption = false \
    }, \
    .tables = { \
        .enabled = true, \
        .format = DEBUG_FORMAT_CSV, \
        .max_rows = 1000, \
        .show_encrypted = false, \
        .stages = { \
            .inputs = true, \
            .after_sort = true, \
            .after_cumsum = true, \
            .after_interval = true, \
            .after_pairwise = false, \
            .after_truncate = true, \
            .after_expand = false, \
            .outputs = true \
        }, \
        .columns = { \
            .use_presets = true, \
            .bottom_up_mask = 0, \
            .top_down_mask = 0, \
            .distribute_mask = 0, \
            .alignment_mask = 0 \
        } \
    }, \
    .perf = { \
        .enabled = true, \
        .per_phase = true, \
        .per_operation = false, \
        .memory_usage = false, \
        .enclave_transitions = false \
    }, \
    .session = { \
        .auto_session = true, \
        .timestamp_files = true, \
        .create_subdirs = true, \
        .session_prefix = "debug", \
        .output_dir = "./debug" \
    } \
}

#ifdef __cplusplus
}
#endif

#endif // DEBUG_CONFIG_H