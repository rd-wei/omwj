#include "aes_crypto.h"
#include "../secure_key.h"

#include <stddef.h>
#include <string.h>
#include <sgx_tcrypto.h>

static uint64_t g_nonce_counter = 1;

uint8_t aes_key[16] = {0};
int     aes_key_initialized = 0;

void init_aes_key(void) {
    if (!aes_key_initialized) {
        uint32_t key = SECURE_ENCRYPTION_KEY;
        for (int i = 0; i < 16; i++) {
            aes_key[i] = (key >> ((i % 4) * 8)) & 0xFF;
            aes_key[i] ^= (uint8_t)(i * 0x37);
        }
        aes_key_initialized = 1;
    }
}

uint64_t get_next_nonce(void) {
    return g_nonce_counter++;
}

void reset_nonce_counter(void) {
    g_nonce_counter = 1;
}

crypto_status_t aes_encrypt_entry(entry_t* entry) {
    if (!entry)            return CRYPTO_INVALID_PARAM;
    if (entry->is_encrypted) return CRYPTO_ALREADY_ENCRYPTED;

    init_aes_key();
    entry->nonce = get_next_nonce();

    uint8_t ctr[16] = {0};
    memcpy(ctr, &entry->nonce, 8);

    size_t is_enc_off   = offsetof(entry_t, is_encrypted);
    size_t nonce_off    = offsetof(entry_t, nonce);

    struct { size_t start; size_t end; } regions[] = {
        { 0,                              is_enc_off              },
        { nonce_off + sizeof(uint64_t),   sizeof(entry_t)         }
    };

    uint8_t* b = (uint8_t*)entry;
    uint8_t  tmp[sizeof(entry_t)];
    memcpy(tmp, b, sizeof(entry_t));

    for (int i = 0; i < 2; i++) {
        size_t sz = regions[i].end - regions[i].start;
        if (sz == 0) continue;
        sgx_status_t st = sgx_aes_ctr_encrypt(
            (const sgx_aes_ctr_128bit_key_t*)aes_key,
            b   + regions[i].start, (uint32_t)sz, ctr, 128,
            tmp + regions[i].start);
        if (st != SGX_SUCCESS) return CRYPTO_OPERATION_FAILED;
    }

    for (int i = 0; i < 2; i++) {
        size_t sz = regions[i].end - regions[i].start;
        if (sz > 0) memcpy(b + regions[i].start, tmp + regions[i].start, sz);
    }

    entry->is_encrypted = 1;
    return CRYPTO_SUCCESS;
}

crypto_status_t aes_decrypt_entry(entry_t* entry) {
    if (!entry)             return CRYPTO_INVALID_PARAM;
    if (!entry->is_encrypted) return CRYPTO_NOT_ENCRYPTED;

    init_aes_key();

    uint8_t ctr[16] = {0};
    memcpy(ctr, &entry->nonce, 8);

    size_t is_enc_off = offsetof(entry_t, is_encrypted);
    size_t nonce_off  = offsetof(entry_t, nonce);

    struct { size_t start; size_t end; } regions[] = {
        { 0,                              is_enc_off              },
        { nonce_off + sizeof(uint64_t),   sizeof(entry_t)         }
    };

    uint8_t* b = (uint8_t*)entry;
    uint8_t  tmp[sizeof(entry_t)];
    memcpy(tmp, b, sizeof(entry_t));

    for (int i = 0; i < 2; i++) {
        size_t sz = regions[i].end - regions[i].start;
        if (sz == 0) continue;
        sgx_status_t st = sgx_aes_ctr_decrypt(
            (const sgx_aes_ctr_128bit_key_t*)aes_key,
            b   + regions[i].start, (uint32_t)sz, ctr, 128,
            tmp + regions[i].start);
        if (st != SGX_SUCCESS) return CRYPTO_OPERATION_FAILED;
    }

    for (int i = 0; i < 2; i++) {
        size_t sz = regions[i].end - regions[i].start;
        if (sz > 0) memcpy(b + regions[i].start, tmp + regions[i].start, sz);
    }

    entry->is_encrypted = 0;
    return CRYPTO_SUCCESS;
}
