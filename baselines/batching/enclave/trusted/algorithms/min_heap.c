#include "min_heap.h"
#include <stdlib.h>
#include <string.h>

// Helper functions for heap operations
static inline size_t parent(size_t i) { return (i - 1) / 2; }
static inline size_t left_child(size_t i) { return 2 * i + 1; }
static inline size_t right_child(size_t i) { return 2 * i + 2; }

// Swap two heap elements
static void heap_swap(MinHeap* heap, size_t i, size_t j) {
    // Swap entries
    entry_t temp_entry = heap->entries[i];
    heap->entries[i] = heap->entries[j];
    heap->entries[j] = temp_entry;
    
    // Swap run indices
    size_t temp_idx = heap->run_indices[i];
    heap->run_indices[i] = heap->run_indices[j];
    heap->run_indices[j] = temp_idx;
}

// Heapify up (for insertion)
static void heapify_up(MinHeap* heap, size_t i) {
    while (i > 0) {
        size_t p = parent(i);
        // If parent is greater than child, swap
        if (heap->compare(&heap->entries[p], &heap->entries[i]) == 0 &&
            heap->compare(&heap->entries[i], &heap->entries[p]) == 1) {
            heap_swap(heap, i, p);
            i = p;
        } else {
            break;
        }
    }
}

// Heapify down (for deletion)
static void heapify_down(MinHeap* heap, size_t i) {
    while (1) {
        size_t smallest = i;
        size_t left = left_child(i);
        size_t right = right_child(i);
        
        // Find smallest among node and its children
        if (left < heap->size && 
            heap->compare(&heap->entries[left], &heap->entries[smallest]) == 1) {
            smallest = left;
        }
        
        if (right < heap->size && 
            heap->compare(&heap->entries[right], &heap->entries[smallest]) == 1) {
            smallest = right;
        }
        
        if (smallest != i) {
            heap_swap(heap, i, smallest);
            i = smallest;
        } else {
            break;
        }
    }
}

// Initialize heap
void minheap_init(MinHeap* heap, size_t capacity, comparator_func_t compare) {
    heap->entries = (entry_t*)malloc(capacity * sizeof(entry_t));
    heap->run_indices = (size_t*)malloc(capacity * sizeof(size_t));
    heap->size = 0;
    heap->capacity = capacity;
    heap->compare = compare;
}

// Push entry to heap
void heap_push(MinHeap* heap, entry_t* entry, size_t run_idx) {
    if (heap->size >= heap->capacity) {
        // Heap is full - this shouldn't happen in our use case
        return;
    }
    
    // Add to end of heap
    heap->entries[heap->size] = *entry;
    heap->run_indices[heap->size] = run_idx;
    
    // Heapify up
    heapify_up(heap, heap->size);
    heap->size++;
}

// Pop minimum from heap
int heap_pop(MinHeap* heap, entry_t* out_entry, size_t* out_run_idx) {
    if (heap->size == 0) {
        return 0;  // Heap is empty
    }
    
    // Return minimum (root)
    *out_entry = heap->entries[0];
    *out_run_idx = heap->run_indices[0];
    
    // Move last element to root
    heap->size--;
    if (heap->size > 0) {
        heap->entries[0] = heap->entries[heap->size];
        heap->run_indices[0] = heap->run_indices[heap->size];
        
        // Heapify down
        heapify_down(heap, 0);
    }
    
    return 1;  // Success
}

// Peek at minimum without removing
int heap_peek(MinHeap* heap, entry_t* out_entry, size_t* out_run_idx) {
    if (heap->size == 0) {
        return 0;  // Heap is empty
    }
    
    *out_entry = heap->entries[0];
    *out_run_idx = heap->run_indices[0];
    return 1;  // Success
}

// Check if heap is empty
int heap_is_empty(MinHeap* heap) {
    return heap->size == 0;
}

// Destroy heap
void heap_destroy(MinHeap* heap) {
    if (heap->entries) {
        free(heap->entries);
        heap->entries = NULL;
    }
    if (heap->run_indices) {
        free(heap->run_indices);
        heap->run_indices = NULL;
    }
    heap->size = 0;
    heap->capacity = 0;
}

// Heap sort implementation
void heap_sort(entry_t* array, size_t size, comparator_func_t compare) {
    if (size <= 1) return;
    
    // Build max-heap (we negate comparisons for max-heap behavior)
    // First, heapify the array
    for (int i = (int)(size / 2) - 1; i >= 0; i--) {
        size_t idx = (size_t)i;
        while (1) {
            size_t largest = idx;
            size_t left = 2 * idx + 1;
            size_t right = 2 * idx + 2;
            
            // For max-heap, we want larger elements at top
            // So we invert the comparison logic
            if (left < size && 
                compare(&array[largest], &array[left]) == 1) {
                largest = left;
            }
            
            if (right < size && 
                compare(&array[largest], &array[right]) == 1) {
                largest = right;
            }
            
            if (largest != idx) {
                // Swap
                entry_t temp = array[idx];
                array[idx] = array[largest];
                array[largest] = temp;
                idx = largest;
            } else {
                break;
            }
        }
    }
    
    // Extract elements from heap one by one
    for (size_t end = size - 1; end > 0; end--) {
        // Move current root to end
        entry_t temp = array[0];
        array[0] = array[end];
        array[end] = temp;
        
        // Heapify reduced heap
        size_t idx = 0;
        size_t heap_size = end;
        while (1) {
            size_t largest = idx;
            size_t left = 2 * idx + 1;
            size_t right = 2 * idx + 2;
            
            if (left < heap_size && 
                compare(&array[largest], &array[left]) == 1) {
                largest = left;
            }
            
            if (right < heap_size && 
                compare(&array[largest], &array[right]) == 1) {
                largest = right;
            }
            
            if (largest != idx) {
                // Swap
                entry_t temp2 = array[idx];
                array[idx] = array[largest];
                array[largest] = temp2;
                idx = largest;
            } else {
                break;
            }
        }
    }
}