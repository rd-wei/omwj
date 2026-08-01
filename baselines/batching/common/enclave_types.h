#ifndef ENCLAVE_TYPES_H
#define ENCLAVE_TYPES_H

#include "../common/constants.h"
#include "../common/types_common.h"
#include <stdint.h>
#include <limits.h>

// Define infinity values and safe range for join attributes
// We limit the valid range to approximately half of int32_t to prevent overflow
// when adding deviations to join attributes
#define JOIN_ATTR_MIN     (-1073741820)         // Minimum valid join attribute
#define JOIN_ATTR_MAX     (1073741820)          // Maximum valid join attribute  
#define JOIN_ATTR_NEG_INF (-1073741821)         // Represents -∞ (just below valid range)
#define JOIN_ATTR_POS_INF (1073741821)          // Represents +∞ (just above valid range)

// Metadata field masks for selective initialization to NULL_VALUE
// These allow fine-grained control over which fields to set to NULL_VALUE (INT32_MAX)
// Group masks - for convenience
#define METADATA_PERSISTENT  0x00F  // original_index, local_mult, final_mult, foreign_sum
#define METADATA_TEMPORARY   0x0F0  // local_cumsum, local_interval, foreign_interval, local_weight
#define METADATA_DISTRIBUTE  0x600  // dst_idx, index
#define METADATA_ALIGNMENT   0x1800 // copy_index, alignment_key  
#define METADATA_TYPE        0x2000 // field_type, equality_type

// Individual field masks - for precise control
#define METADATA_ORIGINAL_INDEX   0x001
#define METADATA_LOCAL_MULT       0x002
#define METADATA_FINAL_MULT       0x004
#define METADATA_FOREIGN_SUM      0x008
#define METADATA_LOCAL_CUMSUM     0x010
#define METADATA_LOCAL_INTERVAL   0x020
#define METADATA_FOREIGN_INTERVAL 0x040
#define METADATA_LOCAL_WEIGHT     0x080
#define METADATA_DST_IDX          0x100
#define METADATA_INDEX            0x200
#define METADATA_COPY_INDEX       0x400
#define METADATA_ALIGNMENT_KEY    0x800
#define METADATA_FIELD_TYPE       0x1000

// Useful combinations
#define METADATA_ALL              0x1FFF  // All metadata fields (updated after removing foreign_cumsum)
#define METADATA_BOTTOM_UP        (METADATA_LOCAL_CUMSUM | METADATA_LOCAL_INTERVAL)
#define METADATA_TOP_DOWN         (METADATA_FOREIGN_INTERVAL | METADATA_LOCAL_WEIGHT)

// This ensures that for any valid join_attr in [JOIN_ATTR_MIN, JOIN_ATTR_MAX]
// and any deviation in the same range, join_attr + deviation will never overflow
// The infinity values are just outside the valid range, making them easy to detect
// Since we never perform inf + inf operations, this approach is safe

// Entry structure for enclave processing
typedef struct {
    // Entry metadata (using int32_t for consistency across encrypt/decrypt)
    int32_t field_type;           // SORT_PADDING, SOURCE, START, END, TARGET, DIST_PADDING
    int32_t equality_type;        // EQ, NEQ, NONE
    uint8_t is_encrypted;         // Whether data is encrypted (0 or 1)
    
    // Encryption nonce for AES-CTR mode
    uint64_t nonce;               // Unique nonce for each encryption
    
    // Join attribute (using int32_t for signed arithmetic)
    int32_t join_attr;
    
    // Persistent metadata (persists across phases)
    int32_t original_index;      // Original position in table
    int32_t local_mult;          // Local multiplicity
    int32_t final_mult;          // Final multiplicity
    int32_t foreign_sum;         // Foreign sum for alignment
    
    // Temporary metadata (reused between phases)
    int32_t local_cumsum;        // Cumulative sum (bottom-up)
    int32_t local_interval;      // Interval value (bottom-up)
    int32_t foreign_interval;    // Foreign interval (top-down)
    int32_t local_weight;        // Local weight counter (top-down)
    
    // Expansion metadata
    int32_t copy_index;          // Which copy of the tuple (0 to final_mult-1)
    int32_t alignment_key;       // Key for alignment phase
    
    // Distribution fields
    int32_t dst_idx;             // Destination index for distribution
    int32_t index;               // Current position (0 to output_size-1)
    
    // Data attributes (all integers for our use case)
    int32_t attributes[MAX_ATTRIBUTES];
} entry_t;

#endif // ENCLAVE_TYPES_H