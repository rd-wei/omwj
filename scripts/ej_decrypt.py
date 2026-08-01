#!/usr/bin/env python3
"""
Shared helper: turn an encrypted result CSV into a plaintext one.

Both oblivious engines write their join result as AES-CTR ciphertext with a
trailing `nonce` column, so anything that inspects result *values* -- the SQLite
comparison, the cross-orientation content digest -- has to decrypt first.

This matters more than it looks for the digest: the nonce is assigned in
emission order, so the same logical row encrypts differently depending on where
it lands in the output.  Two join-tree orientations that are both correct would
otherwise produce completely different ciphertext and read as a content
disagreement.

Decryption goes back through the enclave (`decrypt_result`), so the key never
leaves it.
"""

import csv
import subprocess
from pathlib import Path

NONCE_COLUMN = 'nonce'


def is_encrypted(path) -> bool:
    """True if the CSV carries a trailing nonce column."""
    with open(path, newline='') as f:
        header = next(csv.reader(f), [])
    return bool(header) and header[-1] == NONCE_COLUMN


def decrypt_to(path, out_path, repo_root):
    """Decrypt `path` into `out_path`; returns out_path.

    A missing or failing decryptor raises: silently falling back to comparing
    ciphertext would turn an unverified result into an apparent pass.
    """
    tool = Path(repo_root) / 'decrypt_result'
    if not tool.exists():
        raise RuntimeError(
            f'{path} is encrypted but {tool} is not built. '
            'Build it with `make` -- refusing to fall back to a check that '
            'does not compare values.')

    proc = subprocess.run([str(tool), str(path), str(out_path)],
                          cwd=str(repo_root), capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f'decrypt_result failed on {path}: '
                           f'{proc.stderr.strip()}')
    return out_path


def plaintext_path(path, tmpdir, repo_root, name='decrypted.csv'):
    """Return `path` itself if already plaintext, else a decrypted copy."""
    if not is_encrypted(path):
        return path
    return decrypt_to(path, Path(tmpdir) / name, repo_root)
