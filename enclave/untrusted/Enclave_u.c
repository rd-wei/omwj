#include "Enclave_u.h"
#include <errno.h>

typedef struct ms_ecall_run_join_t {
	sgx_status_t ms_retval;
	const entry_t* ms_table_data;
	size_t ms_total_rows;
	const table_desc_t* ms_table_descs;
	size_t ms_num_tables;
	const join_node_desc_t* ms_join_tree;
	size_t ms_num_nodes;
	const output_col_t* ms_out_cols;
	size_t ms_num_out_cols;
	int32_t* ms_result_count;
} ms_ecall_run_join_t;

typedef struct ms_ecall_decrypt_rows_t {
	sgx_status_t ms_retval;
	entry_t* ms_rows;
	size_t ms_count;
} ms_ecall_decrypt_rows_t;

typedef struct ms_ocall_stream_result_t {
	const entry_t* ms_rows;
	size_t ms_count;
} ms_ocall_stream_result_t;

typedef struct ms_ocall_get_time_ns_t {
	uint64_t* ms_ns;
} ms_ocall_get_time_ns_t;

typedef struct ms_sgx_oc_cpuidex_t {
	int* ms_cpuinfo;
	int ms_leaf;
	int ms_subleaf;
} ms_sgx_oc_cpuidex_t;

typedef struct ms_sgx_thread_wait_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_self;
} ms_sgx_thread_wait_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_set_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_waiter;
} ms_sgx_thread_set_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_setwait_untrusted_events_ocall_t {
	int ms_retval;
	const void* ms_waiter;
	const void* ms_self;
} ms_sgx_thread_setwait_untrusted_events_ocall_t;

typedef struct ms_sgx_thread_set_multiple_untrusted_events_ocall_t {
	int ms_retval;
	const void** ms_waiters;
	size_t ms_total;
} ms_sgx_thread_set_multiple_untrusted_events_ocall_t;

static sgx_status_t SGX_CDECL Enclave_ocall_stream_result(void* pms)
{
	ms_ocall_stream_result_t* ms = SGX_CAST(ms_ocall_stream_result_t*, pms);
	ocall_stream_result(ms->ms_rows, ms->ms_count);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_ocall_get_time_ns(void* pms)
{
	ms_ocall_get_time_ns_t* ms = SGX_CAST(ms_ocall_get_time_ns_t*, pms);
	ocall_get_time_ns(ms->ms_ns);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_oc_cpuidex(void* pms)
{
	ms_sgx_oc_cpuidex_t* ms = SGX_CAST(ms_sgx_oc_cpuidex_t*, pms);
	sgx_oc_cpuidex(ms->ms_cpuinfo, ms->ms_leaf, ms->ms_subleaf);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_wait_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_wait_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_wait_untrusted_event_ocall(ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_set_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_untrusted_event_ocall(ms->ms_waiter);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_setwait_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_setwait_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_setwait_untrusted_events_ocall(ms->ms_waiter, ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_multiple_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_multiple_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_multiple_untrusted_events_ocall(ms->ms_waiters, ms->ms_total);

	return SGX_SUCCESS;
}

static const struct {
	size_t nr_ocall;
	void * table[7];
} ocall_table_Enclave = {
	7,
	{
		(void*)Enclave_ocall_stream_result,
		(void*)Enclave_ocall_get_time_ns,
		(void*)Enclave_sgx_oc_cpuidex,
		(void*)Enclave_sgx_thread_wait_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_set_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_setwait_untrusted_events_ocall,
		(void*)Enclave_sgx_thread_set_multiple_untrusted_events_ocall,
	}
};
sgx_status_t ecall_run_join(sgx_enclave_id_t eid, sgx_status_t* retval, const entry_t* table_data, size_t total_rows, const table_desc_t* table_descs, size_t num_tables, const join_node_desc_t* join_tree, size_t num_nodes, const output_col_t* out_cols, size_t num_out_cols, int32_t* result_count)
{
	sgx_status_t status;
	ms_ecall_run_join_t ms;
	ms.ms_table_data = table_data;
	ms.ms_total_rows = total_rows;
	ms.ms_table_descs = table_descs;
	ms.ms_num_tables = num_tables;
	ms.ms_join_tree = join_tree;
	ms.ms_num_nodes = num_nodes;
	ms.ms_out_cols = out_cols;
	ms.ms_num_out_cols = num_out_cols;
	ms.ms_result_count = result_count;
	status = sgx_ecall(eid, 0, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t ecall_decrypt_rows(sgx_enclave_id_t eid, sgx_status_t* retval, entry_t* rows, size_t count)
{
	sgx_status_t status;
	ms_ecall_decrypt_rows_t ms;
	ms.ms_rows = rows;
	ms.ms_count = count;
	status = sgx_ecall(eid, 1, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

