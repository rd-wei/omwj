#ifndef EJ_ENTRY_CRYPTO_H
#define EJ_ENTRY_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CRYPTO_SUCCESS           = 0,
    CRYPTO_ALREADY_ENCRYPTED = 1,
    CRYPTO_NOT_ENCRYPTED     = 2,
    CRYPTO_INVALID_PARAM     = 3,
    CRYPTO_OPERATION_FAILED  = 4
} crypto_status_t;

#ifdef __cplusplus
}
#endif

#endif /* EJ_ENTRY_CRYPTO_H */
