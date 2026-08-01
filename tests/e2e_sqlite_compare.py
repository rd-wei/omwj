#!/usr/bin/env python3
"""
End-to-end test: compare sgx_join output against a SQLite baseline.

Usage:
    tests/e2e_sqlite_compare.py <query.sql> <plaintext_dir> <result.csv>

Loads every CSV in <plaintext_dir> into an in-memory SQLite database, runs
the query, and compares the resulting tuple multiset against <result.csv>
(produced by ./sgx_join on the corresponding encrypted directory).

Column order may differ between the two engines (sgx_join emits columns in
join-tree order, SQLite in FROM-clause order), so rows are aligned by
column name before comparison.  TPC-H column names are globally unique.

Both engines write their result as ciphertext -- the host is untrusted, so the
join output is AES-CTR encrypted before it leaves the enclave, and a trailing
'nonce' column carries the per-row counter.  Such a file is decrypted through
the enclave (./decrypt_result) and then compared tuple-exact like any other, so
encrypting the output costs nothing in verification strength.

Exit code 0 on match, 1 on mismatch or if an encrypted result cannot be
decrypted -- never a silent downgrade to a weaker check.
"""

import csv
import shutil
import sqlite3
import sys
import tempfile
from collections import Counter
from pathlib import Path

# Repo root -- this file lives in tests/.  decrypt_result loads
# enclave.decrypt.signed.so from its working directory, so it must run there
# regardless of where the caller invoked us from.
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / 'scripts'))
import ej_decrypt                                          # noqa: E402


def decrypt_if_needed(result_path, tmpdir):
    """Return a path to a plaintext result CSV.

    Encrypted results are round-tripped through the enclave.  A missing or
    failing decryptor is a hard error: silently falling back to a weaker check
    would turn an unverified result into an apparent pass.
    """
    try:
        return ej_decrypt.plaintext_path(result_path, tmpdir, REPO_ROOT)
    except RuntimeError as e:
        raise SystemExit(f'FAIL: {e}')


def load_tables(db, plaintext_dir):
    for path in sorted(Path(plaintext_dir).glob('*.csv')):
        with open(path) as f:
            rows = list(csv.reader(f))
        header, data = rows[0], rows[1:]
        cols = ', '.join(f'"{c}" INTEGER' for c in header)
        db.execute(f'CREATE TABLE "{path.stem}" ({cols})')
        ph = ', '.join('?' * len(header))
        db.executemany(f'INSERT INTO "{path.stem}" VALUES ({ph})',
                       [[int(v) for v in r] for r in data])
        # Per-column indexes let SQLite use index range scans for band
        # joins instead of quadratic nested loops.
        for c in header:
            db.execute(f'CREATE INDEX "ix_{path.stem}_{c}" '
                       f'ON "{path.stem}"("{c}")')


def main():
    if len(sys.argv) != 4:
        print(__doc__.strip())
        return 2
    query_path, plaintext_dir, result_path = sys.argv[1:4]

    tmpdir = tempfile.mkdtemp(prefix='ej_verify_')
    try:
        return compare(query_path, plaintext_dir,
                       decrypt_if_needed(result_path, tmpdir))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def compare(query_path, plaintext_dir, result_path):
    db = sqlite3.connect(':memory:')
    load_tables(db, plaintext_dir)

    sql = open(query_path).read()
    cur = db.execute(sql)
    sqlite_cols = [d[0] for d in cur.description]
    sqlite_rows = cur.fetchall()

    with open(result_path) as f:
        rows = list(csv.reader(f))
    result_cols, result_data = rows[0], rows[1:]

    if sorted(sqlite_cols) != sorted(result_cols):
        print(f'FAIL: column sets differ\n  sqlite: {sorted(sqlite_cols)}'
              f'\n  sgx:    {sorted(result_cols)}')
        return 1

    # Reorder sgx_join rows into SQLite column order by name.
    perm = [result_cols.index(c) for c in sqlite_cols]
    got = Counter(tuple(int(r[p]) for p in perm) for r in result_data)
    exp = Counter(tuple(r) for r in sqlite_rows)

    if got == exp:
        print(f'PASS: {sum(exp.values())} tuples match SQLite baseline '
              f'({len(sqlite_cols)} columns)')
        return 0

    unexpected = got - exp
    missing = exp - got
    print(f'FAIL: {sum(unexpected.values())} unexpected, '
          f'{sum(missing.values())} missing tuples')
    for t in list(unexpected)[:3]:
        print('  unexpected:', t)
    for t in list(missing)[:3]:
        print('  missing:   ', t)
    return 1


if __name__ == '__main__':
    sys.exit(main())
