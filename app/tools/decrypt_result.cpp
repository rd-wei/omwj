/*
 * decrypt_result — turn an encrypted result CSV into a plaintext one.
 *
 * Both engines write their join result as ciphertext: the host is untrusted, so
 * the result is AES-CTR encrypted before it leaves the enclave, and each row
 * carries the nonce needed to decrypt it (the trailing `nonce` column).
 *
 * That is correct, but it would cost us tuple-exact correctness checking, so
 * this tool exists purely for verification: it feeds the rows back through the
 * enclave (ecall_decrypt_rows) and writes the plaintext CSV that
 * tests/e2e_sqlite_compare.py compares against SQLite.  The key never leaves
 * the enclave.
 *
 * It reads BOTH engines' output.  The single-ecall engine and the batching
 * engine use the identical scheme -- same key derivation, same two encrypted
 * regions, same nonce-and-zeros counter block -- so one decryptor serves both.
 *
 * Why rebuilding a whole entry_t works: AES-CTR is a byte-stream cipher, so the
 * keystream byte at a given offset depends only on the nonce and that offset.
 * Placing the CSV values back at their true attributes[] offsets in a zeroed
 * entry_t therefore decrypts them correctly.  The surrounding bytes (join_attr
 * and friends) decrypt to garbage because we never had their ciphertext -- we
 * do not read them.
 *
 * Usage:  decrypt_result <encrypted.csv> <plaintext.csv>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sgx_urts.h>
#include "Enclave_u.h"
#include "entry_t.h"

/* Signed from the same enclave.so but with a 64 MB heap: decryption needs
 * essentially no heap, and the join enclave's 4 GB heap would otherwise cost
 * ~27 s of enclave creation on every verification. */
static const char* ENCLAVE_PATH = "enclave.decrypt.signed.so";

/* Rows per ecall.  Bounded so a large result does not need one giant buffer. */
static const size_t BATCH = 4096;

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::istringstream ss(line);
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <encrypted.csv> <plaintext.csv>\n", argv[0]);
        return 2;
    }

    std::ifstream in(argv[1]);
    if (!in) { fprintf(stderr, "ERROR: cannot open %s\n", argv[1]); return 1; }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        fprintf(stderr, "ERROR: %s is empty\n", argv[1]);
        return 1;
    }
    std::vector<std::string> header = split_csv(header_line);
    if (header.empty() || header.back() != "nonce") {
        fprintf(stderr, "ERROR: %s has no trailing 'nonce' column -- it is not "
                        "an encrypted result\n", argv[1]);
        return 1;
    }
    const size_t width = header.size() - 1;      /* data columns, sans nonce */
    if (width > MAX_ATTRIBUTES) {
        fprintf(stderr, "ERROR: %zu columns exceeds MAX_ATTRIBUTES (%d)\n",
                width, MAX_ATTRIBUTES);
        return 1;
    }

    sgx_enclave_id_t eid = 0;
    sgx_status_t ret = sgx_create_enclave(ENCLAVE_PATH, SGX_DEBUG_FLAG,
                                          nullptr, nullptr, &eid, nullptr);
    if (ret != SGX_SUCCESS) {
        fprintf(stderr, "ERROR: sgx_create_enclave(%s) failed (0x%x)\n",
                ENCLAVE_PATH, ret);
        return 1;
    }

    FILE* out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "ERROR: cannot write %s\n", argv[2]);
        sgx_destroy_enclave(eid);
        return 1;
    }
    /* Plaintext copy carries the data columns only; the nonce was a transport
     * detail and means nothing once the row is decrypted. */
    for (size_t i = 0; i < width; i++) {
        if (i) fputc(',', out);
        fputs(header[i].c_str(), out);
    }
    fputc('\n', out);

    std::vector<entry_t> batch;
    batch.reserve(BATCH);
    size_t rows = 0;
    int rc = 0;

    auto flush = [&]() -> bool {
        if (batch.empty()) return true;
        sgx_status_t st = SGX_SUCCESS;
        sgx_status_t call = ecall_decrypt_rows(eid, &st, batch.data(),
                                               batch.size());
        if (call != SGX_SUCCESS || st != SGX_SUCCESS) {
            fprintf(stderr, "ERROR: ecall_decrypt_rows failed "
                            "(call=0x%x status=0x%x)\n", call, st);
            return false;
        }
        for (const entry_t& e : batch) {
            for (size_t c = 0; c < width; c++) {
                if (c) fputc(',', out);
                fprintf(out, "%d", e.attributes[c]);
            }
            fputc('\n', out);
        }
        batch.clear();
        return true;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cell = split_csv(line);
        if (cell.size() != width + 1) {
            fprintf(stderr, "ERROR: row %zu has %zu fields, expected %zu\n",
                    rows + 1, cell.size(), width + 1);
            rc = 1;
            break;
        }
        entry_t e;
        memset(&e, 0, sizeof(e));
        for (size_t c = 0; c < width; c++)
            e.attributes[c] = (int32_t)strtol(cell[c].c_str(), nullptr, 10);
        e.nonce        = strtoull(cell[width].c_str(), nullptr, 10);
        e.is_encrypted = 1;
        batch.push_back(e);
        rows++;
        if (batch.size() == BATCH && !flush()) { rc = 1; break; }
    }
    if (rc == 0 && !flush()) rc = 1;

    fclose(out);
    sgx_destroy_enclave(eid);
    if (rc == 0) printf("decrypted %zu rows (%zu columns)\n", rows, width);
    return rc;
}
