#ifndef ENCLAVE_U_H__
#define ENCLAVE_U_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include <string.h>
#include "sgx_edger8r.h" /* for sgx_status_t etc. */

#include "common/entry_t.h"
#include "common/ecall_types.h"

#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OCALL_STREAM_RESULT_DEFINED__
#define OCALL_STREAM_RESULT_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_stream_result, (const entry_t* rows, size_t count));
#endif
#ifndef OCALL_GET_TIME_NS_DEFINED__
#define OCALL_GET_TIME_NS_DEFINED__
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_get_time_ns, (uint64_t* ns));
#endif
#ifndef SGX_OC_CPUIDEX_DEFINED__
#define SGX_OC_CPUIDEX_DEFINED__
void SGX_UBRIDGE(SGX_CDECL, sgx_oc_cpuidex, (int cpuinfo[4], int leaf, int subleaf));
#endif
#ifndef SGX_THREAD_WAIT_UNTRUSTED_EVENT_OCALL_DEFINED__
#define SGX_THREAD_WAIT_UNTRUSTED_EVENT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_wait_untrusted_event_ocall, (const void* self));
#endif
#ifndef SGX_THREAD_SET_UNTRUSTED_EVENT_OCALL_DEFINED__
#define SGX_THREAD_SET_UNTRUSTED_EVENT_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_set_untrusted_event_ocall, (const void* waiter));
#endif
#ifndef SGX_THREAD_SETWAIT_UNTRUSTED_EVENTS_OCALL_DEFINED__
#define SGX_THREAD_SETWAIT_UNTRUSTED_EVENTS_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_setwait_untrusted_events_ocall, (const void* waiter, const void* self));
#endif
#ifndef SGX_THREAD_SET_MULTIPLE_UNTRUSTED_EVENTS_OCALL_DEFINED__
#define SGX_THREAD_SET_MULTIPLE_UNTRUSTED_EVENTS_OCALL_DEFINED__
int SGX_UBRIDGE(SGX_CDECL, sgx_thread_set_multiple_untrusted_events_ocall, (const void** waiters, size_t total));
#endif

sgx_status_t ecall_run_join(sgx_enclave_id_t eid, sgx_status_t* retval, const entry_t* table_data, size_t total_rows, const table_desc_t* table_descs, size_t num_tables, const join_node_desc_t* join_tree, size_t num_nodes, const output_col_t* out_cols, size_t num_out_cols, int32_t* result_count);
sgx_status_t ecall_decrypt_rows(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* rows, size_t count);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
