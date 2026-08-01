#ifndef MIN_HEAP_H
#define MIN_HEAP_H

#include "../enclave_types.h"
#include "../../../common/comparator_convention.h"
#include <stddef.h>

/**
 * Min-Heap for K-Way Merge Sort
 * 
 * Maintains a min-heap of entries along with their run indices.
 * Used for efficiently finding the minimum element among k runs.
 */
typedef struct {
    entry_t* entries;      // Heap of entries
    size_t* run_indices;   // Which run each entry came from
    size_t size;          // Current number of elements
    size_t capacity;      // Maximum capacity
    comparator_func_t compare;  // Comparison function
} MinHeap;

// Initialize heap with given capacity
void minheap_init(MinHeap* heap, size_t capacity, comparator_func_t compare);

// Add an entry to the heap
void heap_push(MinHeap* heap, entry_t* entry, size_t run_idx);

// Remove and return the minimum entry
// Returns 1 on success, 0 if heap is empty
int heap_pop(MinHeap* heap, entry_t* out_entry, size_t* out_run_idx);

// Peek at minimum without removing
// Returns 1 on success, 0 if heap is empty
int heap_peek(MinHeap* heap, entry_t* out_entry, size_t* out_run_idx);

// Check if heap is empty
int heap_is_empty(MinHeap* heap);

// Clean up heap resources
void heap_destroy(MinHeap* heap);

// Heap sort an array in-place
void heap_sort(entry_t* array, size_t size, comparator_func_t compare);

#endif // MIN_HEAP_H