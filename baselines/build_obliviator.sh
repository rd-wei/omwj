#!/bin/bash
# Fetch, patch and build the OBLIVIATOR baseline.
#
#   A. Mavrogiannakis, X. Wang, I. Demertzis, D. Papadopoulos, M. Garofalakis,
#   "OBLIVIATOR: OBLIVIous Parallel Joins and other OperATORs in Shared Memory
#   Environments", USENIX Security '25.
#   Artifact: https://zenodo.org/records/14723872  (DOI 10.5281/zenodo.14723872)
#
# The upstream artifact ships no LICENSE file, so this repository redistributes
# NONE of it.  We download it here, verify its sha256, and apply our own patch
# series (baselines/obliviator/patches/) -- the diffs are the only Obliviator-
# related content we ship.
#
# The patches, and why each exists.  01 and 03 are needed because the published
# artifact does NOT build, and then does not load, as shipped:
#   01-oe-prefix       upstream includes <mbedtls/error.h> and links -lmbedcrypto,
#                      but OpenEnclave installs mbedtls under its own 3rdparty
#                      tree, so the build stops at common/error.c.  Points both at
#                      $(OE_PREFIX), and adds -D OE_SIMULATION_CERT (the flag
#                      upstream itself sets in the sibling tpch_q5/our_2/Makefile).
#   02-elem-size       ELEM_SIZE 32 -> 320.  The stock 14-byte payload cannot hold
#                      a TPC-H tuple at all, so the query cannot run unpatched.
#                      320 B is the SMALLEST that fits every join order we
#                      evaluate (TM3's widest plan needs 298 chars); a larger
#                      element materially slows their sort, so do not inflate it.
#   03-heap            NumHeapPages 36700160 -> 1048576, i.e. a 140 GB enclave
#                      heap down to the 4 GB every engine here is given.  140 GB
#                      cannot be created on any machine we have; the value is a
#                      placeholder, not a measured requirement.
#   04-daemon-repl     a stdin REPL + READY marker in the host, so one enclave
#                      can serve a multi-step join pipeline and enclave-creation
#                      time becomes separately measurable.  Without it every
#                      pipeline step pays a fresh ~90 s enclave creation.
#   05-fix-distribute  OPTIONAL (OBLIVIATOR_FIX=1).  A one-line correctness fix.
#                      OFF by default: the default build must reproduce their
#                      artifact as published, with our correction available as an
#                      explicit, auditable extra.
#
# Env:
#   OBLIVIATOR_SRC   use an existing unpacked artifact instead of downloading
#   PREFIX           where to unpack        (default ./external)
#   OE_PREFIX        OpenEnclave install    (autodetected: /opt/openenclave,
#                                            then $HOME/openenclave-install)
#   OBLIVIATOR_FIX   1 = also apply the correctness fix
#   SKIP_BUILD       1 = fetch and patch only
set -eu

cd "$(dirname "$0")/.."
REPO="$PWD"
PATCHES="$REPO/baselines/obliviator/patches"
PREFIX="${PREFIX:-$REPO/external}"
# /opt/openenclave is where the open-enclave package installs (and where the
# Dockerfile puts it); $HOME/openenclave-install is the common source-build
# location.  Explicit OE_PREFIX always wins.
if [ -z "${OE_PREFIX:-}" ]; then
    for c in /opt/openenclave "$HOME/openenclave-install"; do
        [ -d "$c" ] && { OE_PREFIX="$c"; break; }
    done
fi
OE_PREFIX="${OE_PREFIX:-/opt/openenclave}"

ZIP_URL="https://zenodo.org/records/14723872/files/Parallel-join-ae.zip?download=1"
ZIP_SHA256="6ac812fcdb9e3c79c339db27ab1f12c849ab418f331f599cbda78f8052d72fe3"

SRC="${OBLIVIATOR_SRC:-$PREFIX/Parallel-join-ae}"

# ---- fetch ---------------------------------------------------------------
if [ ! -d "$SRC" ]; then
    mkdir -p "$PREFIX"
    ZIP="$PREFIX/Parallel-join-ae.zip"
    if [ ! -f "$ZIP" ]; then
        echo "==> downloading OBLIVIATOR artifact from Zenodo"
        curl -fL --retry 3 -o "$ZIP" "$ZIP_URL"
    fi
    echo "==> verifying sha256"
    echo "$ZIP_SHA256  $ZIP" | sha256sum -c - || {
        echo "ERROR: checksum mismatch. The Zenodo record may have been revised;" >&2
        echo "       do not proceed with an unverified baseline." >&2
        exit 1
    }
    echo "==> unpacking into $PREFIX"
    ( cd "$PREFIX" && unzip -q -o "$(basename "$ZIP")" )
fi
[ -d "$SRC/join" ] || { echo "ERROR: no join/ under $SRC" >&2; exit 1; }

# ---- patch ---------------------------------------------------------------
# Idempotent: a patch already applied is skipped, never applied twice.  A patch
# that neither applies nor is already present is a hard error -- silently
# building a half-patched baseline would poison every number downstream.
apply() {
    local p="$PATCHES/$1.patch"
    [ -f "$p" ] || { echo "ERROR: missing patch $p" >&2; exit 1; }
    # --no-backup-if-mismatch: never leave *.orig beside a patched file, so the
    # patched tree is exactly the sources and nothing else.
    if patch -p1 --dry-run -s -f -i "$p" >/dev/null 2>&1; then
        patch -p1 -s --no-backup-if-mismatch -i "$p"
        echo "    applied  $1"
    elif patch -p1 -R --dry-run -s -f -i "$p" >/dev/null 2>&1; then
        echo "    present  $1"
    else
        echo "ERROR: $1 neither applies nor is already applied to $SRC" >&2
        echo "       (upstream may have changed; refusing to build)" >&2
        exit 1
    fi
}

echo "==> patching"
( cd "$SRC"
  apply 01-oe-prefix
  apply 02-elem-size
  apply 03-heap
  apply 04-daemon-repl
  if [ "${OBLIVIATOR_FIX:-0}" = "1" ]; then
      apply 05-fix-distribute
      echo "    NOTE: correctness fix ENABLED -- this is no longer the artifact as published"
  else
      echo "    skipped  05-fix-distribute (set OBLIVIATOR_FIX=1 to enable)"
  fi )

# ---- build ---------------------------------------------------------------
if [ "${SKIP_BUILD:-0}" = "1" ]; then
    echo "==> SKIP_BUILD=1, stopping after patch"
    exit 0
fi
[ -d "$OE_PREFIX" ] || {
    echo "ERROR: no OpenEnclave install at $OE_PREFIX (set OE_PREFIX)" >&2
    exit 1
}
# OBLIVIATOR is an OpenEnclave enclave with no simulation path
# (oe_create_parallel_enclave is called with flags=0), so it needs real SGX.
[ -e /dev/sgx_enclave ] || echo "WARNING: /dev/sgx_enclave absent -- the build will \
succeed but the binary cannot run here (no SIM support upstream)" >&2

echo "==> building (OE_PREFIX=$OE_PREFIX)"
make -C "$SRC/join" OE_PREFIX="$OE_PREFIX"

echo
echo "OBLIVIATOR ready at $SRC"
echo "  run the comparison with:  scripts/run_tm_3engine.sh"
