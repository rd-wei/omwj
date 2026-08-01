#ifndef EJ_ENTRY_T_H
#define EJ_ENTRY_T_H

/*
 * entry_t — the universal row type used throughout the pipeline.
 *
 * IMPORTANT: Field order must match the original enclave_types.h exactly.
 * The AES encryption/decryption code uses offsetof(entry_t, is_encrypted) and
 * offsetof(entry_t, nonce) to compute the two byte regions it operates on.
 * Any layout difference silently corrupts decryption of data produced by the
 * original encrypt_tables tool.
 *
 * Encrypted regions (matching aes_crypto.c):
 *   Region 1: [0,           offsetof(is_encrypted))  — field_type, equality_type
 *   Region 2: [offsetof(nonce)+8, sizeof(entry_t))   — join_attr through attributes[]
 *   Unencrypted: is_encrypted (1 byte), nonce (8 bytes), and 7 alignment pad bytes
 */

#include "constants.h"
#include <stdint.h>
#include <limits.h>

typedef struct {
    /* Row kind and boundary type — ENCRYPTED (region 1). */
    int32_t field_type;     /* SORT_PADDING / SOURCE / START / END / TARGET / DIST_PADDING */
    int32_t equality_type;  /* NONE / EQ / NEQ */

    /* Encryption bookkeeping — NOT encrypted; visible to host. */
    uint8_t  is_encrypted;
    /* 7 bytes implicit padding here (compiler aligns nonce to 8-byte boundary) */
    uint64_t nonce;

    /* Everything below is ENCRYPTED (region 2). */

    /* Join key. */
    int32_t join_attr;

    /* Persistent metadata (survive across phases). */
    int32_t original_index;
    int32_t local_mult;
    int32_t final_mult;
    int32_t foreign_sum;

    /* Temporary metadata (reused between phases). */
    int32_t local_cumsum;
    int32_t local_interval;
    int32_t foreign_interval;
    int32_t local_weight;

    /* Expansion / alignment metadata. */
    int32_t copy_index;
    int32_t alignment_key;

    /* Distribution fields. */
    int32_t dst_idx;
    int32_t index;

    /* Column data. */
    int32_t attributes[MAX_ATTRIBUTES];
} entry_t;

#endif /* EJ_ENTRY_T_H */
