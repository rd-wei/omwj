#ifndef CONSTANTS_H
#define CONSTANTS_H

// Maximum number of attributes per entry
#define MAX_ATTRIBUTES 64

// Window size for linear pass operations
#define WINDOW_SIZE 2

// AES encryption parameters
#define AES_KEY_SIZE 32
#define AES_BLOCK_SIZE 16

// Maximum table size for testing
#define MAX_TABLE_SIZE 10000

// Merge sort parameters
#define MERGE_SORT_K 8                    // Number of ways for k-way merge
#define MERGE_BUFFER_SIZE (MAX_BATCH_SIZE / MERGE_SORT_K)  // Buffer size per run

#endif // CONSTANTS_H