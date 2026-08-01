#ifndef TYPES_COMMON_H
#define TYPES_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>  // For INT32_MAX

// NULL value for metadata fields - using INT32_MAX for clear visibility in debug
// This helps distinguish uninitialized/unused fields from actual values
#define NULL_VALUE INT32_MAX

// Entry type constants (using int32_t for consistency with other metadata)
#define SORT_PADDING 0   // Padding for bitonic sort (always sorts to end)
#define SOURCE       1   // Source table entry
#define START        2   // Target start boundary
#define END          3   // Target end boundary
#define TARGET       4   // Target table entry
#define DIST_PADDING 5   // Padding for distribution (Phase 3)

// Equality type constants for boundary conditions
#define NONE  0   // No equality constraint
#define EQ    1   // Closed boundary (d or e)
#define NEQ   2   // Open boundary (< or >)

// Type aliases for clarity (both map to int32_t)
typedef int32_t entry_type_t;
typedef int32_t equality_type_t;

#endif // TYPES_COMMON_H