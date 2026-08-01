#ifndef DISTRIBUTE_MANAGER_H
#define DISTRIBUTE_MANAGER_H

#include "../data_structures/table.h"
#include "../data_structures/entry.h"
#include "sgx_urts.h"
#include "../../common/enclave_types.h"
#include <vector>

/**
 * DistributeManager - Manages AKS oblivious distribute for arbitrary-sized arrays.
 *
 * Design mirrors ShuffleManager: holds entries as std::vector<Entry>, registers
 * itself as current_instance before the ecall, and exposes static handle_* methods
 * that the global ocall functions in counted_ocalls.cpp delegate to.
 */
class DistributeManager {
private:
    sgx_enclave_id_t eid;
    std::vector<Entry> entries;

    static DistributeManager* current_instance;

public:
    DistributeManager(sgx_enclave_id_t enclave_id);
    ~DistributeManager();

    void distribute(Table& table, size_t output_size);

    // Ocall handlers — called from counted_ocalls.cpp
    static void handle_read_flat(size_t offset, entry_t* buf, size_t count);
    static void handle_write_flat(size_t offset, entry_t* buf, size_t count);

private:
    void set_as_current();
    void clear_current();
};

#endif // DISTRIBUTE_MANAGER_H
