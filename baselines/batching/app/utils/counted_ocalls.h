#ifndef COUNTED_OCALLS_H
#define COUNTED_OCALLS_H

/*
 * Centralized ocall implementations with automatic counting and timing.
 *
 * The actual ocall function signatures are declared in Enclave_u.h (generated
 * from Enclave.edl). This header exists only to pull in the dependencies needed
 * by counted_ocalls.cpp and to document the centralization pattern.
 *
 * Each ocall handler:
 *   1. Stops the in-enclave timer (we are now in untrusted code)
 *   2. Increments the ocall counter
 *   3. Does the actual work (delegates to the manager class)
 *   4. Restarts the in-enclave timer (returning to enclave context)
 */

#include "sgx_urts.h"
#include "../batch/ecall_wrapper.h"    // For g_ocall_count
#include "../batch/performance_timer.h" // For stop/start_in_enclave_timer
#include "../../common/enclave_types.h"
#include "../algorithms/distribute_manager.h"

#endif // COUNTED_OCALLS_H
