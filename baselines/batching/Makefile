#
# Main Makefile for Memory Constrained Oblivious Multi-Way Join
# Reorganized structure with common headers
#

######## SGX SDK Settings ########

SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
SGX_ARCH ?= x64
SGX_DEBUG ?= 1

# Detect 32-bit architecture
ifeq ($(shell getconf LONG_BIT), 32)
	SGX_ARCH := x86
else ifeq ($(findstring -m32, $(CXXFLAGS)), -m32)
	SGX_ARCH := x86
endif

# Architecture-specific settings
ifeq ($(SGX_ARCH), x86)
	SGX_COMMON_FLAGS := -m32
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib
	SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x86/sgx_sign
	SGX_EDGER8R := $(SGX_SDK)/bin/x86/sgx_edger8r
else
	SGX_COMMON_FLAGS := -m64
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
	SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x64/sgx_sign
	SGX_EDGER8R := $(SGX_SDK)/bin/x64/sgx_edger8r
endif

# Debug/Release flags
ifeq ($(SGX_DEBUG), 1)
ifeq ($(SGX_PRERELEASE), 1)
$(error Cannot set SGX_DEBUG and SGX_PRERELEASE at the same time!!)
endif
endif

ifeq ($(SGX_DEBUG), 1)
	SGX_COMMON_FLAGS += -O0 -g
else
	SGX_COMMON_FLAGS += -O2
endif

# Warning flags
SGX_COMMON_FLAGS += -Wall -Wextra -Winit-self -Wpointer-arith -Wreturn-type \
                    -Waddress -Wsequence-point -Wformat-security \
                    -Wmissing-include-dirs -Wfloat-equal -Wundef -Wshadow \
                    -Wcast-align -Wconversion -Wredundant-decls
SGX_COMMON_CFLAGS := $(SGX_COMMON_FLAGS) -Wjump-misses-init -Wstrict-prototypes
SGX_COMMON_CXXFLAGS := $(SGX_COMMON_FLAGS) -Wnon-virtual-dtor -std=c++11

######## App Settings ########

ifneq ($(SGX_MODE), HW)
	Urts_Library_Name := sgx_urts_sim
else
	Urts_Library_Name := sgx_urts
endif

# App source files
App_Cpp_Files := main/sgx_join/main.cpp \
                 app/crypto/crypto_utils.cpp \
                 app/file_io/converters.cpp \
                 app/file_io/table_io.cpp \
                 app/data_structures/entry.cpp \
                 app/data_structures/table.cpp \
                 app/join/join_condition.cpp \
                 app/join/join_constraint.cpp \
                 app/join/join_tree_builder.cpp \
                 app/join/join_attribute_setter.cpp \
                 app/query/query_tokenizer.cpp \
                 app/query/query_parser.cpp \
                 app/query/inequality_parser.cpp \
                 app/query/condition_merger.cpp \
                 app/algorithms/bottom_up_phase.cpp \
                 app/algorithms/top_down_phase.cpp \
                 app/algorithms/distribute_expand.cpp \
                 app/algorithms/align_concat.cpp \
                 app/algorithms/oblivious_join.cpp \
                 app/algorithms/merge_sort_manager.cpp \
                 app/algorithms/shuffle_manager.cpp \
                 app/algorithms/distribute_manager.cpp \
                 app/batch/ecall_batch_collector.cpp \
                 app/batch/ecall_wrapper.cpp \
                 app/batch/performance_timer.cpp \
                 app/utils/counted_ocalls.cpp \
                 app/debug/debug_util.cpp \
                 app/debug/debug_manager.cpp

# Include paths - common is first priority
App_Include_Paths := -I$(SGX_SDK)/include -Icommon -Iapp -Ienclave/untrusted

App_Compile_CFlags := -fPIC -Wno-attributes $(App_Include_Paths)

# Debug configuration
ifeq ($(SGX_DEBUG), 1)
	App_Compile_CFlags += -DDEBUG -UNDEBUG -UEDEBUG
else ifeq ($(SGX_PRERELEASE), 1)
	App_Compile_CFlags += -DNDEBUG -DEDEBUG -UDEBUG
else
	App_Compile_CFlags += -DNDEBUG -UEDEBUG -UDEBUG
endif

# Allow override of DEBUG_LEVEL
ifndef DEBUG_LEVEL
    DEBUG_LEVEL := 0
endif

# Add SLIM_ENTRY flag if specified
ifdef SLIM_ENTRY
    App_Compile_CFlags += -DSLIM_ENTRY
endif

# Allow MAX_BATCH_SIZE override: make MAX_BATCH_SIZE=8000
ifdef MAX_BATCH_SIZE
    App_Compile_CFlags += -DMAX_BATCH_SIZE=$(MAX_BATCH_SIZE)
endif

# Allow WAKSMAN_THRESHOLD override: make WAKSMAN_THRESHOLD=8
ifdef WAKSMAN_THRESHOLD
    App_Compile_CFlags += -DWAKSMAN_THRESHOLD=$(WAKSMAN_THRESHOLD)
endif

App_Compile_CXXFlags := $(App_Compile_CFlags) -std=c++11 -DDEBUG_LEVEL=$(DEBUG_LEVEL)
App_Link_Flags := -L$(SGX_LIBRARY_PATH) -l$(Urts_Library_Name) -lpthread

ifneq ($(SGX_MODE), HW)
	App_Link_Flags += -lsgx_uae_service_sim
else
	App_Link_Flags += -lsgx_uae_service
endif

# Generated untrusted source
Gen_Untrusted_Source := enclave/untrusted/Enclave_u.c
Gen_Untrusted_Object := enclave/untrusted/Enclave_u.o
App_Objects := $(App_Cpp_Files:.cpp=.o) $(Gen_Untrusted_Object)

App_Name := sgx_app
Encrypt_Tool := encrypt_tables

######## Enclave Settings ########

ifneq ($(SGX_MODE), HW)
	Trts_Library_Name := sgx_trts_sim
	Service_Library_Name := sgx_tservice_sim
else
	Trts_Library_Name := sgx_trts
	Service_Library_Name := sgx_tservice
endif
Crypto_Library_Name := sgx_tcrypto

# Enclave source files
Enclave_Cpp_Files := enclave/trusted/Enclave.cpp
Enclave_C_Files := enclave/trusted/crypto/aes_crypto.c \
                   enclave/trusted/crypto/crypto_helpers.c \
                   enclave/trusted/operations/window_functions.c \
                   enclave/trusted/operations/comparators.c \
                   enclave/trusted/operations/merge_comparators.c \
                   enclave/trusted/operations/transform_functions.c \
                   enclave/trusted/operations/distribute_functions.c \
                   enclave/trusted/algorithms/min_heap.c \
                   enclave/trusted/algorithms/heap_sort.c \
                   enclave/trusted/algorithms/oblivious_waksman.c \
                   enclave/trusted/algorithms/k_way_merge.c \
                   enclave/trusted/algorithms/k_way_shuffle.c \
                   enclave/trusted/algorithms/aks_distribute.c \
                   enclave/trusted/batch/batch_dispatcher.c \
                   enclave/trusted/debug_wrapper.c \
                   enclave/trusted/test/test_ecalls.c \
                   enclave/trusted/test/test_crypto_ecalls.c

# Include paths - common is first priority
Enclave_Include_Paths := -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc \
                         -I$(SGX_SDK)/include/libcxx -Icommon -Ienclave/trusted

# Compiler version check for stack protector
CC_BELOW_4_9 := $(shell expr "`$(CC) -dumpversion`" \< "4.9")
ifeq ($(CC_BELOW_4_9), 1)
	Enclave_Compile_CFlags := -fstack-protector
else
	Enclave_Compile_CFlags := -fstack-protector-strong
endif

Enclave_Compile_CFlags += -nostdinc -ffreestanding -fvisibility=hidden \
                          -fpie -ffunction-sections -fdata-sections \
                          -DENCLAVE_BUILD \
                          $(Enclave_Include_Paths)
Enclave_Compile_CXXFlags := -nostdinc++ $(Enclave_Compile_CFlags) -std=c++11

# Add SLIM_ENTRY flag if specified
ifdef SLIM_ENTRY
    Enclave_Compile_CFlags += -DSLIM_ENTRY
    Enclave_Compile_CXXFlags += -DSLIM_ENTRY
endif

# Allow MAX_BATCH_SIZE override (also affects MERGE_BUFFER_SIZE inside enclave)
ifdef MAX_BATCH_SIZE
    Enclave_Compile_CFlags += -DMAX_BATCH_SIZE=$(MAX_BATCH_SIZE)
    Enclave_Compile_CXXFlags += -DMAX_BATCH_SIZE=$(MAX_BATCH_SIZE)
endif

# Enclave linking
Enclave_Link_Flags := -Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L$(SGX_LIBRARY_PATH) \
                      -Wl,--whole-archive -l$(Trts_Library_Name) -Wl,--no-whole-archive \
                      -Wl,--start-group -lsgx_tstdc -lsgx_tcxx -l$(Crypto_Library_Name) \
                      -l$(Service_Library_Name) -Wl,--end-group \
                      -Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined \
                      -Wl,-pie,-eenclave_entry -Wl,--export-dynamic \
                      -Wl,--defsym,__ImageBase=0 -Wl,--gc-sections \
                      -Wl,--version-script=enclave/trusted/Enclave.lds

Enclave_Objects := $(Enclave_Cpp_Files:.cpp=.o) $(Enclave_C_Files:.c=.o)

Enclave_Name := enclave.so
Signed_Enclave_Name := enclave.signed.so
Enclave_Config_File := enclave/trusted/Enclave.config.xml

######## Build Targets ########

.PHONY: all build clean

all: .config_$(Build_Mode)_$(SGX_ARCH) $(App_Name) $(Signed_Enclave_Name)
	@echo "Build complete!"

# Check build mode changes
.config_$(Build_Mode)_$(SGX_ARCH):
	@rm -f .config_* 
	@touch $@

build: all

######## App Build ########

# Generate untrusted edge routines - search in common for headers
$(Gen_Untrusted_Source): $(SGX_EDGER8R) enclave/trusted/Enclave.edl
	@mkdir -p enclave/untrusted
	@cd enclave/untrusted && $(SGX_EDGER8R) --untrusted ../trusted/Enclave.edl \
		--search-path ../trusted --search-path ../../common --search-path $(SGX_SDK)/include
	@echo "GEN  =>  $@"

$(Gen_Untrusted_Object): $(Gen_Untrusted_Source)
	@$(CC) $(SGX_COMMON_CFLAGS) $(App_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

# Compile app source files.
#
# These depend on $(Gen_Untrusted_Source) because the host sources #include
# "Enclave_u.h", which sgx_edger8r emits alongside Enclave_u.c.  Without the
# dependency make is free to compile main.cpp before generating it, and the
# build fails with "Enclave_u.h: No such file or directory".  That never showed
# up while the generated files were committed to the repo; it appears the moment
# you build a clean checkout.
main/%.o: main/%.cpp $(Gen_Untrusted_Source)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

main/tools/%.o: main/tools/%.cpp $(Gen_Untrusted_Source)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

app/%.o: app/%.cpp $(Gen_Untrusted_Source)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Link app
$(App_Name): $(App_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

# Build encrypt_tables utility
Encrypt_Tool_Objects := main/tools/encrypt_tables.o \
                       app/file_io/table_io.o \
                       app/file_io/converters.o \
                       app/crypto/crypto_utils.o \
                       app/data_structures/entry.o \
                       app/data_structures/table.o \
                       app/join/join_condition.o \
                       app/join/join_constraint.o \
                       app/join/join_attribute_setter.o \
                       app/algorithms/merge_sort_manager.o \
                       app/algorithms/shuffle_manager.o \
                       app/algorithms/distribute_manager.o \
                       app/batch/ecall_batch_collector.o \
                       app/batch/ecall_wrapper.o \
                       app/batch/performance_timer.o \
                       app/utils/counted_ocalls.o \
                       app/debug/debug_util.o \
                       app/debug/debug_manager.o \
                       $(Gen_Untrusted_Object)

$(Encrypt_Tool): $(Encrypt_Tool_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Enclave Build ########

# Generate trusted edge routines - search in common for headers
enclave/trusted/Enclave_t.c: $(SGX_EDGER8R) enclave/trusted/Enclave.edl
	@cd enclave/trusted && $(SGX_EDGER8R) --trusted Enclave.edl \
		--search-path . --search-path ../../common --search-path $(SGX_SDK)/include
	@echo "GEN  =>  $@"

enclave/trusted/Enclave_t.o: enclave/trusted/Enclave_t.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

# Compile enclave source files
enclave/trusted/%.o: enclave/trusted/%.cpp
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(Enclave_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

enclave/trusted/%.o: enclave/trusted/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

# Compile enclave subdirectory source files
enclave/trusted/crypto/%.o: enclave/trusted/crypto/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/trusted/operations/%.o: enclave/trusted/operations/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/trusted/algorithms/%.o: enclave/trusted/algorithms/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/trusted/batch/%.o: enclave/trusted/batch/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

enclave/trusted/test/%.o: enclave/trusted/test/%.c
	@$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_Compile_CFlags) -c $< -o $@
	@echo "CC   <=  $<"

# Link enclave
$(Enclave_Name): enclave/trusted/Enclave_t.o $(Enclave_Objects)
	@$(CXX) $^ -o $@ $(Enclave_Link_Flags)
	@echo "LINK =>  $@"

# Sign enclave
$(Signed_Enclave_Name): $(Enclave_Name)
	@$(SGX_ENCLAVE_SIGNER) sign -key enclave/trusted/Enclave_private.pem \
		-enclave $(Enclave_Name) -out $@ -config $(Enclave_Config_File)
	@echo "SIGN =>  $@"

######## Test Programs ########

# Test program settings
Test_Include_Paths := -I$(SGX_SDK)/include -I. -Icommon -Iapp -Ienclave/untrusted
Test_Compile_CFlags := $(SGX_COMMON_CFLAGS) -fPIC -Wno-attributes $(Test_Include_Paths)
Test_Compile_CXXFlags := $(Test_Compile_CFlags) -std=c++17

# Test programs
Test_Join_Objects := tests/integration/test_join.o
Test_Join_Batch_Objects := tests/integration/test_join_batch.o
Sqlite_Baseline_Objects := tests/baseline/sqlite_baseline.o
Test_Merge_Sort_Objects := tests/unit/test_merge_sort.o
Test_Waksman_Objects := tests/unit/test_waksman_shuffle.o
Test_Waksman_Dist_Objects := tests/unit/test_waksman_distribution.o
Test_Shuffle_Manager_Objects := tests/unit/test_shuffle_manager.o

# Common objects needed by test programs (reuse from main app)
Test_Common_Objects := app/crypto/crypto_utils.o \
                      app/file_io/converters.o \
                      app/file_io/table_io.o \
                      app/data_structures/entry.o \
                      app/data_structures/table.o \
                      app/join/join_condition.o \
                      app/join/join_attribute_setter.o \
                      app/algorithms/merge_sort_manager.o \
                      app/algorithms/shuffle_manager.o \
                      app/algorithms/distribute_manager.o \
                      app/batch/ecall_batch_collector.o \
                      app/batch/ecall_wrapper.o \
                      app/batch/performance_timer.o \
                      app/utils/counted_ocalls.o \
                      app/debug/debug_util.o \
                      app/debug/debug_manager.o \
                      $(Gen_Untrusted_Object)

# Test executables
test_join: $(Test_Join_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

sqlite_baseline: $(Sqlite_Baseline_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

test_merge_sort: $(Test_Merge_Sort_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_waksman_shuffle: $(Test_Waksman_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_waksman_distribution: $(Test_Waksman_Dist_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_shuffle_manager: $(Test_Shuffle_Manager_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

test_join_batch: $(Test_Join_Batch_Objects) $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags) -lsqlite3
	@echo "LINK =>  $@"

test_crypto_timing: tests/performance/test_crypto_timing.o $(Test_Common_Objects)
	@$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

# Compile test source files
tests/performance/%.o: tests/performance/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

tests/integration/%.o: tests/integration/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

tests/baseline/%.o: tests/baseline/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

tests/unit/%.o: tests/unit/%.cpp
	@$(CXX) $(Test_Compile_CXXFlags) -c $< -o $@
	@echo "CXX  <=  $<"

# Build all tests
tests: test_join sqlite_baseline test_merge_sort test_waksman_shuffle test_waksman_distribution test_shuffle_manager
	@echo "All tests built successfully"

######## Clean ########

clean:
	@rm -f $(App_Name) $(Enclave_Name) $(Signed_Enclave_Name) $(Encrypt_Tool)
	@rm -f $(App_Objects) $(Enclave_Objects) main/tools/*.o
	@rm -f enclave/trusted/Enclave_t.* $(Gen_Untrusted_Source) $(Gen_Untrusted_Object)
	@rm -f test_join sqlite_baseline $(Test_Join_Objects) $(Sqlite_Baseline_Objects)
	@rm -f .config_*