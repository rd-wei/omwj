#include "table_io.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <climits>  // For INT32_MAX, INT32_MIN
#include "converters.h"
#include "io_entry.h"  // For IO_Entry
#include "Enclave_u.h"
#include "types_common.h"  // For NULL_VALUE and type constants
#include "debug_util.h"

// For directory operations
#include <dirent.h>
#include <sys/stat.h>

Table TableIO::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + filepath);
    }
    
    std::string line;
    std::vector<std::string> headers;
    int nonce_column_index = -1;  // -1 means no nonce column
    
    // Read first line to get headers
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty: " + filepath);
    }
    
    // Parse headers from first line
    headers = parse_csv_line(line);
    
    // Check if any column is "nonce"
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == "nonce") {
            nonce_column_index = static_cast<int>(i);
            break;
        }
    }
    
    // Build schema (excluding nonce column if present)
    std::vector<std::string> schema_columns;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (static_cast<int>(i) != nonce_column_index) {
            schema_columns.push_back(headers[i]);
        }
    }
    
    // Now create table with proper schema
    Table table(extract_table_name(filepath), schema_columns);
    
    // Set num_columns (excluding nonce column if present)
    size_t data_columns = (nonce_column_index >= 0) ? headers.size() - 1 : headers.size();
    table.set_num_columns(data_columns);
    
    // Process data lines
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        auto values = parse_csv_line(line);
        
        {
            // Data line - create IO_Entry for dynamic size handling
            IO_Entry io_entry;
            
            // Build column names (excluding nonce if present)
            for (size_t i = 0; i < headers.size(); ++i) {
                if (static_cast<int>(i) != nonce_column_index) {
                    io_entry.column_names.push_back(headers[i]);
                }
            }
            
            // Parse values
            uint64_t nonce_value = 0;
            for (size_t i = 0; i < values.size() && i < headers.size(); ++i) {
                if (static_cast<int>(i) == nonce_column_index) {
                    // This is the nonce column - parse as uint64_t
                    nonce_value = std::stoull(values[i]);
                } else {
                    // Regular data column
                    int32_t val = parse_value(values[i]);
                    io_entry.attributes.push_back(val);
                    
                    // Set join_attr to the first data column value
                    if (io_entry.attributes.size() == 1) {
                        io_entry.join_attr = val;
                    }
                }
            }
            
            // Set encryption fields
            io_entry.is_encrypted = (nonce_column_index >= 0);  // Encrypted if nonce column exists
            io_entry.nonce = nonce_value;
            
            // Convert IO_Entry to regular Entry with fixed MAX_ATTRIBUTES
            Entry entry = io_entry.to_entry();
            
            // Metadata is already initialized to 0 by IO_Entry::to_entry()
            // It will be set to NULL_VALUE by enclave later
            
            table.add_entry(entry);
        }
    }
    
    file.close();
    return table;
}

void TableIO::save_csv(const Table& table, const std::string& filepath) {
    // Check that all entries are NOT encrypted
    for (size_t i = 0; i < table.size(); i++) {
        if (table.get_entry(i).is_encrypted) {
            throw std::runtime_error("save_csv called with encrypted data at entry " + std::to_string(i) + 
                                   ". Use save_encrypted_csv for encrypted data.");
        }
    }
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create CSV file: " + filepath);
    }
    
    // Write headers (prefer Table schema, fallback to first entry)
    if (table.size() > 0) {
        std::vector<std::string> headers = table.get_schema();
        
        // Table should always have schema set
        if (headers.empty()) {
            throw std::runtime_error("Table has no schema set");
        }
        
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) file << ",";
            file << headers[i];
        }
        file << "\n";
        
        // Write data
        for (size_t row = 0; row < table.size(); ++row) {
            const auto& entry = table.get_entry(row);
            // Write only non-empty columns
            bool first = true;
            for (size_t col = 0; col < headers.size(); ++col) {
                if (!first) file << ",";
                file << static_cast<int64_t>(entry.attributes[col]);
                first = false;
            }
            file << "\n";
        }
    }
    
    file.close();
}

void TableIO::save_encrypted_csv(const Table& table, 
                                 const std::string& filepath,
                                 sgx_enclave_id_t eid) {
    // Assert consistent encryption status - all entries should be encrypted
    uint8_t encryption_status = AssertConsistentEncryption(table);
    
    // Work with a copy of the table to allow encryption if needed
    Table table_copy = table;
    
    // If table is not encrypted, encrypt all entries
    if (encryption_status == 0) {
        for (size_t i = 0; i < table_copy.size(); i++) {
            Entry& entry = table_copy.get_entry(i);
            crypto_status_t ret = CryptoUtils::encrypt_entry(entry, eid);
            if (ret != CRYPTO_SUCCESS) {
                throw std::runtime_error("Encryption failed at entry " + std::to_string(i));
            }
        }
    }
    
    // Verify all entries are now encrypted
    for (size_t i = 0; i < table_copy.size(); i++) {
        if (!table_copy.get_entry(i).is_encrypted) {
            throw std::runtime_error("Internal error: Entry " + std::to_string(i) + 
                                   " is still not encrypted after encryption attempt.");
        }
    }
    
    // Now convert to entry_t vector for writing
    std::vector<entry_t> entries = table_copy.to_entry_t_vector();
    
    // Now write as CSV with encrypted values
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create encrypted CSV file: " + filepath);
    }
    
    // Write headers from Table schema (required)
    if (entries.size() > 0) {
        std::vector<std::string> headers = table_copy.get_schema();
        
        // Schema must be set for saving
        if (headers.empty()) {
            throw std::runtime_error("Cannot save table without schema");
        }
        
        // Write column headers
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) file << ",";
            file << headers[i];
        }
        // Add nonce column header
        file << ",nonce\n";
        
        // Write encrypted data as integers with nonce
        for (const auto& entry : entries) {
            // Write attributes based on schema size
            for (size_t i = 0; i < headers.size(); ++i) {
                if (i > 0) file << ",";
                // Write the encrypted integer value directly
                // Cast to int32_t since that's what the encrypted values are
                file << static_cast<int32_t>(entry.attributes[i]);
            }
            // Add the nonce value
            file << "," << entry.nonce << "\n";
        }
    }
    
    file.close();
}

// load_encrypted_csv has been deprecated
// Use load_csv() instead - it auto-detects encryption by checking for nonce column

std::unordered_map<std::string, Table> 
TableIO::load_csv_directory(const std::string& dir_path) {
    std::unordered_map<std::string, Table> tables;
    
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        throw std::runtime_error("Cannot open directory: " + dir_path);
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // Skip . and ..
        if (filename == "." || filename == "..") continue;
        
        // Check if it's a regular file and CSV
        std::string full_path = dir_path + "/" + filename;
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            if (is_csv_file(filename)) {
                std::string table_name = extract_table_name(filename);
                Table loaded_table = load_csv(full_path);
                std::cout << "Loaded table: " << table_name 
                         << " (" << loaded_table.size() << " rows)" << std::endl;
                tables.emplace(table_name, std::move(loaded_table));
            }
        }
    }
    
    closedir(dir);
    return tables;
}

std::unordered_map<std::string, Table> 
TableIO::load_tables_from_directory(const std::string& dir_path) {
    // This function is now just an alias for load_csv_directory
    // Auto-detects encryption by checking for nonce column in each file
    return load_csv_directory(dir_path);
}

bool TableIO::file_exists(const std::string& filepath) {
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

std::string TableIO::extract_table_name(const std::string& filepath) {
    // Extract filename from path
    size_t last_slash = filepath.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos) 
                          ? filepath.substr(last_slash + 1)
                          : filepath;
    
    // Remove extension
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string::npos) {
        filename = filename.substr(0, last_dot);
    }
    
    // Remove any numeric suffixes (e.g., "supplier1" -> "supplier1")
    // Keep them for now as they might be intentional
    return filename;
}

std::vector<std::string> TableIO::parse_csv_line(const std::string& line) {
    std::vector<std::string> values;
    std::stringstream ss(line);
    std::string value;
    
    while (std::getline(ss, value, ',')) {
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        values.push_back(value);
    }
    
    return values;
}

int32_t TableIO::parse_value(const std::string& str) {
    try {
        // Parse as integer (our data is all integers)
        long long val = std::stoll(str);
        // Clamp to int32_t range
        if (val > INT32_MAX) return INT32_MAX;
        if (val < INT32_MIN) return INT32_MIN;
        return static_cast<int32_t>(val);
    } catch (const std::exception& e) {
        std::cerr << "Warning: Cannot parse value '" << str << "', using 0" << std::endl;
        return 0;
    }
}

bool TableIO::is_csv_file(const std::string& filename) {
    return filename.size() >= 4 && 
           filename.substr(filename.size() - 4) == ".csv";
}