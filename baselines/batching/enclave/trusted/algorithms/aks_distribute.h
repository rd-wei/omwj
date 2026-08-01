#ifndef AKS_DISTRIBUTE_H
#define AKS_DISTRIBUTE_H

#include "../enclave_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AKS-style oblivious distribute.
 *
 * Routes n entries so that entry with dst_idx == k ends up at position k.
 * Input invariant (enforced by caller):
 *   - Real entries (field_type != DIST_PADDING) are at the front,
 *     their dst_idx values are strictly increasing: 0 = d[0] < d[1] < ...
 *   - DIST_PADDING entries fill the remainder.
 * Inside this function DIST_PADDING entries get dst_idx = n (past end)
 * so they are never counted as targeting any output slot.
 *
 * All entries are decrypted on entry, re-encrypted on return.
 * Memory access pattern depends only on n, not on data values.
 */
void aks_distribute(entry_t* data, size_t n);

/*
 * Full AKS distribute for arbitrary n via ocall-based chunk I/O.
 * Data lives in untrusted memory; enclave fetches/stores via ocalls.
 */
void aks_distribute_large(size_t n);

#ifdef __cplusplus
}
#endif

#endif /* AKS_DISTRIBUTE_H */
