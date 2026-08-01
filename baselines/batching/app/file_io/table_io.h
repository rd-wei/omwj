#ifndef TABLE_IO_H
#define TABLE_IO_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include "../data_structures/data_structures.h"
#include "../crypto/crypto_utils.h"
#include "sgx_urts.h"

/**
 * TableIO Class
 * 
 * Handles loading and saving tables in various formats:
 * - CSV: Plain text format for initial data (TPC-H tables)
 * - Encrypted binary: Pre-encrypted format for production use
 * 
 * CSV Format:
 * - First row contains column headers
 * - Subsequent rows contain data values
 * - All values are numeric (as seen in the TPC-H data)
 * 
 * Encrypted Binary Format:
 * - Header with metadata (magic, version, counts, table name)
 * - Array of encrypted entry_t structures
 * - Can be loaded directly without re-encryption
 */

class TableIO {
public:
    // CSV Operations
    /**
     * Load a CSV file into a Table object
     * @param filepath Path to the CSV file
     * @return Loaded table with data
     */
    static Table load_csv(const std::string& filepath);
    
    /**
     * Save a Table to CSV format (PLAINTEXT ONLY)
     * IMPORTANT: This enforces that ALL entries must have is_encrypted=false.
     *            Use this for debugging or when you explicitly need plaintext output.
     * @param table Table to save
     * @param filepath Output CSV file path
     * @throws runtime_error if any entry has is_encrypted=true
     */
    static void save_csv(const Table& table, const std::string& filepath);
    
    // Encrypted CSV Operations
    
    /**
     * Save a table as encrypted CSV with nonce column
     * IMPORTANT: Ensures ALL entries are encrypted before saving.
     *            Any unencrypted entries will be automatically encrypted.
     *            Output CSV contains ciphertext values and a nonce column.
     * The encryption key is stored securely inside the enclave
     * @param table Table to encrypt and save
     * @param filepath Output CSV file path
     * @param eid Enclave ID for encryption
     * @throws runtime_error if encryption fails
     */
    static void save_encrypted_csv(const Table& table, 
                                   const std::string& filepath,
                                   sgx_enclave_id_t eid);
    
    // load_encrypted_csv has been deprecated
    // Use load_csv() instead - it auto-detects encryption by checking for nonce column
    
    // Batch Operations
    /**
     * Load all CSV files from a directory
     * @param dir_path Directory containing CSV files
     * @return Map of table name to Table object
     */
    static std::unordered_map<std::string, Table> 
        load_csv_directory(const std::string& dir_path);
    
    /**
     * Load all tables from a directory (plain CSV or encrypted CSV)
     * Auto-detects encryption by checking for nonce column in each file.
     * @param dir_path Directory path
     * @return Map of table name to Table object
     */
    static std::unordered_map<std::string, Table> 
        load_tables_from_directory(const std::string& dir_path);
    
    // Utility Functions
    /**
     * Check if a file exists
     * @param filepath File path to check
     * @return true if file exists
     */
    static bool file_exists(const std::string& filepath);
    
    /**
     * Get table name from file path (removes path and extension)
     * @param filepath File path
     * @return Table name
     */
    static std::string extract_table_name(const std::string& filepath);
    
private:
    // Helper functions
    static std::vector<std::string> parse_csv_line(const std::string& line);
    static int32_t parse_value(const std::string& str);
    static bool is_csv_file(const std::string& filename);
};

#endif // TABLE_IO_H