######## SGX SDK settings ########

SGX_SDK    ?= /opt/intel/sgxsdk
SGX_MODE   ?= HW
SGX_ARCH   ?= x64
SGX_DEBUG  ?= 1

ifeq ($(SGX_ARCH), x86)
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib
	SGX_EDGER8R      := $(SGX_SDK)/bin/x86/sgx_edger8r
	SGX_SIGNER       := $(SGX_SDK)/bin/x86/sgx_sign
	SGX_COMMON_FLAGS := -m32
else
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
	SGX_EDGER8R      := $(SGX_SDK)/bin/x64/sgx_edger8r
	SGX_SIGNER       := $(SGX_SDK)/bin/x64/sgx_sign
	SGX_COMMON_FLAGS := -m64
endif

ifeq ($(SGX_DEBUG), 1)
	SGX_COMMON_FLAGS += -O0 -g
else
	SGX_COMMON_FLAGS += -O2
endif

SGX_COMMON_FLAGS += -Wall -Wextra
SGX_COMMON_CFLAGS   := $(SGX_COMMON_FLAGS) -Wstrict-prototypes
SGX_COMMON_CXXFLAGS := $(SGX_COMMON_FLAGS) -std=c++17

# --- Waksman-swap vectorisation (oblivious_sort.o) ---
# Built with AVX2 by default for HW/perf.  AVX2 SIGILLs under Rosetta 2 / QEMU
# x86 emulation (e.g. Docker on Apple Silicon), so SIM builds default to no
# vectorisation for portability.  The AVX2 swap is byte-identical to the scalar
# one, so correctness is unaffected.  Override on an AVX2 x86 host with
# `make ... VECTORIZE=on`.
ifeq ($(SGX_MODE), SIM)
	VECTORIZE ?= off
else
	VECTORIZE ?= on
endif
ifeq ($(VECTORIZE), on)
	EJ_VEC_FLAGS := -O3 -ftree-vectorize -mavx2
else
	EJ_VEC_FLAGS :=
endif

ifeq ($(SGX_MODE), HW)
	Urts_Library  := sgx_urts
	Trts_Library  := sgx_trts
	Svc_Library   := sgx_tservice
else
	Urts_Library  := sgx_urts_sim
	Trts_Library  := sgx_trts_sim
	Svc_Library   := sgx_tservice_sim
endif
Crypto_Library := sgx_tcrypto

######## Non-SGX (Phase 1–4) sources ########

QUERY_SRCS := app/query/query_parser.cpp \
              app/query/query_tokenizer.cpp \
              app/query/inequality_parser.cpp \
              app/query/condition_merger.cpp \
              app/join/join_constraint.cpp
QUERY_OBJS := $(QUERY_SRCS:.cpp=.o)

IO_SRCS := app/io/table_reader.cpp
IO_OBJS := $(IO_SRCS:.cpp=.o)

JOIN_SRCS := app/join/join_tree_serializer.cpp
JOIN_OBJS := $(JOIN_SRCS:.cpp=.o)

######## App (host) settings ########

App_Include := -I. -I$(SGX_SDK)/include -Icommon -Iapp -Ienclave/untrusted
App_CFlags  := $(SGX_COMMON_CXXFLAGS) -fPIC -Wno-attributes $(App_Include)

App_Srcs := app/sgx/sgx_join.cpp \
            app/sgx/ocalls.cpp \
            $(QUERY_SRCS) \
            $(IO_SRCS) \
            $(JOIN_SRCS)
App_Objs  := $(App_Srcs:.cpp=.o) enclave/untrusted/Enclave_u.o

# HW must load the PSW uRTS from the system (ldconfig) — do NOT rpath the SDK
# lib dir or it picks the wrong (SDK) uRTS and sgx_create_enclave fails.
# SIM libs live only in the SDK lib dir, so bake it in as DT_RPATH (old dtags,
# via --disable-new-dtags) so the loader resolves the sim uRTS and its
# transitive deps (urts_sim -> uae_service_sim) without LD_LIBRARY_PATH.
ifeq ($(SGX_MODE), SIM)
App_Link := -L$(SGX_LIBRARY_PATH) -Wl,--disable-new-dtags -Wl,-rpath,$(SGX_LIBRARY_PATH) -l$(Urts_Library) -lpthread
else
App_Link := -L$(SGX_LIBRARY_PATH) -l$(Urts_Library) -lpthread
endif

######## Enclave (trusted) settings ########

Enclave_Include := -I. -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc \
                   -I$(SGX_SDK)/include/libcxx -Icommon -Ienclave/trusted

CC_BELOW_49 := $(shell expr "`$(CC) -dumpversion`" \< "4.9")
ifeq ($(CC_BELOW_49), 1)
	SP_FLAG := -fstack-protector
else
	SP_FLAG := -fstack-protector-strong
endif

Enclave_CFlags := $(SP_FLAG) -nostdinc -ffreestanding -fvisibility=hidden \
                  -fpie -ffunction-sections -fdata-sections -DENCLAVE_BUILD \
                  $(SGX_COMMON_FLAGS) -Wstrict-prototypes \
                  $(Enclave_Include)
# -Wstrict-prototypes is C-only; strip it for C++ compilation
Enclave_CXXFlags := -nostdinc++ $(filter-out -Wstrict-prototypes,$(Enclave_CFlags)) -std=c++17

Enclave_Link := -Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles \
                -L$(SGX_LIBRARY_PATH) \
                -Wl,--whole-archive -l$(Trts_Library) -Wl,--no-whole-archive \
                -Wl,--start-group -lsgx_tstdc -lsgx_tcxx -l$(Crypto_Library) \
                -l$(Svc_Library) -Wl,--end-group \
                -Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined \
                -Wl,-pie,-eenclave_entry -Wl,--export-dynamic \
                -Wl,--defsym,__ImageBase=0 -Wl,--gc-sections \
                -Wl,--version-script=enclave/trusted/Enclave.lds

Enclave_Cpp_Srcs := enclave/trusted/Enclave.cpp
Enclave_C_Srcs   := enclave/trusted/mem_track.c \
                    enclave/trusted/crypto/aes_crypto.c \
                    enclave/trusted/join/oblivious_sort.c \
                    enclave/trusted/join/comparators.c \
                    enclave/trusted/join/skinny.c \
                    enclave/trusted/join/trow.c \
                    enclave/trusted/join/bottom_up.c \
                    enclave/trusted/join/top_down.c \
                    enclave/trusted/join/distribute.c \
                    enclave/trusted/join/align.c
Enclave_Objs     := $(Enclave_Cpp_Srcs:.cpp=.o) $(Enclave_C_Srcs:.c=.o)

Enclave_Name        := enclave.so
Signed_Enclave_Name := enclave.signed.so
# Overridable: pass Enclave_Config=enclave/trusted/configs/heap_<size>.xml
# to sign with a different heap limit.
Enclave_Config      ?= enclave/trusted/Enclave.config.xml
Enclave_Key         := enclave/trusted/Enclave_private.pem

# A second signing of the SAME enclave.so at a 64 MB heap, used only by the
# decrypt_result verification tool.  Decryption needs essentially no heap, and
# loading the join enclave (4 GB) would add ~27 s of enclave creation to every
# correctness check.
Decrypt_Enclave_Name := enclave.decrypt.signed.so
Decrypt_Enclave_Cfg  := enclave/trusted/configs/heap_64m.xml

######## Top-level targets ########

.PHONY: all clean

all: $(Signed_Enclave_Name) $(Decrypt_Enclave_Name) sgx_join decrypt_result

######## EDL → edge routines ########

enclave/untrusted/Enclave_u.c enclave/untrusted/Enclave_u.h: enclave/Enclave.edl
	$(SGX_EDGER8R) --untrusted enclave/Enclave.edl \
		--untrusted-dir enclave/untrusted \
		--search-path . --search-path common --search-path $(SGX_SDK)/include

enclave/trusted/Enclave_t.c enclave/trusted/Enclave_t.h: enclave/Enclave.edl
	$(SGX_EDGER8R) --trusted enclave/Enclave.edl \
		--trusted-dir enclave/trusted \
		--search-path . --search-path common --search-path $(SGX_SDK)/include

######## Compile rules ########

enclave/untrusted/Enclave_u.o: enclave/untrusted/Enclave_u.c
	$(CC) $(SGX_COMMON_CFLAGS) -fPIC -Wno-attributes $(App_Include) -c $< -o $@

enclave/trusted/Enclave_t.o: enclave/trusted/Enclave_t.c
	$(CC) $(Enclave_CFlags) -c $< -o $@

enclave/trusted/%.o: enclave/trusted/%.cpp enclave/trusted/Enclave_t.h
	$(CXX) $(Enclave_CXXFlags) -c $< -o $@

# Trusted C sources sitting directly in enclave/trusted/ (mem_track.c).
# Subdirectory sources match the more specific crypto/ and join/ rules below
# (GNU make prefers the pattern rule with the shorter stem).
enclave/trusted/%.o: enclave/trusted/%.c
	$(CC) $(Enclave_CFlags) -c $< -o $@

enclave/trusted/crypto/%.o: enclave/trusted/crypto/%.c
	$(CC) $(Enclave_CFlags) -c $< -o $@

# oblivious_sort is dominated by the Waksman word-swap loop; build it with
# vectorisation enabled (the restrict-qualified swap lets the compiler use AVX2).
enclave/trusted/join/oblivious_sort.o: enclave/trusted/join/oblivious_sort.c
	$(CC) $(Enclave_CFlags) $(EJ_VEC_FLAGS) -c $< -o $@

enclave/trusted/join/%.o: enclave/trusted/join/%.c
	$(CC) $(Enclave_CFlags) -c $< -o $@

app/%.o: app/%.cpp
	$(CXX) $(App_CFlags) -c $< -o $@

######## Enclave link + sign ########

$(Enclave_Name): enclave/trusted/Enclave_t.o $(Enclave_Objs)
	$(CXX) $^ -o $@ $(Enclave_Link)


$(Signed_Enclave_Name): $(Enclave_Name) $(Enclave_Config)
	$(SGX_SIGNER) sign -key $(Enclave_Key) -enclave $(Enclave_Name) -out $@ -config $(Enclave_Config)

$(Decrypt_Enclave_Name): $(Enclave_Name) $(Decrypt_Enclave_Cfg)
	$(SGX_SIGNER) sign -key $(Enclave_Key) -enclave $(Enclave_Name) -out $@ -config $(Decrypt_Enclave_Cfg)

######## Host application ########

# Ensure generated header exists before any app source is compiled.
app/sgx/sgx_join.o app/sgx/ocalls.o app/tools/decrypt_result.o: enclave/untrusted/Enclave_u.h

sgx_join: $(App_Objs)
	$(CXX) $^ -o $@ $(App_Link)

# Verification tool: decrypts either engine's result CSV so the SQLite
# comparison stays tuple-exact now that results leave the enclave encrypted.
# ocalls.o is linked in because Enclave_u.o carries the untrusted bridges for
# ocall_stream_result / ocall_get_time_ns and needs their definitions, even
# though this tool only ever calls ecall_decrypt_rows.
decrypt_result: app/tools/decrypt_result.o app/sgx/ocalls.o enclave/untrusted/Enclave_u.o
	$(CXX) $^ -o $@ $(App_Link)

######## Phase 1-4 test programs (no SGX) ########

NOSGX_CXX    := g++
NOSGX_FLAGS  := -std=c++17 -Wall -Wextra -Wpedantic -Icommon -Iapp -g

tests/%.o: tests/%.cpp
	$(NOSGX_CXX) $(NOSGX_FLAGS) -c $< -o $@

%.o: %.cpp
	$(NOSGX_CXX) $(NOSGX_FLAGS) -c $< -o $@

test_query_parser: tests/test_query_parser.o $(QUERY_OBJS)
	$(NOSGX_CXX) $^ -o $@

test_table_reader: tests/test_table_reader.o $(IO_OBJS)
	$(NOSGX_CXX) $^ -o $@

test_join_tree_serializer: tests/test_join_tree_serializer.o $(QUERY_OBJS) $(IO_OBJS) $(JOIN_OBJS)
	$(NOSGX_CXX) $^ -o $@

######## Clean ########

clean:
	find . -name '*.o' -delete
	rm -f enclave/untrusted/Enclave_u.c enclave/untrusted/Enclave_u.h
	rm -f enclave/trusted/Enclave_t.c enclave/trusted/Enclave_t.h
	rm -f $(Enclave_Name) $(Signed_Enclave_Name) $(Decrypt_Enclave_Name)
	rm -f sgx_join decrypt_result
	rm -f test_query_parser test_table_reader test_join_tree_serializer
