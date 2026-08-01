# Reproducible build/run environment for the oblivious multi-way join comparison.
#
# Carries BOTH SGX toolchains, because the three engines do not share one:
#   single-ecall (this repo)  Intel SGX SDK
#   batching                  Intel SGX SDK   (fetched by baselines/build_batching.sh)
#   OBLIVIATOR                OpenEnclave     (fetched by baselines/build_obliviator.sh)
#
# Pinned to the toolchain this project was benchmarked on:
#   Ubuntu 22.04 (jammy) + gcc 11.4 + Intel SGX SDK 2.22.100.3 + OpenEnclave.
#
# Build args:
#   SGX_MODE         SIM | HW  (default SIM -- correctness anywhere, no SGX device)
#   VECTORIZE        off | on  (default off -- SIM is Rosetta/QEMU-safe, no AVX2)
#   WITH_OBLIVIATOR  0 | 1     (default 0 -- 1 adds OpenEnclave for the OBLIVIATOR
#                              baseline, which needs real SGX; see the NOTE below)
#
# ---- SIM (default): correctness on any x86, incl. Apple Silicon emulation ----
#   docker build -t omwj-sim .
#   docker run --platform linux/amd64 --rm omwj-sim          # runs run_all.sh
#   Covers single-ecall and batching.  OBLIVIATOR has no simulation path
#   upstream, so the three-way comparison needs hardware.
#
# ---- HW: paper timings, requires a real SGX host / Azure DCsv3 VM ----
#   docker build --build-arg SGX_MODE=HW --build-arg VECTORIZE=on -t omwj-hw .
#   docker run --device /dev/sgx_enclave --device /dev/sgx_provision \
#              -v /var/run/aesmd:/var/run/aesmd --rm -e FULL=1 omwj-hw
#
# See REPRODUCE.md for the full reproducibility guide.

FROM ubuntu:22.04

ARG SGX_MODE=SIM
ARG VECTORIZE=off
ARG SGX_SDK_VERSION=2.22.100.3
ARG DEBIAN_FRONTEND=noninteractive

# Base toolchain + fetch/verify utilities.  python3 (stdlib only) drives the
# SQLite correctness comparison; no pip packages are required.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates wget gnupg python3 make \
        libssl-dev libcurl4-openssl-dev protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

# --- Intel SGX PSW runtime (only exercised in HW mode; harmless in SIM) ---
# Installed unconditionally so a single image serves both SGX_MODE values.
RUN echo 'deb [arch=amd64 signed-by=/etc/apt/keyrings/intel-sgx.gpg] ' \
        'https://download.01.org/intel-sgx/sgx_repo/ubuntu jammy main' \
        > /etc/apt/sources.list.d/intel-sgx.list \
    && mkdir -p /etc/apt/keyrings \
    && wget -qO - https://download.01.org/intel-sgx/sgx_repo/ubuntu/intel-sgx-deb.key \
        | gpg --dearmor -o /etc/apt/keyrings/intel-sgx.gpg \
    && apt-get update && apt-get install -y --no-install-recommends \
        libsgx-enclave-common libsgx-urts libsgx-launch libsgx-epid \
        libsgx-quote-ex libsgx-uae-service \
    && rm -rf /var/lib/apt/lists/* \
    || echo "PSW install skipped (SIM-only build can proceed without it)"

# --- Intel SGX SDK (SIM libraries + sgx_sign live here; needed in both modes) ---
# Pinned by sha256 so the build fails loudly if download.01.org is reorganised
# or the binary changes, rather than silently building against something else.
ARG SGX_SDK_SHA256=941bd4e1c2b7c982688f4e6c6438715b18bf1ae4f2bf3c6c8d420ed792ab79c6
RUN wget -q "https://download.01.org/intel-sgx/sgx-linux/${SGX_SDK_VERSION%.*.*}/distro/ubuntu22.04-server/sgx_linux_x64_sdk_${SGX_SDK_VERSION}.bin" \
        -O /tmp/sgx_sdk.bin \
    && echo "${SGX_SDK_SHA256}  /tmp/sgx_sdk.bin" | sha256sum -c - \
    && chmod +x /tmp/sgx_sdk.bin \
    && /tmp/sgx_sdk.bin --prefix /opt/intel \
    && rm -f /tmp/sgx_sdk.bin
ENV SGX_SDK=/opt/intel/sgxsdk
ENV PATH="${SGX_SDK}/bin:${SGX_SDK}/bin/x64:${PATH}"
ENV PKG_CONFIG_PATH="${SGX_SDK}/pkgconfig"
ENV LD_LIBRARY_PATH="${SGX_SDK}/sdk_libs"

# --- OpenEnclave (for the OBLIVIATOR baseline only) -----------------------
# single-ecall and batching use the Intel SDK above; OBLIVIATOR is an
# OpenEnclave enclave, so the three-engine comparison needs both toolchains.
#
# NOTE: OBLIVIATOR has no simulation path upstream -- oe_create_parallel_enclave
# is called with flags=0 -- so it runs only on real SGX.  The SIM tier therefore
# compares two engines; see REPRODUCE.md.  Pass --build-arg WITH_OBLIVIATOR=1
# to add this layer for the HW three-engine tier.
#
# The default is 0 deliberately.  Microsoft publishes open-enclave for Ubuntu
# 20.04 (focal) but not for 22.04 (jammy), which this image pins, so the layer
# below fails with "E: Unable to locate package open-enclave" on any machine.
# Defaulting it on would break `docker build -t omwj-sim .` for every SIM
# reviewer over a dependency the SIM tier cannot use.
ARG WITH_OBLIVIATOR=0
ENV OE_PREFIX=/opt/openenclave
RUN if [ "${WITH_OBLIVIATOR}" = "1" ]; then \
        mkdir -p /etc/apt/keyrings \
        && wget -qO - https://packages.microsoft.com/keys/microsoft.asc \
             | gpg --dearmor -o /etc/apt/keyrings/microsoft.gpg \
        && echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/microsoft.gpg] https://packages.microsoft.com/ubuntu/22.04/prod jammy main" \
             > /etc/apt/sources.list.d/microsoft-prod.list \
        && apt-get update \
        && apt-get install -y --no-install-recommends \
             open-enclave unzip curl patch libmpich-dev mpich \
        && rm -rf /var/lib/apt/lists/* ; \
    else \
        apt-get update && apt-get install -y --no-install-recommends unzip curl patch \
        && rm -rf /var/lib/apt/lists/* \
        && echo "WITH_OBLIVIATOR=0: OpenEnclave omitted (two-engine image)" ; \
    fi

# --- Project source + build ---
WORKDIR /opt/omwj
COPY . .
# Restore the executable bit on scripts: artifact archives (zip on Zenodo/ACM
# DL) do not preserve the Unix exec bit, so the ENTRYPOINT would otherwise fail
# with "permission denied" even when the git tree has the mode set.
RUN chmod +x scripts/*.sh scripts/*.py baselines/*.sh \
    && make clean \
    && make SGX_MODE=${SGX_MODE} VECTORIZE=${VECTORIZE} SGX_DEBUG=0 \
            Enclave_Config=enclave/trusted/configs/heap_2g.xml

# Bake the chosen mode into the default entrypoint env so `docker run <img>`
# with no extra args reproduces the correctness suite in the built flavour.
ENV SGX_MODE=${SGX_MODE}
ENV VECTORIZE=${VECTORIZE}
ENTRYPOINT ["scripts/run_all.sh"]
