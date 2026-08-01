#ifndef OBLIVIOUS_WAKSMAN_H
#define OBLIVIOUS_WAKSMAN_H

#include <stddef.h>
#include "sgx.h"
#include "../../common/enclave_types.h"
#include "../../common/batch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Oblivious 2-way Waksman shuffle
 * 
 * Performs data-oblivious shuffle of n entries using Waksman permutation network.
 * All memory accesses are independent of data values to prevent side-channel attacks.
 * 
 * @param data Array of entries to shuffle (in-place)
 * @param n Number of entries (must be <= MAX_BATCH_SIZE)
 * @return SGX_SUCCESS on success, error code otherwise
 */
sgx_status_t ecall_oblivious_2way_waksman(entry_t* data, size_t n);

#ifdef __cplusplus
}
#endif

#endif // OBLIVIOUS_WAKSMAN_H