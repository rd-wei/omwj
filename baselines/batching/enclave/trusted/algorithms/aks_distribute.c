#include "aks_distribute.h"
#include "../core.h"
#include "../crypto/aes_crypto.h"
#include "../../common/types_common.h"
#include "../Enclave_t.h"
#include <stdint.h>
#include <stddef.h>

/*
 * AKS recursive oblivious distribute.
 *
 * Correctness proof sketch (mirrors Python prototype):
 *   distribute_inner routes N entries so that after the call every entry with
 *   dst_idx in [index_start_cur, index_start_cur+N) sits at the matching local
 *   position.  distribute_2power handles power-of-2 sub-blocks with a cyclic
 *   rotation state (offset) propagated from the parent level.
 *
 * Obliviousness: the address sequence of every aks_oswap call is determined
 *   solely by N (and the recursion structure), not by any entry values.
 *   oblivious_swap always reads and writes both elements regardless of the
 *   swap condition, so no branch leaks secret bits.
 */

/* Largest power of 2 strictly less than N. */
static size_t aks_pow2_lt(size_t N) {
    size_t p = 1;
    while (p < N) p <<= 1;
    return p >> 1;
}

/* Branchless oblivious swap of buf[i] and buf[j]. */
static void aks_oswap(entry_t* buf, size_t i, size_t j, int cond) {
    oblivious_swap(&buf[i], &buf[j], cond);
}

/* Forward declaration. */
static void distribute_inner(entry_t* buf, size_t start,
                             int32_t index_start_cur, size_t N);

/*
 * Route a power-of-2 block of N entries starting at buf[start].
 * index_start_cur: global output index of this block's first slot.
 * offset:          cyclic rotation carried from the parent level.
 */
static void distribute_2power(entry_t* buf, size_t start,
                              int32_t index_start_cur, int32_t offset,
                              size_t N) {
    if (N <= 1) return;

    int32_t temp2 = (int32_t)(N >> 1);
    int32_t temp  = index_start_cur + temp2;
    int32_t m2    = 0;

    for (size_t i = 0; i < N; i++) {
        m2 += (buf[start + i].dst_idx < temp);
    }

    if (N == 2) {
        aks_oswap(buf, start, start + 1, !(m2 ^ offset));
        return;
    }

    int     cond0 = (offset < temp2);
    int     cond1 = ((offset % temp2 + m2) < temp2);
    int     cond3 = cond0 ^ cond1;
    int32_t ii    = (offset + m2) % temp2;

    for (size_t i = 0; i < (size_t)temp2; i++) {
        int cond2 = cond3 ^ (ii <= (int32_t)i);
        aks_oswap(buf, start + i, start + (size_t)temp2 + i, cond2);
    }

    /* Right half first, then left — matches C reference implementation. */
    distribute_2power(buf, start + (size_t)temp2,
                      index_start_cur + temp2,
                      (offset + m2) % temp2,
                      (size_t)temp2);
    distribute_2power(buf, start,
                      index_start_cur,
                      offset % temp2,
                      (size_t)temp2);
}

/*
 * Route an arbitrary-size block of N entries starting at buf[start].
 * Layout: [n2 left part | n1 right power-of-2 part] where n1 = pow2_lt(N).
 */
static void distribute_inner(entry_t* buf, size_t start,
                             int32_t index_start_cur, size_t N) {
    if (N <= 1) return;

    if (N == 2) {
        /*
         * has_value analogue: field_type != DIST_PADDING.
         * Swap if a real element is in the wrong slot.
         * Both conditions are computed without branches.
         */
        entry_t* e0 = &buf[start];
        entry_t* e1 = &buf[start + 1];
        int hv0 = (e0->field_type != DIST_PADDING);
        int hv1 = (e1->field_type != DIST_PADDING);
        int sw  = (hv0 & (index_start_cur < e0->dst_idx)) |
                  (hv1 & (e1->dst_idx < (index_start_cur + 1)));
        aks_oswap(buf, start, start + 1, sw);
        return;
    }

    size_t  n1   = aks_pow2_lt(N);
    size_t  n2   = N - n1;
    int32_t temp = index_start_cur + (int32_t)n2;
    int32_t m2   = 0;

    /* Count elements in the first n2 positions targeting the left block. */
    for (size_t i = 0; i < n2; i++) {
        m2 += (buf[start + i].dst_idx < temp);
    }

    /* Cross-swaps: pair position i with position i+n1. */
    for (size_t i = 0; i < n2; i++) {
        aks_oswap(buf, start + i, start + i + n1, m2 <= (int32_t)i);
    }

    distribute_inner(buf, start, index_start_cur, n2);
    distribute_2power(buf, start + n2,
                      index_start_cur + (int32_t)n2,
                      ((int32_t)n1 - (int32_t)n2 + m2) % (int32_t)n1,
                      n1);
}

/*
 * Full AKS distribute for large n via ocall-based chunk I/O.
 *
 * Three global EPC buffers handle all data movement; each level of the
 * recursion reuses them sequentially (depth-first, no concurrency).
 *
 * AKS_BUFFER entries fit comfortably in L2 cache (512 * 336 = 172 KB).
 * Pairs per swap chunk = AKS_BUFFER / 2 = 256 so two buffers per chunk.
 */
#define AKS_BUFFER 512

static entry_t g_aks_scan_buf[AKS_BUFFER];  /* scan + base-case working buf */
static entry_t g_aks_left_buf[AKS_BUFFER];  /* left  side of swap pairs     */
static entry_t g_aks_right_buf[AKS_BUFFER]; /* right side of swap pairs     */

/* Forward declarations for mutual recursion. */
static void aks_distribute_inner_large(size_t offset, int32_t index_start, size_t n);
static void aks_distribute_2power_large(size_t offset, int32_t index_start,
                                        int32_t ofs, size_t n);

/*
 * distribute_inner for n > AKS_BUFFER: ocall-fetch each chunk.
 * offset   : entry index into the flat array where this subproblem starts.
 * index_start: global dst_idx of the first slot in this subproblem.
 */
static void aks_distribute_inner_large(size_t offset, int32_t index_start, size_t n) {
    if (n <= 1) return;

    /* Base case: fetch n entries into EPC, run in-place, write back. */
    if (n <= AKS_BUFFER) {
        ocall_aks_read_flat(offset, g_aks_scan_buf, n);
        for (size_t i = 0; i < n; i++) {
            if (g_aks_scan_buf[i].is_encrypted) aes_decrypt_entry(&g_aks_scan_buf[i]);
            /* Padding entries get dst_idx beyond any valid position. */
            if (g_aks_scan_buf[i].field_type == DIST_PADDING)
                g_aks_scan_buf[i].dst_idx = (int32_t)(index_start + (int32_t)n);
        }
        distribute_inner(g_aks_scan_buf, 0, index_start, n);
        for (size_t i = 0; i < n; i++) aes_encrypt_entry(&g_aks_scan_buf[i]);
        ocall_aks_write_flat(offset, g_aks_scan_buf, n);
        return;
    }

    size_t  n1   = aks_pow2_lt(n);
    size_t  n2   = n - n1;
    int32_t temp = index_start + (int32_t)n2;
    int32_t m2   = 0;

    /* Scan phase: count elements in [offset, offset+n2) with dst_idx < temp. */
    for (size_t chunk = 0; chunk < n2; chunk += AKS_BUFFER) {
        size_t csz = (chunk + AKS_BUFFER <= n2) ? AKS_BUFFER : n2 - chunk;
        ocall_aks_read_scan_flat(offset + chunk, g_aks_scan_buf, csz);
        for (size_t i = 0; i < csz; i++) {
            entry_t e = g_aks_scan_buf[i];
            if (e.is_encrypted) aes_decrypt_entry(&e);
            /* Treat DIST_PADDING as targeting beyond the array (always right). */
            int32_t d = (e.field_type == DIST_PADDING) ? (int32_t)(n + offset + 1) : e.dst_idx;
            m2 += (d < temp);
        }
        /* No write-back: entries remain encrypted in untrusted memory. */
    }

    /* Swap phase: cross-swap pairs (offset+i, offset+n1+i) for i in [0, n2).
     * Swap if m2 <= i (positional threshold derived from m2 above). */
    const size_t pair_buf = AKS_BUFFER / 2;
    for (size_t chunk = 0; chunk < n2; chunk += pair_buf) {
        size_t csz = (chunk + pair_buf <= n2) ? pair_buf : n2 - chunk;
        ocall_aks_read_flat(offset + chunk,      g_aks_left_buf,  csz);
        ocall_aks_read_flat(offset + n1 + chunk, g_aks_right_buf, csz);
        for (size_t i = 0; i < csz; i++) {
            if (g_aks_left_buf[i].is_encrypted)  aes_decrypt_entry(&g_aks_left_buf[i]);
            if (g_aks_right_buf[i].is_encrypted) aes_decrypt_entry(&g_aks_right_buf[i]);
            int cond = (m2 <= (int32_t)(chunk + i));
            oblivious_swap(&g_aks_left_buf[i], &g_aks_right_buf[i], cond);
            aes_encrypt_entry(&g_aks_left_buf[i]);
            aes_encrypt_entry(&g_aks_right_buf[i]);
        }
        ocall_aks_write_flat(offset + chunk,      g_aks_left_buf,  csz);
        ocall_aks_write_flat(offset + n1 + chunk, g_aks_right_buf, csz);
    }

    /* Recurse: left subproblem then right. */
    aks_distribute_inner_large(offset, index_start, n2);
    aks_distribute_2power_large(offset + n2, index_start + (int32_t)n2,
                                ((int32_t)n1 - (int32_t)n2 + m2) % (int32_t)n1,
                                n1);
}

/*
 * distribute_2power for n > AKS_BUFFER.
 * ofs: cyclic rotation offset propagated from parent level.
 */
static void aks_distribute_2power_large(size_t offset, int32_t index_start,
                                        int32_t ofs, size_t n) {
    if (n <= 1) return;

    /* Base case. */
    if (n <= AKS_BUFFER) {
        ocall_aks_read_flat(offset, g_aks_scan_buf, n);
        for (size_t i = 0; i < n; i++) {
            if (g_aks_scan_buf[i].is_encrypted) aes_decrypt_entry(&g_aks_scan_buf[i]);
            if (g_aks_scan_buf[i].field_type == DIST_PADDING)
                g_aks_scan_buf[i].dst_idx = (int32_t)(index_start + (int32_t)n);
        }
        distribute_2power(g_aks_scan_buf, 0, index_start, ofs, n);
        for (size_t i = 0; i < n; i++) aes_encrypt_entry(&g_aks_scan_buf[i]);
        ocall_aks_write_flat(offset, g_aks_scan_buf, n);
        return;
    }

    int32_t temp2 = (int32_t)(n >> 1);
    int32_t temp  = index_start + temp2;
    int32_t m2    = 0;

    /* Scan phase: count elements in [offset, offset+n) with dst_idx < temp. */
    for (size_t chunk = 0; chunk < n; chunk += AKS_BUFFER) {
        size_t csz = (chunk + AKS_BUFFER <= n) ? AKS_BUFFER : n - chunk;
        ocall_aks_read_scan_flat(offset + chunk, g_aks_scan_buf, csz);
        for (size_t i = 0; i < csz; i++) {
            entry_t e = g_aks_scan_buf[i];
            if (e.is_encrypted) aes_decrypt_entry(&e);
            int32_t d = (e.field_type == DIST_PADDING) ? (int32_t)(n + offset + 1) : e.dst_idx;
            m2 += (d < temp);
        }
    }

    /* Compute positional swap parameters. */
    int     cond0 = (ofs < temp2);
    int     cond1 = ((ofs % temp2 + m2) < temp2);
    int     cond3 = cond0 ^ cond1;
    int32_t ii    = (ofs + m2) % temp2;

    /* Swap phase: cross-swap pairs (offset+i, offset+temp2+i) for i in [0, temp2).
     * Swap condition: cond3 ^ (ii <= i). */
    const size_t pair_buf = AKS_BUFFER / 2;
    for (size_t chunk = 0; chunk < (size_t)temp2; chunk += pair_buf) {
        size_t csz = (chunk + pair_buf <= (size_t)temp2) ? pair_buf : (size_t)temp2 - chunk;
        ocall_aks_read_flat(offset + chunk,              g_aks_left_buf,  csz);
        ocall_aks_read_flat(offset + (size_t)temp2 + chunk, g_aks_right_buf, csz);
        for (size_t i = 0; i < csz; i++) {
            if (g_aks_left_buf[i].is_encrypted)  aes_decrypt_entry(&g_aks_left_buf[i]);
            if (g_aks_right_buf[i].is_encrypted) aes_decrypt_entry(&g_aks_right_buf[i]);
            int cond2 = cond3 ^ (ii <= (int32_t)(chunk + i));
            oblivious_swap(&g_aks_left_buf[i], &g_aks_right_buf[i], cond2);
            aes_encrypt_entry(&g_aks_left_buf[i]);
            aes_encrypt_entry(&g_aks_right_buf[i]);
        }
        ocall_aks_write_flat(offset + chunk,                 g_aks_left_buf,  csz);
        ocall_aks_write_flat(offset + (size_t)temp2 + chunk, g_aks_right_buf, csz);
    }

    /* Recurse: right first, then left — matches C reference implementation. */
    aks_distribute_2power_large(offset + (size_t)temp2, index_start + temp2,
                                (ofs + m2) % temp2, (size_t)temp2);
    aks_distribute_2power_large(offset, index_start,
                                ofs % temp2, (size_t)temp2);
}

/* Public entry point for large-array AKS distribute. */
void aks_distribute_large(size_t n) {
    aks_distribute_inner_large(0, 0, n);
}

/* Public entry point called from ecall_distribute_small. */
void aks_distribute(entry_t* data, size_t n) {
    /* Decrypt all entries so distribute_inner can read dst_idx / field_type. */
    for (size_t i = 0; i < n; i++) {
        if (data[i].is_encrypted) {
            aes_decrypt_entry(&data[i]);
        }
    }

    /*
     * Assign DIST_PADDING entries dst_idx = n (one past the last valid
     * position).  This ensures they are never counted in any m2 tally
     * (since every temp value in the recursion satisfies temp <= n).
     */
    for (size_t i = 0; i < n; i++) {
        if (data[i].field_type == DIST_PADDING) {
            data[i].dst_idx = (int32_t)n;
        }
    }

    distribute_inner(data, 0, 0, n);

    /* Re-encrypt all entries before returning to untrusted memory. */
    for (size_t i = 0; i < n; i++) {
        aes_encrypt_entry(&data[i]);
    }
}
