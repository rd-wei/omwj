#include "distribute_manager.h"
#include "../utils/counted_ecalls.h"
#include "../utils/counted_ocalls.h"
#include "debug_util.h"

DistributeManager* DistributeManager::current_instance = nullptr;

DistributeManager::DistributeManager(sgx_enclave_id_t enclave_id)
    : eid(enclave_id) {}

DistributeManager::~DistributeManager() {
    clear_current();
}

void DistributeManager::distribute(Table& table, size_t output_size) {
    if (output_size <= 1) return;

    DEBUG_INFO("DistributeManager::distribute: n=%zu", output_size);

    entries.clear();
    entries.reserve(table.size());
    for (size_t i = 0; i < table.size(); i++) {
        entries.push_back(table[i]);
    }

    set_as_current();
    sgx_status_t retval;
    counted_ecall_aks_distribute_large(eid, &retval, output_size);
    clear_current();

    table.clear();
    for (const auto& e : entries) {
        table.add_entry(e);
    }

    DEBUG_INFO("DistributeManager::distribute complete");
}

void DistributeManager::handle_read_flat(size_t offset, entry_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
        buf[i] = current_instance->entries[offset + i].to_entry_t();
    }
}

void DistributeManager::handle_write_flat(size_t offset, entry_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
        current_instance->entries[offset + i].from_entry_t(buf[i]);
    }
}

void DistributeManager::set_as_current() {
    current_instance = this;
}

void DistributeManager::clear_current() {
    if (current_instance == this) {
        current_instance = nullptr;
    }
}
