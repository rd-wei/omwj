/**
 * Secure Table Encryption Utility
 * 
 * This utility encrypts plaintext CSV tables using a secure key stored
 * inside the SGX enclave. The encryption key never leaves the enclave,
 * ensuring data security.
 * 
 * Usage: ./encrypt_tables <input_dir> <output_dir>
 * 
 * Example:
 *   ./encrypt_tables plaintext/data_0_001/ encrypted/data_0_001/
 * 
 * Note: The encryption key is securely stored inside the SGX enclave
 *       and cannot be accessed or modified by untrusted code.
 */

#include <iostream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "sgx_urts.h"
#include "file_io/table_io.h"
#include "Enclave_u.h"

// Global enclave ID
sgx_enclave_id_t global_eid = 0;

// Initialize enclave
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return -1;
    }
    
    std::cout << "Enclave initialized successfully" << std::endl;
    return 0;
}

// Destroy enclave
void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
        std::cout << "Enclave destroyed" << std::endl;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <input_dir> <output_dir>" << std::endl;
    std::cout << std::endl;
    std::cout << "Arguments:" << std::endl;
    std::cout << "  input_dir   - Directory containing plaintext CSV files" << std::endl;
    std::cout << "  output_dir  - Directory to save encrypted CSV files" << std::endl;
    std::cout << std::endl;
    std::cout << "Security Note:" << std::endl;
    std::cout << "  The encryption key is securely stored inside the SGX enclave." << std::endl;
    std::cout << "  It cannot be accessed or modified by untrusted code." << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << program_name << " plaintext/data_0_001/ encrypted/data_0_001/" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string input_dir = argv[1];
    std::string output_dir = argv[2];
    // Key is now securely stored in the enclave - no need to pass it as parameter
    
    // Verify input directory exists
    struct stat dir_stat;
    if (stat(input_dir.c_str(), &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
        std::cerr << "Error: Input directory does not exist: " << input_dir << std::endl;
        return 1;
    }
    
    // Create output directory if it doesn't exist
    if (stat(output_dir.c_str(), &dir_stat) != 0) {
        if (mkdir(output_dir.c_str(), 0755) == 0) {
            std::cout << "Created output directory: " << output_dir << std::endl;
        } else {
            std::cerr << "Error: Could not create output directory: " << output_dir << std::endl;
            return 1;
        }
    }
    
    // Initialize enclave
    if (initialize_enclave() != 0) {
        std::cerr << "Failed to initialize enclave" << std::endl;
        return 1;
    }
    
    // Process all CSV files in input directory
    size_t files_processed = 0;
    size_t files_failed = 0;
    
    std::cout << "\nEncrypting tables using secure enclave key" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    DIR* dir = opendir(input_dir.c_str());
    if (dir == nullptr) {
        std::cerr << "Error: Cannot open input directory" << std::endl;
        destroy_enclave();
        return 1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // Skip . and ..
        if (filename == "." || filename == "..") continue;
        
        // Only process CSV files
        if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".csv") {
            std::string input_path = input_dir + "/" + filename;
            std::string output_path = output_dir + "/" + filename;
            
            // Check if it's a regular file
            struct stat file_stat;
            if (stat(input_path.c_str(), &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
                continue;
            }
            
            try {
                std::cout << "Processing: " << filename << " ... ";
                
                // Load plaintext CSV
                Table table = TableIO::load_csv(input_path);
                std::cout << table.size() << " rows ... ";
                
                // Save as encrypted CSV with nonce using secure enclave key
                TableIO::save_encrypted_csv(table, output_path, global_eid);
                
                std::cout << "✓ Done" << std::endl;
                files_processed++;
                
            } catch (const std::exception& e) {
                std::cout << "✗ Failed: " << e.what() << std::endl;
                files_failed++;
            }
        }
    }
    
    closedir(dir);
    
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Summary:" << std::endl;
    std::cout << "  Files processed: " << files_processed << std::endl;
    std::cout << "  Files failed:    " << files_failed << std::endl;
    
    // Verify by loading one encrypted file
    if (files_processed > 0) {
        std::cout << "\nVerifying encryption..." << std::endl;
        
        // Find first CSV file in output directory
        DIR* verify_dir = opendir(output_dir.c_str());
        if (verify_dir != nullptr) {
            struct dirent* verify_entry;
            while ((verify_entry = readdir(verify_dir)) != nullptr) {
                std::string filename = verify_entry->d_name;
                if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".csv") {
                    std::string verify_path = output_dir + "/" + filename;
                    
                    try {
                        // load_csv auto-detects encryption by checking for nonce column
                        Table encrypted = TableIO::load_csv(verify_path);
                        auto status = encrypted.get_encryption_status();
                        if (status == Table::ENCRYPTED) {
                            std::cout << "✓ Verification successful: " << filename 
                                     << " detected as encrypted (nonce column found)" << std::endl;
                        } else {
                            std::cout << "✗ Verification failed: encryption not detected" << std::endl;
                        }
                    } catch (const std::exception& e) {
                        std::cout << "✗ Verification failed: " << e.what() << std::endl;
                    }
                    break;
                }
            }
            closedir(verify_dir);
        }
    }
    
    // Cleanup
    destroy_enclave();
    
    return (files_failed == 0) ? 0 : 1;
}