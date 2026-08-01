/*
 * ocalls.cpp — untrusted implementations of ocalls declared in Enclave.edl,
 * plus the result_writer sink they stream into.
 */
#include "Enclave_u.h"
#include "entry_t.h"
#include "result_writer.h"
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <stdexcept>

/* Matches STREAM_TABLE_MARKER in Enclave.cpp — stream header entry with
 * attributes[0] = row count, attributes[1] = row width. */
#define STREAM_TABLE_MARKER (-999)

/* Matches STREAM_TIMING_MARKER in Enclave.cpp — per-phase durations (ms)
 * in attributes[0..5], allocation high-water mark (KiB) in attributes[6]. */
#define STREAM_TIMING_MARKER (-998)

static FILE*  g_out = nullptr;
static int    g_width = 0;
static size_t g_rows = 0;

void result_writer_open(const std::string& path,
                        const std::vector<std::string>& col_names) {
    g_out = fopen(path.c_str(), "w");
    if (!g_out) throw std::runtime_error("cannot open output file: " + path);
    g_rows = 0;
    for (size_t i = 0; i < col_names.size(); i++) {
        if (i) fputc(',', g_out);
        fputs(col_names[i].c_str(), g_out);
    }
    /* The enclave encrypts every result row before it crosses the boundary, so
     * the values below are AES-CTR ciphertext and each row carries the nonce
     * needed to decrypt it.  Same wire format as the batching engine's
     * save_encrypted_csv, so tools/decrypt_result reads either engine's output. */
    fputs(",nonce\n", g_out);
}

void result_writer_close() {
    if (g_out) fclose(g_out);
    g_out = nullptr;
}

size_t result_writer_rows() {
    return g_rows;
}

/* Called by the enclave in chunks; writes each result tuple as a CSV line.
 * Marker entries carry stream metadata / per-phase timings instead.
 *
 * Data rows arrive encrypted, and field_type sits inside the encrypted region
 * (see the region map in common/entry_t.h), so it can no longer be used to
 * spot markers.  is_encrypted is deliberately left in the clear, so the test
 * inverts: unencrypted => marker, encrypted => data row. */
void ocall_stream_result(const entry_t* rows, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const entry_t* e = &rows[i];
        if (!e->is_encrypted && e->field_type == STREAM_TABLE_MARKER) {
            g_width = e->attributes[1];
            continue;
        }
        if (!e->is_encrypted && e->field_type == STREAM_TIMING_MARKER) {
            double d[6];
            double total = 0;
            for (int c = 0; c < 6; c++) {
                d[c] = e->attributes[c] / 1e3;   /* milliseconds */
                total += d[c];
            }
            printf("PHASE_TIMING(enclave): decrypt=%.3f bottom_up=%.3f "
                   "top_down=%.3f distribute_expand=%.3f align_concat=%.3f "
                   "output_stream=%.3f total=%.3f (s)\n",
                   d[0], d[1], d[2], d[3], d[4], d[5], total);
            /* Peak simultaneously-live join-engine allocation inside the
             * enclave (mem_track.h): the "peak enclave memory" cost figure. */
            printf("PEAK_HEAP: %.2f MiB (%d KiB)\n",
                   e->attributes[6] / 1024.0, e->attributes[6]);
            continue;
        }
        if (!g_out) continue;
        for (int c = 0; c < g_width; c++) {
            if (c) fputc(',', g_out);
            fprintf(g_out, "%d", e->attributes[c]);
        }
        /* Per-row AES-CTR counter; without it the row cannot be decrypted. */
        fprintf(g_out, ",%llu\n", (unsigned long long)e->nonce);
        g_rows++;
    }
}

/* Monotonic host timestamp for the enclave's per-phase timing. */
void ocall_get_time_ns(uint64_t* ns) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
