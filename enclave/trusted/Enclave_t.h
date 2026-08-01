#ifndef ENCLAVE_T_H__
#define ENCLAVE_T_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include "sgx_edger8r.h" /* for sgx_ocall etc. */

#include "common/entry_t.h"
#include "common/ecall_types.h"

#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

sgx_status_t ecall_run_join(const entry_t* table_data, size_t total_rows, const table_desc_t* table_descs, size_t num_tables, const join_node_desc_t* join_tree, size_t num_nodes, const output_col_t* out_cols, size_t num_out_cols, int32_t* result_count);
sgx_status_t ecall_decrypt_rows(entry_t* rows, size_t count);

sgx_status_t SGX_CDECL ocall_stream_result(const entry_t* rows, size_t count);
sgx_status_t SGX_CDECL ocall_get_time_ns(uint64_t* ns);
sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf);
sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter);
sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
