#ifndef EJ_AES_CRYPTO_H
#define EJ_AES_CRYPTO_H

#include "common/entry_t.h"
#include "entry_crypto.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

crypto_status_t aes_encrypt_entry(entry_t* entry);
crypto_status_t aes_decrypt_entry(entry_t* entry);

uint64_t get_next_nonce(void);
void     reset_nonce_counter(void);
void     init_aes_key(void);

extern uint8_t aes_key[16];
extern int     aes_key_initialized;

#ifdef __cplusplus
}
#endif

#endif /* EJ_AES_CRYPTO_H */
