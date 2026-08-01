/*
 * mem_track.c — see mem_track.h.
 *
 * Each tracked allocation carries a 16-byte header holding its size, so
 * mt_free knows how much to release without a side table.  16 bytes preserves
 * the 16-byte alignment malloc already guarantees, keeping tracked buffers
 * identically aligned to untracked ones (relevant: the Waksman swap loop is
 * AVX2-vectorised and alignment-sensitive).
 */
#include "mem_track.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t size;
    size_t reserved;   /* padding to 16 bytes; keeps payload alignment */
} mt_hdr_t;

static size_t g_live;
static size_t g_peak;

void* mt_alloc(size_t size)
{
    if (size > SIZE_MAX - sizeof(mt_hdr_t)) return NULL;

    mt_hdr_t* h = (mt_hdr_t*)malloc(sizeof(mt_hdr_t) + size);
    if (!h) return NULL;

    h->size     = size;
    h->reserved = 0;
    g_live += size;
    if (g_live > g_peak) g_peak = g_live;
    return (void*)(h + 1);
}

void* mt_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) return NULL;

    size_t total = count * size;
    void*  p     = mt_alloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void mt_free(void* p)
{
    if (!p) return;

    mt_hdr_t* h = ((mt_hdr_t*)p) - 1;
    g_live -= h->size;
    free(h);
}

size_t mt_peak_bytes(void) { return g_peak; }

size_t mt_live_bytes(void) { return g_live; }

void mt_reset(void) { g_live = 0; g_peak = 0; }
