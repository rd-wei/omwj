#include "comparators.h"
#include <stdint.h>

void ej_oblivious_swap_g(void* a, void* b, size_t size_bytes, int should_swap) {
    uint64_t mask = (uint64_t)-(uint64_t)(should_swap != 0);
    uint64_t* p1 = (uint64_t*)a;
    uint64_t* p2 = (uint64_t*)b;
    size_t words = size_bytes / sizeof(uint64_t);
    for (size_t i = 0; i < words; i++) {
        uint64_t diff = (p1[i] ^ p2[i]) & mask;
        p1[i] ^= diff;
        p2[i] ^= diff;
    }
}
