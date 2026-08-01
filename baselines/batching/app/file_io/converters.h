#ifndef CONVERTERS_H
#define CONVERTERS_H

#include <vector>
#include <string>
#include <cstring>
#include "../data_structures/data_structures.h"
#include "../../common/enclave_types.h"

// Forward declarations
class Entry;
class Table;

// Note: Entry conversion functions moved to Entry class methods
// Use Entry::to_entry_t() and Entry::from_entry_t() instead

// Note: Table conversion functions moved to Table class methods
// Use Table::to_entry_t_vector() and Table constructor/methods instead

// NOTE: entry_t_vector_to_table has been removed - use Table::from_entry_t_vector() instead

/**
 * Helper function to convert std::string to char array
 * Ensures null termination and truncates if necessary
 */
void string_to_char_array(const std::string& str, char* arr, size_t max_len);

/**
 * Helper function to convert char array to std::string
 * Handles null termination properly
 */
std::string char_array_to_string(const char* arr, size_t max_len);


/**
 * Convert vector of int32_t to fixed array
 * Used for attributes conversion
 */
void int32_to_array(const std::vector<int32_t>& vec, int32_t* arr, size_t max_size);

/**
 * Convert fixed array to vector of int32_t
 * Used for attributes conversion
 */
std::vector<int32_t> array_to_int32(const int32_t* arr, size_t size);

#endif // CONVERTERS_H