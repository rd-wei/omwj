/*
 * mem_track.h — allocation high-water mark for the in-enclave join engine.
 *
 * The join pipeline allocates a small number of *bulk* buffers (one per table
 * copy, per sort scratch, per expanded/aligned intermediate); nothing is
 * allocated per row.  Routing those through mt_alloc/mt_calloc/mt_free gives
 * the peak simultaneously-live byte count, which is the "peak enclave memory"
 * figure the evaluation reports (experimenter_instructions.md F7) without
 * running a min-heap bisection.
 *
 * Reported peak counts bytes *requested by the engine*: it excludes the SGX
 * runtime's own footprint, allocator metadata, and heap fragmentation, so the
 * heap a query actually needs is somewhat larger.  It is a lower bound on the
 * working set and an exact measure of the algorithm's own demand.
 *
 * Not thread-safe; the enclave runs single-threaded (one ecall, one thread).
 *
 * Leakage: allocation sizes are already public in this system's model (input
 * sizes, tree shape, and cardinalities are all public), so publishing the peak
 * reveals nothing that the observable heap growth did not already.
 */
#ifndef ENCLAVE_MEM_TRACK_H
#define ENCLAVE_MEM_TRACK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* malloc/calloc/free replacements.  mt_alloc(0) returns a valid, freeable
 * pointer (matching malloc's behaviour here), so zero-row intermediates —
 * which legitimately occur, e.g. an empty deep hop chain — are not mistaken
 * for allocation failures. */
void*  mt_alloc(size_t size);
void*  mt_calloc(size_t count, size_t size);
void   mt_free(void* p);

/* Peak simultaneously-live tracked bytes since the last mt_reset(). */
size_t mt_peak_bytes(void);

/* Currently live tracked bytes (0 at the end of a leak-free run). */
size_t mt_live_bytes(void);

void   mt_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ENCLAVE_MEM_TRACK_H */
