/*
 * Enclave.cpp — trusted enclave for enclave_join.
 *
 * Full pipeline: decrypt, bottom-up, top-down, distribute-expand,
 * align-concat, project through out_cols, and stream the result tuples
 * back to the host.  A single marker entry precedes the rows, carrying
 * the row count and the row width.
 */

#include "Enclave_t.h"
#include "common/entry_t.h"
#include "common/ecall_types.h"
#include "crypto/aes_crypto.h"
#include "join/bottom_up.h"
#include "join/top_down.h"
#include "join/distribute.h"
#include "join/align.h"
#include "mem_track.h"
#include <sgx_trts.h>
#include <string.h>
#include <stdlib.h>

#define STREAM_CHUNK 256

/* field_type marker for the stream header entry:
 * attributes[0] = row count, attributes[1] = result width. */
#define STREAM_TABLE_MARKER (-999)

/* field_type marker for the per-phase timing entry (streamed last):
 * attributes[0..5] = decrypt, bottom_up, top_down, distribute_expand,
 * align_concat, output_stream durations in MILLISECONDS (microseconds
 * overflow int32 for phases longer than ~35 minutes);
 * attributes[6] = allocation high-water mark in KiB (bytes overflow int32
 * above 2 GB, and the heap tops out at 8 GB). */
#define STREAM_TIMING_MARKER (-998)

static uint64_t now_ns(void) {
    uint64_t t = 0;
    ocall_get_time_ns(&t);
    return t;
}

/* Stream the trimmed result rows: project each trow through out_cols into
 * a staging buffer of wire-format entries, one chunk at a time.
 *
 * Every row is AES-CTR encrypted before it crosses the boundary.  The host is
 * untrusted, so the join result must leave the enclave as ciphertext -- the
 * batching engine does the same thing via save_encrypted_csv, and both use the
 * identical scheme, so one decryptor reads either engine's output.
 *
 * is_encrypted and nonce stay in the clear by construction (see the region map
 * in common/entry_t.h): the host needs the nonce to decrypt, and uses
 * is_encrypted to tell data rows from the plaintext stream markers.
 */
static int stream_result_rows(const join_result_t* result,
                              const output_col_t* out_cols,
                              size_t num_out_cols)
{
    entry_t* chunk = (entry_t*)mt_alloc(STREAM_CHUNK * sizeof(entry_t));
    if (!chunk) return -1;
    memset(chunk, 0, STREAM_CHUNK * sizeof(entry_t));

    int32_t out_width = (num_out_cols > 0) ? (int32_t)num_out_cols
                                           : result->width;
    size_t sent = 0;
    while (sent < result->n) {
        size_t cnt = result->n - sent;
        if (cnt > STREAM_CHUNK) cnt = STREAM_CHUNK;
        for (size_t i = 0; i < cnt; i++) {
            /* The align accumulator (and thus the result) is a narrow rrow_t. */
            const rrow_t* t = rrow_cat(result->rows, sent + i, result->stride);
            entry_t* e = &chunk[i];
            /* The buffer is reused across chunks, and the previous pass left
             * is_encrypted=1 on it.  aes_encrypt_entry refuses an already-
             * encrypted entry, so without this reset every chunk after the
             * first would be emitted in the clear -- a result that is
             * ciphertext for STREAM_CHUNK rows and plaintext thereafter. */
            e->is_encrypted = 0;
            e->nonce        = 0;
            if (num_out_cols > 0) {
                for (size_t c = 0; c < num_out_cols; c++)
                    e->attributes[c] = t->attributes[out_cols[c].attr_idx];
            } else {
                memcpy(e->attributes, t->attributes,
                       4u * (size_t)result->width);
            }
            /* Fatal, never skipped: emitting one plaintext row would defeat
             * the point of encrypting the rest.  -2 distinguishes this from
             * the allocation failure above, so a crypto fault is not reported
             * to the host as out-of-memory. */
            if (aes_encrypt_entry(e) != CRYPTO_SUCCESS) {
                mt_free(chunk);
                return -2;
            }
        }
        ocall_stream_result(chunk, cnt);
        sent += cnt;
    }
    mt_free(chunk);
    return out_width;
}

/* Decrypt result rows in place, so the harness can compare them against the
 * SQLite reference.  Verification aid only -- it is not on the join path, and
 * exists so that encrypting the output does not cost us tuple-exact checking.
 * The key stays inside the enclave. */
sgx_status_t ecall_decrypt_rows(entry_t* rows, size_t count)
{
    if (!rows) return SGX_ERROR_INVALID_PARAMETER;
    for (size_t i = 0; i < count; i++) {
        if (aes_decrypt_entry(&rows[i]) != CRYPTO_SUCCESS)
            return SGX_ERROR_UNEXPECTED;
    }
    return SGX_SUCCESS;
}

sgx_status_t ecall_run_join(
    const entry_t*          table_data,
    size_t                  total_rows,
    const table_desc_t*     table_descs,
    size_t                  num_tables,
    const join_node_desc_t* join_tree,
    size_t                  num_nodes,
    const output_col_t*     out_cols,
    size_t                  num_out_cols,
    int32_t*                result_count)
{
    uint64_t t0 = now_ns();
    mt_reset();

    /* Make a writable copy of all entries. */
    entry_t* rows = (entry_t*)mt_alloc(total_rows * sizeof(entry_t));
    if (!rows) return SGX_ERROR_OUT_OF_MEMORY;
    memcpy(rows, table_data, total_rows * sizeof(entry_t));

    /* Decrypt every encrypted entry. */
    for (size_t t = 0; t < num_tables; t++) {
        uint32_t start = table_descs[t].offset_rows;
        uint32_t count = table_descs[t].num_rows;
        for (uint32_t r = 0; r < count; r++) {
            entry_t* e = &rows[start + r];
            if (e->is_encrypted)
                aes_decrypt_entry(e);
        }
    }
    uint64_t t_decrypt = now_ns();

    /* The four join phases. */
    if (bottom_up(rows, table_descs, num_tables, join_tree, num_nodes) != 0) {
        mt_free(rows);
        return SGX_ERROR_OUT_OF_MEMORY;
    }
    uint64_t t_bottom_up = now_ns();

    if (top_down(rows, table_descs, num_tables, join_tree, num_nodes) != 0) {
        mt_free(rows);
        return SGX_ERROR_OUT_OF_MEMORY;
    }
    uint64_t t_top_down = now_ns();

    expanded_table_t* expanded =
        (expanded_table_t*)mt_alloc(num_nodes * sizeof(expanded_table_t));
    if (!expanded) { mt_free(rows); return SGX_ERROR_OUT_OF_MEMORY; }
    if (distribute_expand(rows, table_descs, join_tree, num_nodes, expanded) != 0) {
        mt_free(expanded);
        mt_free(rows);
        return SGX_ERROR_OUT_OF_MEMORY;
    }
    mt_free(rows);
    uint64_t t_distribute = now_ns();

    join_result_t result;
    if (align_concat(expanded, table_descs, join_tree, num_nodes, &result) != 0) {
        free_expanded(expanded, num_nodes);
        mt_free(expanded);
        return SGX_ERROR_OUT_OF_MEMORY;
    }
    free_expanded(expanded, num_nodes);
    mt_free(expanded);
    uint64_t t_align = now_ns();

    /* Stream the result: marker (count + width), then projected tuples. */
    entry_t marker;
    memset(&marker, 0, sizeof(marker));
    marker.field_type    = STREAM_TABLE_MARKER;
    marker.attributes[0] = (int32_t)result.n;
    marker.attributes[1] = (num_out_cols > 0) ? (int32_t)num_out_cols
                                              : result.width;
    ocall_stream_result(&marker, 1);
    int stream_rc = stream_result_rows(&result, out_cols, num_out_cols);
    if (stream_rc < 0) {
        mt_free(result.rows);
        return (stream_rc == -2) ? SGX_ERROR_UNEXPECTED
                                 : SGX_ERROR_OUT_OF_MEMORY;
    }

    mt_free(result.rows);
    uint64_t t_output = now_ns();

    /* Stream the per-phase timing marker (microseconds per phase). */
    entry_t timing;
    memset(&timing, 0, sizeof(timing));
    timing.field_type    = STREAM_TIMING_MARKER;
    timing.attributes[0] = (int32_t)((t_decrypt    - t0)           / 1000000);
    timing.attributes[1] = (int32_t)((t_bottom_up  - t_decrypt)    / 1000000);
    timing.attributes[2] = (int32_t)((t_top_down   - t_bottom_up)  / 1000000);
    timing.attributes[3] = (int32_t)((t_distribute - t_top_down)   / 1000000);
    timing.attributes[4] = (int32_t)((t_align      - t_distribute) / 1000000);
    timing.attributes[5] = (int32_t)((t_output     - t_align)      / 1000000);
    /* Peak simultaneously-live join-engine allocation, in KiB (F7: peak
     * enclave memory without a min-heap bisection).  Excludes the SGX
     * runtime's own footprint and allocator overhead — see mem_track.h. */
    timing.attributes[6] = (int32_t)(mt_peak_bytes() / 1024);
    ocall_stream_result(&timing, 1);

    *result_count = (int32_t)result.n;
    return SGX_SUCCESS;
}
