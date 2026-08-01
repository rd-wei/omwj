#include "Enclave_t.h"

#include "sgx_trts.h" /* for sgx_ocalloc, sgx_is_outside_enclave */
#include "sgx_lfence.h" /* for sgx_lfence */

#include <errno.h>
#include <mbusafecrt.h> /* for memcpy_s etc */
#include <stdlib.h> /* for malloc/free etc */

#define CHECK_REF_POINTER(ptr, siz) do {	\
	if (!(ptr) || ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_UNIQUE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_ENCLAVE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_within_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define ADD_ASSIGN_OVERFLOW(a, b) (	\
	((a) += (b)) < (b)	\
)


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

static sgx_status_t SGX_CDECL sgx_ecall_run_join(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_run_join_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_run_join_t* ms = SGX_CAST(ms_ecall_run_join_t*, pms);
	ms_ecall_run_join_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_run_join_t), ms, sizeof(ms_ecall_run_join_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	const entry_t* _tmp_table_data = __in_ms.ms_table_data;
	size_t _tmp_total_rows = __in_ms.ms_total_rows;
	size_t _len_table_data = _tmp_total_rows * sizeof(entry_t);
	entry_t* _in_table_data = NULL;
	const table_desc_t* _tmp_table_descs = __in_ms.ms_table_descs;
	size_t _tmp_num_tables = __in_ms.ms_num_tables;
	size_t _len_table_descs = _tmp_num_tables * sizeof(table_desc_t);
	table_desc_t* _in_table_descs = NULL;
	const join_node_desc_t* _tmp_join_tree = __in_ms.ms_join_tree;
	size_t _tmp_num_nodes = __in_ms.ms_num_nodes;
	size_t _len_join_tree = _tmp_num_nodes * sizeof(join_node_desc_t);
	join_node_desc_t* _in_join_tree = NULL;
	const output_col_t* _tmp_out_cols = __in_ms.ms_out_cols;
	size_t _tmp_num_out_cols = __in_ms.ms_num_out_cols;
	size_t _len_out_cols = _tmp_num_out_cols * sizeof(output_col_t);
	output_col_t* _in_out_cols = NULL;
	int32_t* _tmp_result_count = __in_ms.ms_result_count;
	size_t _len_result_count = sizeof(int32_t);
	int32_t* _in_result_count = NULL;
	sgx_status_t _in_retval;

	if (sizeof(*_tmp_table_data) != 0 &&
		(size_t)_tmp_total_rows > (SIZE_MAX / sizeof(*_tmp_table_data))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	if (sizeof(*_tmp_table_descs) != 0 &&
		(size_t)_tmp_num_tables > (SIZE_MAX / sizeof(*_tmp_table_descs))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	if (sizeof(*_tmp_join_tree) != 0 &&
		(size_t)_tmp_num_nodes > (SIZE_MAX / sizeof(*_tmp_join_tree))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	if (sizeof(*_tmp_out_cols) != 0 &&
		(size_t)_tmp_num_out_cols > (SIZE_MAX / sizeof(*_tmp_out_cols))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_table_data, _len_table_data);
	CHECK_UNIQUE_POINTER(_tmp_table_descs, _len_table_descs);
	CHECK_UNIQUE_POINTER(_tmp_join_tree, _len_join_tree);
	CHECK_UNIQUE_POINTER(_tmp_out_cols, _len_out_cols);
	CHECK_UNIQUE_POINTER(_tmp_result_count, _len_result_count);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_table_data != NULL && _len_table_data != 0) {
		_in_table_data = (entry_t*)malloc(_len_table_data);
		if (_in_table_data == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_table_data, _len_table_data, _tmp_table_data, _len_table_data)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_table_descs != NULL && _len_table_descs != 0) {
		_in_table_descs = (table_desc_t*)malloc(_len_table_descs);
		if (_in_table_descs == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_table_descs, _len_table_descs, _tmp_table_descs, _len_table_descs)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_join_tree != NULL && _len_join_tree != 0) {
		_in_join_tree = (join_node_desc_t*)malloc(_len_join_tree);
		if (_in_join_tree == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_join_tree, _len_join_tree, _tmp_join_tree, _len_join_tree)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_out_cols != NULL && _len_out_cols != 0) {
		_in_out_cols = (output_col_t*)malloc(_len_out_cols);
		if (_in_out_cols == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_out_cols, _len_out_cols, _tmp_out_cols, _len_out_cols)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	if (_tmp_result_count != NULL && _len_result_count != 0) {
		if ( _len_result_count % sizeof(*_tmp_result_count) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		if ((_in_result_count = (int32_t*)malloc(_len_result_count)) == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		memset((void*)_in_result_count, 0, _len_result_count);
	}
	_in_retval = ecall_run_join((const entry_t*)_in_table_data, _tmp_total_rows, (const table_desc_t*)_in_table_descs, _tmp_num_tables, (const join_node_desc_t*)_in_join_tree, _tmp_num_nodes, (const output_col_t*)_in_out_cols, _tmp_num_out_cols, _in_result_count);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}
	if (_in_result_count) {
		if (memcpy_verw_s(_tmp_result_count, _len_result_count, _in_result_count, _len_result_count)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_table_data) free(_in_table_data);
	if (_in_table_descs) free(_in_table_descs);
	if (_in_join_tree) free(_in_join_tree);
	if (_in_out_cols) free(_in_out_cols);
	if (_in_result_count) free(_in_result_count);
	return status;
}

static sgx_status_t SGX_CDECL sgx_ecall_decrypt_rows(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_ecall_decrypt_rows_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_ecall_decrypt_rows_t* ms = SGX_CAST(ms_ecall_decrypt_rows_t*, pms);
	ms_ecall_decrypt_rows_t __in_ms;
	if (memcpy_s(&__in_ms, sizeof(ms_ecall_decrypt_rows_t), ms, sizeof(ms_ecall_decrypt_rows_t))) {
		return SGX_ERROR_UNEXPECTED;
	}
	sgx_status_t status = SGX_SUCCESS;
	entry_t* _tmp_rows = __in_ms.ms_rows;
	size_t _tmp_count = __in_ms.ms_count;
	size_t _len_rows = _tmp_count * sizeof(entry_t);
	entry_t* _in_rows = NULL;
	sgx_status_t _in_retval;

	if (sizeof(*_tmp_rows) != 0 &&
		(size_t)_tmp_count > (SIZE_MAX / sizeof(*_tmp_rows))) {
		return SGX_ERROR_INVALID_PARAMETER;
	}

	CHECK_UNIQUE_POINTER(_tmp_rows, _len_rows);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_rows != NULL && _len_rows != 0) {
		_in_rows = (entry_t*)malloc(_len_rows);
		if (_in_rows == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_rows, _len_rows, _tmp_rows, _len_rows)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}
	_in_retval = ecall_decrypt_rows(_in_rows, _tmp_count);
	if (memcpy_verw_s(&ms->ms_retval, sizeof(ms->ms_retval), &_in_retval, sizeof(_in_retval))) {
		status = SGX_ERROR_UNEXPECTED;
		goto err;
	}
	if (_in_rows) {
		if (memcpy_verw_s(_tmp_rows, _len_rows, _in_rows, _len_rows)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}
	}

err:
	if (_in_rows) free(_in_rows);
	return status;
}

SGX_EXTERNC const struct {
	size_t nr_ecall;
	struct {void* ecall_addr; uint8_t is_priv; uint8_t is_switchless;} ecall_table[2];
} g_ecall_table = {
	2,
	{
		{(void*)(uintptr_t)sgx_ecall_run_join, 0, 0},
		{(void*)(uintptr_t)sgx_ecall_decrypt_rows, 0, 0},
	}
};

SGX_EXTERNC const struct {
	size_t nr_ocall;
	uint8_t entry_table[7][2];
} g_dyn_entry_table = {
	7,
	{
		{0, 0, },
		{0, 0, },
		{0, 0, },
		{0, 0, },
		{0, 0, },
		{0, 0, },
		{0, 0, },
	}
};


sgx_status_t SGX_CDECL ocall_stream_result(const entry_t* rows, size_t count)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_rows = count * sizeof(entry_t);

	ms_ocall_stream_result_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_stream_result_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(rows, _len_rows);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (rows != NULL) ? _len_rows : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_stream_result_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_stream_result_t));
	ocalloc_size -= sizeof(ms_ocall_stream_result_t);

	if (rows != NULL) {
		if (memcpy_verw_s(&ms->ms_rows, sizeof(const entry_t*), &__tmp, sizeof(const entry_t*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, rows, _len_rows)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_rows);
		ocalloc_size -= _len_rows;
	} else {
		ms->ms_rows = NULL;
	}

	if (memcpy_verw_s(&ms->ms_count, sizeof(ms->ms_count), &count, sizeof(count))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(0, ms);

	if (status == SGX_SUCCESS) {
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL ocall_get_time_ns(uint64_t* ns)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_ns = sizeof(uint64_t);

	ms_ocall_get_time_ns_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_ocall_get_time_ns_t);
	void *__tmp = NULL;

	void *__tmp_ns = NULL;

	CHECK_ENCLAVE_POINTER(ns, _len_ns);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (ns != NULL) ? _len_ns : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_ocall_get_time_ns_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_ocall_get_time_ns_t));
	ocalloc_size -= sizeof(ms_ocall_get_time_ns_t);

	if (ns != NULL) {
		if (memcpy_verw_s(&ms->ms_ns, sizeof(uint64_t*), &__tmp, sizeof(uint64_t*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_ns = __tmp;
		if (_len_ns % sizeof(*ns) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		memset_verw(__tmp_ns, 0, _len_ns);
		__tmp = (void *)((size_t)__tmp + _len_ns);
		ocalloc_size -= _len_ns;
	} else {
		ms->ms_ns = NULL;
	}

	status = sgx_ocall(1, ms);

	if (status == SGX_SUCCESS) {
		if (ns) {
			if (memcpy_s((void*)ns, _len_ns, __tmp_ns, _len_ns)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_cpuinfo = 4 * sizeof(int);

	ms_sgx_oc_cpuidex_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_oc_cpuidex_t);
	void *__tmp = NULL;

	void *__tmp_cpuinfo = NULL;

	CHECK_ENCLAVE_POINTER(cpuinfo, _len_cpuinfo);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (cpuinfo != NULL) ? _len_cpuinfo : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_oc_cpuidex_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_oc_cpuidex_t));
	ocalloc_size -= sizeof(ms_sgx_oc_cpuidex_t);

	if (cpuinfo != NULL) {
		if (memcpy_verw_s(&ms->ms_cpuinfo, sizeof(int*), &__tmp, sizeof(int*))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp_cpuinfo = __tmp;
		if (_len_cpuinfo % sizeof(*cpuinfo) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		memset_verw(__tmp_cpuinfo, 0, _len_cpuinfo);
		__tmp = (void *)((size_t)__tmp + _len_cpuinfo);
		ocalloc_size -= _len_cpuinfo;
	} else {
		ms->ms_cpuinfo = NULL;
	}

	if (memcpy_verw_s(&ms->ms_leaf, sizeof(ms->ms_leaf), &leaf, sizeof(leaf))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_subleaf, sizeof(ms->ms_subleaf), &subleaf, sizeof(subleaf))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(2, ms);

	if (status == SGX_SUCCESS) {
		if (cpuinfo) {
			if (memcpy_s((void*)cpuinfo, _len_cpuinfo, __tmp_cpuinfo, _len_cpuinfo)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_wait_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(3, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_set_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(4, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_setwait_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);

	if (memcpy_verw_s(&ms->ms_waiter, sizeof(ms->ms_waiter), &waiter, sizeof(waiter))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	if (memcpy_verw_s(&ms->ms_self, sizeof(ms->ms_self), &self, sizeof(self))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(5, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_waiters = total * sizeof(void*);

	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(waiters, _len_waiters);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (waiters != NULL) ? _len_waiters : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_multiple_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);

	if (waiters != NULL) {
		if (memcpy_verw_s(&ms->ms_waiters, sizeof(const void**), &__tmp, sizeof(const void**))) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		if (_len_waiters % sizeof(*waiters) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_verw_s(__tmp, ocalloc_size, waiters, _len_waiters)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_waiters);
		ocalloc_size -= _len_waiters;
	} else {
		ms->ms_waiters = NULL;
	}

	if (memcpy_verw_s(&ms->ms_total, sizeof(ms->ms_total), &total, sizeof(total))) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}

	status = sgx_ocall(6, ms);

	if (status == SGX_SUCCESS) {
		if (retval) {
			if (memcpy_s((void*)retval, sizeof(*retval), &ms->ms_retval, sizeof(ms->ms_retval))) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

