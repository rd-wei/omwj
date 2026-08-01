#include "oblivious_sort.h"
#include "mem_track.h"
#include "common/constants.h"
#include <sgx_trts.h>
#include <sgx_tcrypto.h>
#include <stdlib.h>
#include <string.h>

/* ---- Secret random switch bits ----
 *
 * One fresh AES-128 key per shuffle (from sgx_read_rand), expanded into a
 * keystream with AES-CTR.  The network consumes bits in a fixed order, so a
 * flat pre-generated bit buffer suffices. */

typedef struct {
    uint8_t* bits;
    size_t   next;
} bitstream_t;

static int bitstream_init(bitstream_t* bs, size_t num_bits) {
    size_t bytes = (num_bits + 7) / 8;
    bs->bits = (uint8_t*)mt_calloc(bytes, 1);
    bs->next = 0;
    if (!bs->bits) return -1;

    sgx_aes_ctr_128bit_key_t key;
    if (sgx_read_rand((unsigned char*)&key, sizeof(key)) != SGX_SUCCESS) {
        mt_free(bs->bits);
        bs->bits = NULL;
        return -1;
    }
    uint8_t ctr[16] = {0};
    /* Encrypting the zero buffer in place yields the raw keystream. */
    if (sgx_aes_ctr_encrypt(&key, bs->bits, (uint32_t)bytes, ctr, 128,
                            bs->bits) != SGX_SUCCESS) {
        mt_free(bs->bits);
        bs->bits = NULL;
        return -1;
    }
    return 0;
}

static int next_bit(bitstream_t* bs) {
    int b = (bs->bits[bs->next >> 3] >> (bs->next & 7)) & 1;
    bs->next++;
    return b;
}

/* Number of switches in the network below: T(1)=0, T(2)=1,
 * T(n) = (n-1) + 2*T(n/2)  =  n*log2(n) - n + 1  for power-of-two n. */
static size_t num_switches(size_t n) {
    size_t total = 0, groups = 1;
    while (n >= 2) {
        total += groups * (n - 1);
        groups <<= 1;
        n >>= 1;
    }
    return total;
}

/* Word-wise branchless swap of two elements of `words` 64-bit words.
 * Waksman only ever swaps two DISTINCT, non-overlapping records, so the
 * restrict-qualified pointers are a sound no-alias promise that lets the
 * compiler vectorise the loop (same bytes, same addresses -> access pattern
 * and obliviousness unchanged). */
static void swap_words(uint64_t* restrict p1, uint64_t* restrict p2,
                       size_t words, int should_swap) {
    uint64_t mask = (uint64_t)-(uint64_t)(should_swap != 0);
    for (size_t i = 0; i < words; i++) {
        uint64_t diff = (p1[i] ^ p2[i]) & mask;
        p1[i] ^= diff;
        p2[i] ^= diff;
    }
}

/* ---- Waksman permutation network (power-of-two n) ----
 *
 * Mirrors the original waksman_recursive: input switches on adjacent pairs,
 * recurse on even/odd interleaves, output switches on all pairs but the
 * first.  Every switch executes an oblivious swap; the memory access
 * sequence is a function of n and elem_size alone. */
static void waksman(uint8_t* arr, size_t words, size_t start, size_t stride,
                    size_t n, bitstream_t* bs)
{
    size_t bytes = words * sizeof(uint64_t);
    if (n <= 1) return;
    if (n == 2) {
        swap_words((uint64_t*)(arr + start * bytes),
                   (uint64_t*)(arr + (start + stride) * bytes),
                   words, next_bit(bs));
        return;
    }

    size_t half = n / 2;
    for (size_t i = 0; i < half; i++) {
        swap_words((uint64_t*)(arr + (start + (i * 2) * stride) * bytes),
                   (uint64_t*)(arr + (start + (i * 2 + 1) * stride) * bytes),
                   words, next_bit(bs));
    }
    waksman(arr, words, start, stride * 2, half, bs);
    waksman(arr, words, start + stride, stride * 2, half, bs);
    for (size_t i = 1; i < half; i++) {
        swap_words((uint64_t*)(arr + (start + (i * 2) * stride) * bytes),
                   (uint64_t*)(arr + (start + (i * 2 + 1) * stride) * bytes),
                   words, next_bit(bs));
    }
}

/* ---- Public entry points ---- */

int ej_osort_g(void* arr, size_t n, size_t elem_size,
               int (*cmp)(const void*, const void*),
               ej_make_pad_fn make_pad)
{
    if (n <= 1) return 0;
    if (elem_size % sizeof(uint64_t) != 0) return -1;
    size_t words = elem_size / sizeof(uint64_t);

    /* Pad to the next power of two (required by the Waksman network). */
    size_t m = 1;
    while (m < n) m <<= 1;

    /* When n is already a power of two, shuffle and sort the caller's
     * array directly; otherwise stage through a padded copy. */
    uint8_t* buf = (uint8_t*)arr;
    if (m != n) {
        buf = (uint8_t*)mt_alloc(m * elem_size);
        if (!buf) return -1;
        memcpy(buf, arr, n * elem_size);
        for (size_t i = n; i < m; i++)
            make_pad(buf + i * elem_size);
    }

    bitstream_t bs;
    if (bitstream_init(&bs, num_switches(m)) != 0) {
        if (buf != (uint8_t*)arr) mt_free(buf);
        return -1;
    }
    waksman(buf, words, 0, 1, m, &bs);
    mt_free(bs.bits);

    /* Post-shuffle, any deterministic comparison sort's access pattern is
     * distributed independently of the input order (the pattern is a
     * function of the secret uniform permutation), so an in-place qsort
     * suffices.  Measured note: an argsort-with-gather variant (sort 4-byte
     * indices, move wide rows once) is SLOWER under EPC paging — its
     * indirect comparisons read wide rows at random positions O(n log n)
     * times, while qsort's partition sweeps are sequential and sub-EPC
     * partitions stop paging entirely. */
    qsort(buf, m, elem_size, cmp);

    /* Real elements sort before all padding. */
    if (buf != (uint8_t*)arr) {
        memcpy(arr, buf, n * elem_size);
        mt_free(buf);
    }
    return 0;
}

