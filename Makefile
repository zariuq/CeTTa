SHELL = /bin/bash
CC = gcc
LLVM_OPT ?= opt
LLVM_CLANG ?= clang
.DEFAULT_GOAL := all

include src/generated/cetta_execution_contracts.generated.mk

# Build mode:
#   make                   -> BUILD=python      (default: Python foreign-module support enabled)
#   make BUILD=core        -> bare CeTTa (no Python, no static MORK bridge)
#   make BUILD=python      -> CeTTa + Python (no static MORK bridge)
#   make BUILD=mork        -> bridge build without Python (canonical no-Python bridge mode)
#   make BUILD=main        -> bridge build with Python (canonical bridge mode)
#   make BUILD=pathmap     -> compatibility alias of BUILD=mork
#   make BUILD=full        -> compatibility alias of BUILD=main
#
# Core and python builds do not auto-link local MORK artifacts. Non-mork
# builds can still load a bridge dynamically at runtime via
# CETTA_MORK_SPACE_BRIDGE_LIB or a globally installed libcetta_space_bridge.so.
BUILD ?= python
BUILD_CANON := $(BUILD)
ifneq ($(filter $(BUILD_CANON),core python mork main pathmap full),$(BUILD_CANON))
$(error BUILD must be core, python, mork, main, pathmap, or full)
endif

ENABLE_PYTHON := 0
ENABLE_MORK_STATIC := 0
ENABLE_PATHMAP_SPACE := 0
ENABLE_GMP ?= 1
ENABLE_RUNTIME_STATS ?= 0
ENABLE_RUNTIME_TIMING ?= 0
ENABLE_SANITIZERS ?= 0
ENABLE_PIC ?= 0
CETTA_PROVENANCE_ASSERT ?= 0
RHOCOST_COMMIT_AUDIT ?= 0
ENABLE_PRIME_RECEIPT_PRIMARY_INDEX ?= 0
ENABLE_PRIME_NEED_HEAP_INDEX ?= 1
ENABLE_PRIME_NEED_CLOSURE_CAPTURE ?= 0
ENABLE_PRIME_EVAL_STACK ?= 1
ENABLE_LIB_PROLOG ?= auto
ENABLE_PETTA_TYPECHECK_V2 ?= 1
ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS ?= 0
PRIME_NEED_ALGEBRA_CHECKS := 98
PRIME_NEED_CLOSURE_CAPTURE_GATE :=
PRIME_NEED_CLOSURE_CAPTURE_STATS_GATE :=
PRIME_EVAL_STACK_GATE :=
PRIME_EVAL_STACK_STATS_GATE :=
ifeq ($(ENABLE_PRIME_NEED_CLOSURE_CAPTURE),1)
PRIME_NEED_ALGEBRA_CHECKS := 106
PRIME_NEED_CLOSURE_CAPTURE_GATE := test-prime-need-closure-capture
ifeq ($(ENABLE_RUNTIME_STATS),1)
PRIME_NEED_CLOSURE_CAPTURE_STATS_GATE := test-prime-need-closure-capture-stats
endif
endif
ifeq ($(ENABLE_PRIME_EVAL_STACK),1)
PRIME_EVAL_STACK_GATE := test-prime-eval-stack
ifeq ($(ENABLE_RUNTIME_STATS),1)
PRIME_EVAL_STACK_STATS_GATE := test-prime-eval-stack-stats
endif
endif
SANITIZERS ?= address,undefined
RHO_BENCH_RUNS ?= 3
RHO_BENCH_THREADS ?= 1,2,4,8
RHO_BENCH_SEED ?= 0xC377A
RHO_BENCH_GENERATED_SIZE_MODE ?= smallest
RHO_BENCH_ENFORCE_BASELINE ?= 0
RHO_BENCH_CSV ?=
RHO_BENCH_CSV_ARG = $(if $(strip $(RHO_BENCH_CSV)),--csv "$(RHO_BENCH_CSV)",)
RHO_COST_BENCH_CSV ?=
RHO_COST_BENCH_CSV_ARG = $(if $(strip $(RHO_COST_BENCH_CSV)),--csv "$(RHO_COST_BENCH_CSV)",)
ifneq ($(filter $(RHO_BENCH_GENERATED_SIZE_MODE),smallest largest),$(RHO_BENCH_GENERATED_SIZE_MODE))
$(error RHO_BENCH_GENERATED_SIZE_MODE must be smallest or largest)
endif
ifneq ($(filter $(RHO_BENCH_ENFORCE_BASELINE),0 1),$(RHO_BENCH_ENFORCE_BASELINE))
$(error RHO_BENCH_ENFORCE_BASELINE must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_GMP),0 1),$(ENABLE_GMP))
$(error ENABLE_GMP must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_SANITIZERS),0 1),$(ENABLE_SANITIZERS))
$(error ENABLE_SANITIZERS must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_PIC),0 1),$(ENABLE_PIC))
$(error ENABLE_PIC must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_PETTA_TYPECHECK_V2),0 1),$(ENABLE_PETTA_TYPECHECK_V2))
$(error ENABLE_PETTA_TYPECHECK_V2 must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS),0 1),$(ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS))
$(error ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS must be 0 or 1)
endif
ifneq ($(filter $(CETTA_PROVENANCE_ASSERT),0 1),$(CETTA_PROVENANCE_ASSERT))
$(error CETTA_PROVENANCE_ASSERT must be 0 or 1)
endif
ifneq ($(filter $(RHOCOST_COMMIT_AUDIT),0 1),$(RHOCOST_COMMIT_AUDIT))
$(error RHOCOST_COMMIT_AUDIT must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_PRIME_RECEIPT_PRIMARY_INDEX),0 1),$(ENABLE_PRIME_RECEIPT_PRIMARY_INDEX))
$(error ENABLE_PRIME_RECEIPT_PRIMARY_INDEX must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_PRIME_NEED_HEAP_INDEX),0 1),$(ENABLE_PRIME_NEED_HEAP_INDEX))
$(error ENABLE_PRIME_NEED_HEAP_INDEX must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_PRIME_NEED_CLOSURE_CAPTURE),0 1),$(ENABLE_PRIME_NEED_CLOSURE_CAPTURE))
$(error ENABLE_PRIME_NEED_CLOSURE_CAPTURE must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_PRIME_EVAL_STACK),0 1),$(ENABLE_PRIME_EVAL_STACK))
$(error ENABLE_PRIME_EVAL_STACK must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_LIB_PROLOG),0 1 auto),$(ENABLE_LIB_PROLOG))
$(error ENABLE_LIB_PROLOG must be 0, 1, or auto)
endif
ifeq ($(ENABLE_RUNTIME_TIMING),1)
ENABLE_RUNTIME_STATS := 1
endif
ifeq ($(BUILD_CANON),python)
ENABLE_PYTHON := 1
endif
ifeq ($(BUILD_CANON),mork)
ENABLE_MORK_STATIC := 1
endif
ifeq ($(BUILD_CANON),main)
ENABLE_PYTHON := 1
ENABLE_MORK_STATIC := 1
endif
ifeq ($(BUILD_CANON),pathmap)
ENABLE_MORK_STATIC := 1
endif
ifeq ($(BUILD_CANON),full)
ENABLE_PYTHON := 1
ENABLE_MORK_STATIC := 1
endif
ifeq ($(ENABLE_MORK_STATIC),1)
ENABLE_PATHMAP_SPACE := 1
endif

BOOTSTRAP_TMPDIR = runtime/bootstrap
CETTA_REPO_DIR ?= $(CURDIR)
CETTA_RUST_DIR ?= $(abspath ./rust)
MORK_BRIDGE_DIR ?= $(CETTA_RUST_DIR)/target/release
MORK_BRIDGE_MANIFEST ?= $(CETTA_RUST_DIR)/cetta-space-bridge/Cargo.toml
MORK_BRIDGE_WORKDIR ?= $(CETTA_RUST_DIR)
MORK_BRIDGE_CARGO ?= cargo +nightly
MORK_BRIDGE_RUSTFLAGS ?= -C target-cpu=native
MORK_BRIDGE_WARNINGS ?= quiet
ifeq ($(MORK_BRIDGE_WARNINGS),quiet)
MORK_BRIDGE_RUSTFLAGS += -Awarnings
endif
MORK_BRIDGE_CARGO_FEATURE_ARGS =
ifeq ($(ENABLE_PATHMAP_SPACE),0)
MORK_BRIDGE_CARGO_FEATURE_ARGS += --no-default-features
endif
MORK_BRIDGE_STATICLIB := $(MORK_BRIDGE_DIR)/libcetta_space_bridge.a
MORK_BRIDGE_FEATURE_TAG := default
ifeq ($(ENABLE_PATHMAP_SPACE),0)
MORK_BRIDGE_FEATURE_TAG := no-default-features
endif
PATHMAP_REPO_DIR ?= $(abspath $(CETTA_REPO_DIR)/../PathMap)
MORK_REPO_DIR ?= $(abspath $(CETTA_REPO_DIR)/../MORK)
PATHMAP_MANIFEST ?= $(PATHMAP_REPO_DIR)/Cargo.toml
MORK_KERNEL_MANIFEST ?= $(MORK_REPO_DIR)/kernel/Cargo.toml
MORK_EXPR_MANIFEST ?= $(MORK_REPO_DIR)/expr/Cargo.toml
MORK_FRONTEND_MANIFEST ?= $(MORK_REPO_DIR)/frontend/Cargo.toml
MORK_INTERNING_MANIFEST ?= $(MORK_REPO_DIR)/interning/Cargo.toml
MORK_BRIDGE_WORKSPACE_MEMBERS := cetta-space cetta-space-bridge cetta-pathmap-adapter cetta-mork-adapter
PATHMAP_DEP_DIR := $(abspath $(dir $(PATHMAP_MANIFEST)))
MORK_KERNEL_DIR := $(abspath $(dir $(MORK_KERNEL_MANIFEST)))
MORK_EXPR_DIR := $(abspath $(dir $(MORK_EXPR_MANIFEST)))
MORK_FRONTEND_DIR := $(abspath $(dir $(MORK_FRONTEND_MANIFEST)))
MORK_INTERNING_DIR := $(abspath $(dir $(MORK_INTERNING_MANIFEST)))
MORK_BRIDGE_PATH_TAG := $(strip $(shell printf '%s\n%s\n%s\n%s\n%s\n' '$(PATHMAP_DEP_DIR)' '$(MORK_KERNEL_DIR)' '$(MORK_EXPR_DIR)' '$(MORK_FRONTEND_DIR)' '$(MORK_INTERNING_DIR)' | sha1sum | cut -c1-12))
MORK_BRIDGE_WORKSPACE_DIR ?= $(abspath $(BOOTSTRAP_TMPDIR))/bridge-workspace.$(MORK_BRIDGE_PATH_TAG)
MORK_BRIDGE_WORKSPACE_MANIFEST ?= $(MORK_BRIDGE_WORKSPACE_DIR)/Cargo.toml
MORK_BRIDGE_REQUIRED_MANIFESTS := \
	$(PATHMAP_MANIFEST) \
	$(MORK_KERNEL_MANIFEST) \
	$(MORK_EXPR_MANIFEST) \
	$(MORK_FRONTEND_MANIFEST) \
	$(MORK_INTERNING_MANIFEST)
MORK_BRIDGE_MISSING_MANIFESTS := $(strip $(foreach manifest,$(MORK_BRIDGE_REQUIRED_MANIFESTS),$(if $(wildcard $(manifest)),,$(manifest))))
ifeq ($(MORK_BRIDGE_MISSING_MANIFESTS),)
MORK_BRIDGE_DEPS_READY := 1
else
MORK_BRIDGE_DEPS_READY := 0
endif
MORK_BRIDGE_SOURCE_DEPS :=
MORK_BRIDGE_FEATURE_STATICLIB := runtime/bootstrap/libcetta_space_bridge.v2.$(MORK_BRIDGE_FEATURE_TAG).$(MORK_BRIDGE_PATH_TAG).a
MORK_BRIDGE_BUILD_STAMP := runtime/bootstrap/mork-bridge.v2.$(MORK_BRIDGE_FEATURE_TAG).$(MORK_BRIDGE_PATH_TAG).stamp
BRIDGE_DEPS =
BRIDGE_CFLAGS =
BRIDGE_LDFLAGS =
MORK_BUILD_HAS_BRIDGE := 0
ifeq ($(ENABLE_MORK_STATIC),1)
MORK_BRIDGE_SOURCE_DEPS := $(shell find $(CETTA_RUST_DIR) -path '$(CETTA_RUST_DIR)/target' -prune -o -type f \( -name '*.rs' -o -name 'Cargo.toml' -o -name 'Cargo.lock' -o -name '*.h' \) -print)
ifeq ($(wildcard $(MORK_BRIDGE_MANIFEST)),)
$(error BUILD=$(BUILD_CANON) requires $(MORK_BRIDGE_MANIFEST))
endif
ifneq ($(MORK_BRIDGE_DEPS_READY),1)
$(error BUILD=$(BUILD_CANON) requires Rust bridge dependencies. Missing: $(MORK_BRIDGE_MISSING_MANIFESTS). Set PATHMAP_REPO_DIR=/path/to/PathMap and MORK_REPO_DIR=/path/to/MORK (or the *_MANIFEST overrides) to point at your local checkouts)
endif
BRIDGE_DEPS += $(MORK_BRIDGE_BUILD_STAMP) $(MORK_BRIDGE_FEATURE_STATICLIB)
BRIDGE_CFLAGS += -DCETTA_MORK_BRIDGE_STATIC=1
BRIDGE_LDFLAGS += $(MORK_BRIDGE_FEATURE_STATICLIB) -lrt
MORK_BUILD_HAS_BRIDGE := 1
endif

$(MORK_BRIDGE_WORKSPACE_MANIFEST): $(MORK_BRIDGE_SOURCE_DEPS) Makefile
	@mkdir -p "$(MORK_BRIDGE_WORKSPACE_DIR)"
	@set -e; \
	for member in $(MORK_BRIDGE_WORKSPACE_MEMBERS); do \
		rm -rf "$(MORK_BRIDGE_WORKSPACE_DIR)/$$member.tmp"; \
		cp -R --no-preserve=all "$(CETTA_RUST_DIR)/$$member" "$(MORK_BRIDGE_WORKSPACE_DIR)/$$member.tmp"; \
		rm -rf "$(MORK_BRIDGE_WORKSPACE_DIR)/$$member"; \
		mv "$(MORK_BRIDGE_WORKSPACE_DIR)/$$member.tmp" "$(MORK_BRIDGE_WORKSPACE_DIR)/$$member"; \
	done; \
	tmp="$@.tmp"; \
	sed \
		-e "s|path = \"../../PathMap\"|path = \"$(PATHMAP_DEP_DIR)\"|" \
		-e "s|path = \"../../MORK/kernel\"|path = \"$(MORK_KERNEL_DIR)\"|" \
		-e "s|path = \"../../MORK/expr\"|path = \"$(MORK_EXPR_DIR)\"|" \
		-e "s|path = \"../../MORK/frontend\"|path = \"$(MORK_FRONTEND_DIR)\"|" \
		-e "s|path = \"../../MORK/interning\"|path = \"$(MORK_INTERNING_DIR)\"|" \
		"$(CETTA_RUST_DIR)/Cargo.toml" > "$$tmp"; \
	if [ -f "$@" ] && cmp -s "$$tmp" "$@"; then \
		rm -f "$$tmp"; \
	else \
		mv "$$tmp" "$@"; \
	fi

$(MORK_BRIDGE_BUILD_STAMP): $(MORK_BRIDGE_SOURCE_DEPS) $(MORK_BRIDGE_WORKSPACE_MANIFEST)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@cd $(MORK_BRIDGE_WORKDIR) && \
		MAKEFLAGS= \
		CARGO_TARGET_DIR='$(CETTA_RUST_DIR)/target' \
		RUSTFLAGS='$(MORK_BRIDGE_RUSTFLAGS)' \
		$(MORK_BRIDGE_CARGO) build --manifest-path "$(MORK_BRIDGE_WORKSPACE_MANIFEST)" -p cetta-space-bridge --release $(MORK_BRIDGE_CARGO_FEATURE_ARGS)
	@test -f "$(MORK_BRIDGE_STATICLIB)"
	@touch "$@"

$(MORK_BRIDGE_FEATURE_STATICLIB): $(MORK_BRIDGE_BUILD_STAMP)
	@mkdir -p $(dir $@)
	@cp "$(MORK_BRIDGE_STATICLIB)" "$@"

PY_CFLAGS =
PY_LDFLAGS =
PY_RPATH =
PYTHON_SRC = src/foreign_stub.c
LIB_PROLOG_PKG_AVAILABLE := $(shell pkg-config --exists swipl 2>/dev/null && printf '%s' 1 || printf '%s' 0)
LIB_PROLOG_ENABLED := 0
ifeq ($(ENABLE_LIB_PROLOG),auto)
LIB_PROLOG_ENABLED := $(LIB_PROLOG_PKG_AVAILABLE)
else
LIB_PROLOG_ENABLED := $(ENABLE_LIB_PROLOG)
endif
ifeq ($(LIB_PROLOG_ENABLED),1)
ifneq ($(LIB_PROLOG_PKG_AVAILABLE),1)
$(error ENABLE_LIB_PROLOG=1 requires the swipl pkg-config package)
endif
LIB_PROLOG_CFLAGS := $(shell pkg-config --cflags swipl)
LIB_PROLOG_LDFLAGS := $(shell pkg-config --libs swipl)
LIB_PROLOG_RPATH := -Wl,-rpath,$(shell pkg-config --variable=libdir swipl)
LIB_PROLOG_SRC := src/petta_libpl.c
else
LIB_PROLOG_CFLAGS :=
LIB_PROLOG_LDFLAGS :=
LIB_PROLOG_RPATH :=
LIB_PROLOG_SRC := src/petta_libpl_stub.c
endif
ifeq ($(ENABLE_GMP),1)
GMP_CFLAGS ?= $(shell pkg-config --cflags gmp 2>/dev/null)
GMP_LDFLAGS ?= $(shell pkg-config --libs gmp 2>/dev/null || printf '%s' -lgmp)
else
GMP_CFLAGS =
GMP_LDFLAGS =
endif
ifeq ($(ENABLE_PYTHON),1)
PYTHON_CONFIG := $(strip $(shell command -v python3-config 2>/dev/null))
ifeq ($(PYTHON_CONFIG),)
$(error BUILD=$(BUILD_CANON) requires python3-config)
endif
PY_CFLAGS = $(shell python3-config --includes)
PY_LDFLAGS = $(shell python3-config --embed --ldflags)
PY_RPATH = -Wl,-rpath,$(shell python3 -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or "")')
PYTHON_SRC = src/foreign.c
endif
empty :=
space := $(empty) $(empty)
comma := ,
BUILD_OBJ_TAG = $(BUILD_CANON)
ifeq ($(ENABLE_GMP),0)
BUILD_OBJ_TAG := $(BUILD_CANON).nogmp
endif
ifeq ($(ENABLE_SANITIZERS),1)
SANITIZER_TAG := $(subst $(comma),-,$(subst $(space),_,$(SANITIZERS)))
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).sanitize.$(SANITIZER_TAG)
endif
ifeq ($(ENABLE_PIC),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).pic
endif
ifeq ($(CETTA_PROVENANCE_ASSERT),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).provenance
endif
ifeq ($(RHOCOST_COMMIT_AUDIT),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).rhocost-audit
endif
ifeq ($(ENABLE_PRIME_RECEIPT_PRIMARY_INDEX),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).prime-receipt-primary-index
endif
ifeq ($(ENABLE_PRIME_NEED_HEAP_INDEX),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).prime-need-heap-index
endif
ifeq ($(ENABLE_PRIME_NEED_CLOSURE_CAPTURE),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).prime-need-closure-capture
endif
ifeq ($(ENABLE_PRIME_EVAL_STACK),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).prime-eval-stack
endif
ifeq ($(LIB_PROLOG_ENABLED),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).lib-prolog
endif
ifeq ($(ENABLE_PETTA_TYPECHECK_V2),0)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).no-petta-typecheck-v2
endif
ifeq ($(ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS),1)
BUILD_OBJ_TAG := $(BUILD_OBJ_TAG).langdef-diagnostics
endif
SANITIZER_WORDS := $(subst $(comma), ,$(SANITIZERS))

# Test and probe invocations must not share the public ./cetta output.  A
# recursive bridge/configuration test may select a different BUILD, and other
# developers may build another mode concurrently.  Keep those executables
# configuration-addressed so the gate always runs the mode it requested and
# Make can reuse an unchanged link product.
CETTA_TEST_ISOLATED ?= 0
ifneq ($(strip $(filter test% probe%,$(MAKECMDGOALS))),)
CETTA_TEST_ISOLATED := 1
endif
export CETTA_TEST_ISOLATED

# A public binary and an isolated test binary use different output-addressing
# policies.  Requesting both in one make process would make the public path an
# unrelated existing file with no active prerequisites, so reject that stale-
# evidence shape and require two independently incremental invocations.
ifneq ($(strip $(filter test% probe%,$(MAKECMDGOALS))),)
ifneq ($(strip $(filter cetta runtime/cetta-$(BUILD_CANON)-runtime-stats,$(MAKECMDGOALS))),)
$(error Public binary and test/probe goals must be built in separate make invocations)
endif
endif

GSLT2PARSE_SHARED_ASAN_ENV =
GSLT2PARSE_SHARED_ASAN_ARGS =
ifeq ($(ENABLE_SANITIZERS),1)
ifneq ($(filter address,$(SANITIZER_WORDS)),)
GSLT2PARSE_ASAN_RUNTIME := $(shell $(CC) -print-file-name=libasan.so)
GSLT2PARSE_SHARED_ASAN_ENV = LD_PRELOAD="$(GSLT2PARSE_ASAN_RUNTIME)"
GSLT2PARSE_SHARED_ASAN_ARGS = --isolate-oracle-environment
endif
endif
TSAN_ENABLED := 0
ifeq ($(ENABLE_SANITIZERS),1)
ifneq ($(filter thread,$(SANITIZER_WORDS)),)
TSAN_ENABLED := 1
endif
endif
ifeq ($(ENABLE_RUNTIME_STATS),1)
BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_OBJ_TAG).runtime-stats.h
BUILD_CONFIG_STAMP = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_OBJ_TAG).runtime-stats.stamp
else
BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_OBJ_TAG).h
BUILD_CONFIG_STAMP = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_OBJ_TAG).stamp
endif
STAGE0_BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.stage0.$(BUILD_OBJ_TAG).h
STAGE0_BUILD_CONFIG_STAMP = $(BOOTSTRAP_TMPDIR)/build_config.stage0.$(BUILD_OBJ_TAG).stamp
VERSION_FILE = VERSION
CETTA_VERSION := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null))
PROVENANCE_CPPFLAGS =
ifeq ($(CETTA_PROVENANCE_ASSERT),1)
PROVENANCE_CPPFLAGS = -DCETTA_PROVENANCE_ASSERT=1
endif
PRIME_RECEIPT_INDEX_CPPFLAGS =
ifeq ($(ENABLE_PRIME_RECEIPT_PRIMARY_INDEX),1)
PRIME_RECEIPT_INDEX_CPPFLAGS = -DCETTA_PRIME_RECEIPT_PRIMARY_INDEX=1
endif
PRIME_NEED_HEAP_INDEX_CPPFLAGS =
ifeq ($(ENABLE_PRIME_NEED_HEAP_INDEX),1)
PRIME_NEED_HEAP_INDEX_CPPFLAGS = -DCETTA_PRIME_NEED_HEAP_INDEX=1
endif
PRIME_NEED_CLOSURE_CAPTURE_CPPFLAGS =
ifeq ($(ENABLE_PRIME_NEED_CLOSURE_CAPTURE),1)
PRIME_NEED_CLOSURE_CAPTURE_CPPFLAGS = -DCETTA_PRIME_NEED_CLOSURE_CAPTURE=1
endif
PRIME_EVAL_STACK_CPPFLAGS =
ifeq ($(ENABLE_PRIME_EVAL_STACK),1)
PRIME_EVAL_STACK_CPPFLAGS = -DCETTA_PRIME_EVAL_STACK=1
endif
CPPFLAGS = -Isrc -I. -Iexperiments/gslt2parse_foundation/native $(BRIDGE_CFLAGS) $(PY_CFLAGS) $(GMP_CFLAGS) $(LIB_PROLOG_CFLAGS) $(PROVENANCE_CPPFLAGS) $(PRIME_RECEIPT_INDEX_CPPFLAGS) $(PRIME_NEED_HEAP_INDEX_CPPFLAGS) $(PRIME_NEED_CLOSURE_CAPTURE_CPPFLAGS) $(PRIME_EVAL_STACK_CPPFLAGS) -include $(BUILD_CONFIG_HEADER)
CFLAGS = -O3 -Wall -Werror -std=c11 -pthread
DEPFLAGS = -MMD -MP
LDFLAGS = $(BRIDGE_LDFLAGS) -ldl -lm -pthread $(GMP_LDFLAGS) $(LIB_PROLOG_LDFLAGS) $(LIB_PROLOG_RPATH) $(PY_LDFLAGS) $(PY_RPATH)
ifeq ($(ENABLE_SANITIZERS),1)
CFLAGS := -O1 -g -fno-omit-frame-pointer -fsanitize=$(SANITIZERS) -fno-sanitize-recover=all -Wall -Werror -std=c11 -pthread
LDFLAGS += -fsanitize=$(SANITIZERS) -fno-sanitize-recover=all
endif
ifeq ($(ENABLE_PIC),1)
CFLAGS += -fPIC
endif

HE_COMPILED_READER_RUNTIME_SRC = \
	src/he_compiled_reader.c \
	src/gslt_direct_reader_v1.c \
	src/generated/he_reader_direct_v1.generated.c
PETTA_COMPILED_READER_RUNTIME_SRC = \
	src/petta_compiled_reader.c \
	src/generated/petta_reader_direct_v1.generated.c
PRIME_COMPILED_READER_RUNTIME_SRC = \
	src/prime_compiled_reader.c \
	src/generated/prime_reader_direct_v1.generated.c
COMPILED_READER_RUNTIME_SRC = \
	$(HE_COMPILED_READER_RUNTIME_SRC) \
	$(PETTA_COMPILED_READER_RUNTIME_SRC) \
	$(PRIME_COMPILED_READER_RUNTIME_SRC)
PETTA_TYPECHECK_V2_SRC =
ifeq ($(ENABLE_PETTA_TYPECHECK_V2),1)
PETTA_TYPECHECK_V2_SRC = src/petta_typecheck.c \
	src/generated/petta_typecheck_v2_source_binding_v1.generated.c
endif
SRC = src/symbol.c src/atom.c src/name_key.c src/atom_blob.c src/abt.c src/parser.c $(COMPILED_READER_RUNTIME_SRC) src/mm2_lower.c src/subst_tree.c src/space.c src/registry_resolver.c src/space_match_backend.c src/match.c src/match_decision.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/answer_bank.c src/table_store.c src/search_machine.c src/petta_program.c src/petta_search_machine.c $(PETTA_TYPECHECK_V2_SRC) src/petta_specializer.c src/rule_machine.c $(LIB_PROLOG_SRC) src/term_universe.c src/stats.c src/parallel_executor.c src/prime_need.c src/petta_semantics.c src/prepared_pure_machine.c src/eval.c src/grounded.c src/he_typing.c src/he_typing_authority.c src/generated/he_typing_consistency_core_source_binding_v1.generated.c src/inference_checker.c src/nik_direct_authority.c src/nik_runtime.c src/prime_semantics.c src/generated/prime_typing_closed_formation_source_binding_v1.generated.c src/text_source.c src/native_handle.c src/native_sha256.c src/mork_space_bridge_runtime.c src/library.c src/langdef_pack.c src/gslt_provider_runtime.c src/gslt_space_fact_provider_v1.c src/gslt_finite_fact_provider_v1.c src/gslt_revisioned_space_provider_v1.c src/gslt_abt_provider_v1.c src/gslt_horn_runtime.c src/gslt_dense_bitset_v1.c src/gslt_compiled_runtime.c src/gslt_indexed_instruction_decoder_v1.c src/gslt_indexed_value_table_v1.c src/gslt_split_indexed_table_v1.c src/gslt_literal_hole_program_v1.c src/gslt_u32_index_v1.c src/gslt_u32_slice_arena_v1.c src/gslt_epoch_slots_v1.c src/gslt_ground_dense_term_v1.c src/gslt_language_runtime.c src/gslt_pure_provider_v1.c src/gslt_support_transform_runtime.c src/generated/prime_nik_authorities_v1.generated.c src/generated/prime_nik_runtime_v1.generated.c src/generated/gslt_il_language_v1.generated.c src/generated/metta_interact_language_v1.generated.c src/generated/mm2_gslt_profile_v1.generated.c src/generated/subzero_language_v1.generated.c src/generated/zero_language_v1.generated.c src/generated/zero_exp_language_v1.generated.c src/generated/zero_emit_language_v1.generated.c src/generated/zero_interact_language_v1.generated.c src/generated/zero_interact_provider_catalog_v1.generated.c src/generated/zerouv_language_v1.generated.c src/he_small_step_pack.c src/lib_parse_native_grammar.c src/lib_parse_inference_native.c experiments/gslt2parse_foundation/native/finite_horn_gslt_v1.c experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.c experiments/gslt2parse_foundation/native/parser_term_projection_v1.c experiments/gslt2parse_foundation/native/parser_pack_abi_v1.c experiments/gslt2parse_foundation/native/parser_action_bytecode_v1.c experiments/gslt2parse_foundation/native/parser_pack_native_v1.c experiments/gslt2parse_foundation/native/parser_pack_lexical_v1.c experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c experiments/gslt2parse_foundation/native/regular_span_dfa_v1.c experiments/gslt2parse_foundation/native/regular_span_nfa_v1.c $(PYTHON_SRC) src/session.c src/lang.c src/rhocalc_core.c src/rhocalc_syntax.c src/compile.c src/runtime.c src/cetta_stdlib.c native/native_modules.c src/main.c
SRC += src/inference_side_condition_provider.c \
	src/generated/prime_nik_side_condition_provider_catalog_v1.generated.c \
	experiments/gslt2parse_foundation/native/parser_pack_glr_v1.c
LANGDEF_COMPILED_CURSOR_RUNTIME_SRC = \
	experiments/gslt2parse_foundation/native/finite_horn_answer_stream_v1.c \
	experiments/gslt2parse_foundation/native/parser_pack_guard_evidence_stream_v1.c \
	experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.c \
	experiments/gslt2parse_foundation/native/parser_pack_guard_plan_v1.c \
	experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_v1.c \
	experiments/gslt2parse_foundation/native/parser_pack_guard_ref_v1.c \
	experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_exec_v1.c \
	experiments/gslt2parse_foundation/native/parser_occurrence_fold_v1.c \
	experiments/gslt2parse_foundation/native/parser_occurrence_span_mask_v1.c \
	experiments/gslt2parse_foundation/native/parser_occurrence_file_resolver_v1.c \
	experiments/gslt2parse_foundation/native/relational_value_list_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_article_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_plan_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_sequence_evidence_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_relational_assertion_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_relational_declaration_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_relational_machine_v1.c \
	experiments/gslt2parse_foundation/native/oslf_native_type_plan_v1.c \
	experiments/gslt2parse_foundation/native/oslf_native_type_vm_v1.c \
	experiments/gslt2parse_foundation/native/proof_gslt_relational_runtime_v1.c \
	experiments/gslt2parse_foundation/native/proof_storage_plan_v1.c \
	experiments/gslt2parse_foundation/native/relational_stack_proof_v1.c \
	experiments/gslt2parse_foundation/native/relational_state_program_v1.c \
	experiments/gslt2parse_foundation/native/parser_atom_projection_v1.c \
	experiments/gslt2parse_foundation/native/parser_atom_projection_events_v1.c \
	experiments/gslt2parse_foundation/native/parser_atom_projection_action_v1.c \
	experiments/gslt2parse_foundation/native/semantic_mask_nfa_v1.c
ifeq ($(ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS),1)
LANGDEF_COMPILED_CURSOR_RUNTIME_SRC += \
	experiments/gslt2parse_foundation/native/first_order_frame_decoder_v1.c
endif
SRC += experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.c \
	$(LANGDEF_COMPILED_CURSOR_RUNTIME_SRC) native/langdef_module.c
ifeq ($(ENABLE_RUNTIME_STATS),1)
OBJ = $(SRC:.c=.$(BUILD_OBJ_TAG).runtime-stats.o)
ifeq ($(CETTA_TEST_ISOLATED),1)
BIN = runtime/cetta-$(BUILD_OBJ_TAG)-runtime-stats
FALLBACK_EVAL_TEST_BIN = runtime/test_fallback_eval_session-$(BUILD_OBJ_TAG)-runtime-stats
else
BIN = runtime/cetta-$(BUILD_CANON)-runtime-stats
FALLBACK_EVAL_TEST_BIN = runtime/test_fallback_eval_session-$(BUILD_CANON)-runtime-stats
endif
FALLBACK_EVAL_TEST_OBJ = runtime/bootstrap/test_fallback_eval_session.$(BUILD_OBJ_TAG).runtime-stats.o
BIN_FORCE =
else
OBJ = $(SRC:.c=.$(BUILD_OBJ_TAG).o)
FALLBACK_EVAL_TEST_OBJ = runtime/bootstrap/test_fallback_eval_session.$(BUILD_OBJ_TAG).o
FALLBACK_EVAL_TEST_BIN = runtime/test_fallback_eval_session-$(BUILD_CANON)
ifeq ($(CETTA_TEST_ISOLATED),1)
BIN = runtime/cetta-$(BUILD_OBJ_TAG)
BIN_FORCE =
else
BIN = cetta
BIN_FORCE = FORCE
endif
endif
COMPILED_READER_RUNTIME_OBJ = $(patsubst %.c,%.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o,$(COMPILED_READER_RUNTIME_SRC))
FALLBACK_EVAL_TEST_SRC = tests/support/test_fallback_eval_session.c
FALLBACK_EVAL_TEST_LINK_OBJ = $(filter-out src/main.$(BUILD_OBJ_TAG).runtime-stats.o src/main.$(BUILD_OBJ_TAG).o $(COMPILED_READER_RUNTIME_OBJ),$(OBJ))
HE_COMPILED_READER_TEST_SRC = tests/support/test_he_compiled_reader_v1.c
HE_COMPILED_READER_TEST_OBJ = runtime/bootstrap/test_he_compiled_reader_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
HE_COMPILED_READER_TEST_BIN = runtime/test_he_compiled_reader_v1-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
HE_COMPILED_READER_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(COMPILED_READER_RUNTIME_OBJ)
HE_TYPING_DIRECT_TEST_SRC = tests/support/test_he_typing_direct_authority.c
HE_TYPING_DIRECT_TEST_OBJ = runtime/bootstrap/test_he_typing_direct_authority.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
HE_TYPING_DIRECT_TEST_BIN = runtime/test_he_typing_direct_authority-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
HE_TYPING_DIRECT_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PETTA_COMPILED_READER_TEST_SRC = tests/support/test_petta_compiled_reader_v1.c
PETTA_COMPILED_READER_TEST_OBJ = runtime/bootstrap/test_petta_compiled_reader_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PETTA_COMPILED_READER_TEST_BIN = runtime/test_petta_compiled_reader_v1-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
PETTA_COMPILED_READER_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(COMPILED_READER_RUNTIME_OBJ)
PETTA_SEARCH_MACHINE_TEST_SRC = tests/support/test_petta_search_machine.c
PETTA_SEARCH_MACHINE_TEST_OBJ = runtime/bootstrap/test_petta_search_machine.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PETTA_SEARCH_MACHINE_TEST_BIN = runtime/test_petta_search_machine-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
PETTA_SEARCH_MACHINE_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
MATCH_DECISION_TEST_SRC = tests/support/test_match_decision.c
MATCH_DECISION_TEST_OBJ = runtime/bootstrap/test_match_decision.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
MATCH_DECISION_TEST_BIN = runtime/test_match_decision-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
MATCH_DECISION_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PETTA_SPECIALIZER_PREPARE_TEST_SRC = tests/test_petta_specializer_prepare.c
PETTA_SPECIALIZER_PREPARE_TEST_OBJ = runtime/bootstrap/test_petta_specializer_prepare.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PETTA_SPECIALIZER_PREPARE_TEST_BIN = runtime/test_petta_specializer_prepare-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
PETTA_SPECIALIZER_PREPARE_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PRIME_COMPILED_READER_TEST_SRC = tests/support/test_prime_compiled_reader_v1.c
PRIME_COMPILED_READER_TEST_OBJ = runtime/bootstrap/test_prime_compiled_reader_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PRIME_COMPILED_READER_TEST_BIN = runtime/test_prime_compiled_reader_v1-$(BUILD_CANON)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
PRIME_COMPILED_READER_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(COMPILED_READER_RUNTIME_OBJ)
HE_COMPILED_READER_BENCH_SRC = tests/bench_he_compiled_reader_v1.c
HE_COMPILED_READER_BENCH_OBJ = runtime/bootstrap/bench_he_compiled_reader_v1.$(BUILD_OBJ_TAG).o
HE_COMPILED_READER_BENCH_BIN = runtime/bench_he_compiled_reader_v1-$(BUILD_OBJ_TAG)
HE_COMPILED_READER_BENCH_LINK_OBJ = $(HE_COMPILED_READER_TEST_LINK_OBJ)
HE_COMPILED_READER_BENCH_INPUT ?= lib/stdlib.metta
HE_COMPILED_READER_BENCH_ITERATIONS ?= 101
PRIME_DELAYED_AMBIGUITY_TEST_SRC = tests/support/test_prime_delayed_ambiguity.c
PRIME_DELAYED_AMBIGUITY_TEST_OBJ = runtime/bootstrap/test_prime_delayed_ambiguity.$(BUILD_OBJ_TAG).o
PRIME_DELAYED_AMBIGUITY_TEST_BIN = runtime/test_prime_delayed_ambiguity-$(BUILD_CANON)
PRIME_DELAYED_AMBIGUITY_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PRIME_PACKAGE_VALIDATION_TEST_SRC = tests/support/test_prime_package_validation.c
PRIME_PACKAGE_VALIDATION_TEST_OBJ = runtime/bootstrap/test_prime_package_validation.$(BUILD_OBJ_TAG).o
PRIME_PACKAGE_VALIDATION_TEST_BIN = runtime/test_prime_package_validation-$(BUILD_CANON)
PRIME_PACKAGE_VALIDATION_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
RUNTIME_NAMED_VAR_TEST_SRC = tests/support/test_runtime_named_var.c
RUNTIME_NAMED_VAR_TEST_OBJ = runtime/bootstrap/test_runtime_named_var.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
RUNTIME_NAMED_VAR_TEST_BIN = runtime/test_runtime_named_var-$(BUILD_CANON)
RUNTIME_NAMED_VAR_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PRIME_BARE_DOLLAR_PARSER_TEST_SRC = tests/support/test_prime_bare_dollar_parser.c
PRIME_BARE_DOLLAR_PARSER_TEST_OBJ = runtime/bootstrap/test_prime_bare_dollar_parser.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PRIME_BARE_DOLLAR_PARSER_TEST_BIN = runtime/test_prime_bare_dollar_parser-$(BUILD_CANON)
PRIME_BARE_DOLLAR_PARSER_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PRIME_BARE_DOLLAR_EVAL_DIR = runtime/prime-bare-dollar-evaluator-$(BUILD_CANON)
PRIME_BARE_DOLLAR_LITERAL_PARSER_OBJ = $(PRIME_BARE_DOLLAR_EVAL_DIR)/parser-literal.o
PRIME_BARE_DOLLAR_SHARED_PARSER_OBJ = $(PRIME_BARE_DOLLAR_EVAL_DIR)/parser-shared.o
PRIME_BARE_DOLLAR_LITERAL_BIN = $(PRIME_BARE_DOLLAR_EVAL_DIR)/cetta-literal
PRIME_BARE_DOLLAR_SHARED_BIN = $(PRIME_BARE_DOLLAR_EVAL_DIR)/cetta-shared
PRIME_READER_AST_ORACLE_SRC = tests/support/prime_reader_ast_oracle.c
PRIME_READER_AST_ORACLE_OBJ = runtime/bootstrap/prime_reader_ast_oracle.$(BUILD_OBJ_TAG).o
PRIME_READER_AST_ORACLE_BIN = runtime/prime_reader_ast_oracle-$(BUILD_CANON)
PRIME_READER_AST_ORACLE_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PRIME_SYNTAX_GSLT_DIR = .generated/prime-universal-name
PRIME_SYNTAX_GSLT_ENGINE = $(PRIME_SYNTAX_GSLT_DIR)/gslt2parse
PRIME_SYNTAX_GSLT_PRESENTATION = $(PRIME_SYNTAX_GSLT_DIR)/prime_syntax_gslt.metta
PRIME_SYNTAX_GSLT_MUTANT = $(PRIME_SYNTAX_GSLT_DIR)/prime_syntax_gslt_no_universal.metta
PRIME_SYNTAX_GSLT_DOLLAR_SYMBOL = $(PRIME_SYNTAX_GSLT_DIR)/prime_syntax_gslt_bare_dollar_symbol.metta
PAYLOAD_MAP_CAPACITY_TEST_SRC = tests/support/test_rhometta_payload_map_capacity.c
PAYLOAD_MAP_CAPACITY_TEST_OBJ = runtime/bootstrap/test_rhometta_payload_map_capacity.$(BUILD_OBJ_TAG).o
PAYLOAD_MAP_CAPACITY_TEST_BIN = runtime/test_rhometta_payload_map_capacity-$(BUILD_CANON)
PAYLOAD_MAP_CAPACITY_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
RHOCALC_ABT_SUBSTITUTION_TEST_SRC = tests/support/test_rhocalc_abt_substitution.c
RHOCALC_ABT_SUBSTITUTION_TEST_OBJ = runtime/bootstrap/test_rhocalc_abt_substitution.$(BUILD_OBJ_TAG).o
RHOCALC_ABT_SUBSTITUTION_TEST_BIN = runtime/test_rhocalc_abt_substitution-$(BUILD_CANON)
RHOCALC_ABT_SUBSTITUTION_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
LIB_PARSE_GLL_UTF8_FOREST_TEST_SRC = tests/support/test_lib_parse_gll_utf8_forest.c
LIB_PARSE_GLL_UTF8_FOREST_TEST_OBJ = runtime/bootstrap/test_lib_parse_gll_utf8_forest.$(BUILD_OBJ_TAG).o
LIB_PARSE_GLL_UTF8_FOREST_TEST_BIN = runtime/test_lib_parse_gll_utf8_forest-$(BUILD_OBJ_TAG)
LIB_PARSE_GLL_UTF8_FOREST_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
LIB_PARSE_GLR_UTF8_FOREST_TEST_SRC = tests/support/test_lib_parse_glr_utf8_forest.c
LIB_PARSE_GLR_UTF8_FOREST_TEST_OBJ = runtime/bootstrap/test_lib_parse_glr_utf8_forest.$(BUILD_OBJ_TAG).o
LIB_PARSE_GLR_UTF8_FOREST_TEST_BIN = runtime/test_lib_parse_glr_utf8_forest-$(BUILD_OBJ_TAG)
LIB_PARSE_GLR_UTF8_FOREST_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
LIB_PARSE_SLR_PREPARED_TEST_SRC = tests/support/test_lib_parse_slr_prepared.c
LIB_PARSE_SLR_PREPARED_TEST_OBJ = runtime/bootstrap/test_lib_parse_slr_prepared.$(BUILD_OBJ_TAG).o
LIB_PARSE_SLR_PREPARED_TEST_BIN = runtime/test_lib_parse_slr_prepared-$(BUILD_OBJ_TAG)
LIB_PARSE_SLR_PREPARED_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_GLL_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_parser_pack_gll_v1.c
PARSER_PACK_GLL_V1_TEST_OBJ = runtime/bootstrap/test_parser_pack_gll_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_GLL_V1_TEST_BIN = runtime/test_parser_pack_gll_v1-$(BUILD_OBJ_TAG)
PARSER_PACK_GLL_V1_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_GLR_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_parser_pack_glr_v1.c
PARSER_PACK_GLR_V1_TEST_OBJ = runtime/bootstrap/test_parser_pack_glr_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_GLR_V1_TEST_BIN = runtime/test_parser_pack_glr_v1-$(BUILD_OBJ_TAG)
PARSER_PACK_GLR_V1_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_LEXICAL_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_parser_pack_lexical_v1.c
PARSER_PACK_LEXICAL_V1_TEST_OBJ = runtime/bootstrap/test_parser_pack_lexical_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_LEXICAL_V1_TEST_BIN = runtime/test_parser_pack_lexical_v1-$(BUILD_OBJ_TAG)
PARSER_PACK_GUARD_RELATION_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.c
PARSER_PACK_GUARD_RELATION_V1_OBJ = experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_LEXICAL_V1_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ)
PARSER_PACK_LEXICAL_PLAN_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_lexical_plan_v1_stream.c
PARSER_PACK_LEXICAL_PLAN_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_lexical_plan_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_LEXICAL_PLAN_V1_STREAM_BIN = runtime/parser_pack_lexical_plan_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_LEXICAL_PLAN_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_GUARD_PLAN_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_plan_v1.c
PARSER_PACK_GUARD_PLAN_V1_OBJ = experiments/gslt2parse_foundation/native/parser_pack_guard_plan_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_GUARD_PLAN_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_parser_pack_guard_plan_v1.c
PARSER_PACK_GUARD_PLAN_V1_TEST_OBJ = runtime/bootstrap/test_parser_pack_guard_plan_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_GUARD_PLAN_V1_TEST_BIN = runtime/test_parser_pack_guard_plan_v1-$(BUILD_OBJ_TAG)
PARSER_PACK_GUARD_PLAN_V1_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ)
PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_evidence_stream_v1.c
PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ = experiments/gslt2parse_foundation/native/parser_pack_guard_evidence_stream_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_GUARD_PLAN_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_plan_v1_stream.c
PARSER_PACK_GUARD_PLAN_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_guard_plan_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN = runtime/parser_pack_guard_plan_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_GUARD_PLAN_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ)
PARSER_PACK_GUARDED_LEXICAL_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_v1.c
PARSER_PACK_GUARDED_LEXICAL_V1_OBJ = experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_plan_v1_stream.c
PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_guarded_lexical_plan_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN = runtime/parser_pack_guarded_lexical_plan_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ)
PARSER_PACK_GUARD_REF_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_ref_v1.c
PARSER_PACK_GUARD_REF_V1_OBJ = experiments/gslt2parse_foundation/native/parser_pack_guard_ref_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_GUARD_REF_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_ref_v1_stream.c
PARSER_PACK_GUARD_REF_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_guard_ref_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_GUARD_REF_V1_STREAM_BIN = runtime/parser_pack_guard_ref_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_GUARD_REF_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ) $(PARSER_PACK_GUARD_REF_V1_OBJ)
PARSER_PACK_GUARD_SCALAR_EXEC_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guard_scalar_exec_v1.c
PARSER_PACK_GUARD_SCALAR_EXEC_V1_OBJ = runtime/bootstrap/parser_pack_guard_scalar_exec_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_exec_v1.c
PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ = experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_exec_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_CURSOR_C_EMITTER_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_cursor_c_emitter_v1.c
PARSER_PACK_CURSOR_C_EMITTER_V1_OBJ = runtime/bootstrap/parser_pack_cursor_c_emitter_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_CURSOR_GENERATED_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_parser_pack_cursor_generated_v1.c
PARSER_OCCURRENCE_FOLD_V1_SRC = experiments/gslt2parse_foundation/native/parser_occurrence_fold_v1.c
PARSER_OCCURRENCE_FOLD_V1_HEADER = experiments/gslt2parse_foundation/native/parser_occurrence_fold_v1.h
PARSER_OCCURRENCE_FOLD_V1_OBJ = experiments/gslt2parse_foundation/native/parser_occurrence_fold_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_OCCURRENCE_SPAN_MASK_V1_SRC = experiments/gslt2parse_foundation/native/parser_occurrence_span_mask_v1.c
PARSER_OCCURRENCE_SPAN_MASK_V1_HEADER = experiments/gslt2parse_foundation/native/parser_occurrence_span_mask_v1.h
PARSER_OCCURRENCE_SPAN_MASK_V1_OBJ = experiments/gslt2parse_foundation/native/parser_occurrence_span_mask_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
RELATIONAL_VALUE_LIST_V1_SRC = experiments/gslt2parse_foundation/native/relational_value_list_v1.c
RELATIONAL_VALUE_LIST_V1_HEADER = experiments/gslt2parse_foundation/native/relational_value_list_v1.h
RELATIONAL_VALUE_LIST_V1_OBJ = experiments/gslt2parse_foundation/native/relational_value_list_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_ARTICLE_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_article_v1.c
PROOF_GSLT_ARTICLE_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_article_v1.h
PROOF_GSLT_ARTICLE_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_article_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_ARTICLE_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_article_v1.c
PROOF_GSLT_ARTICLE_V1_TEST_BIN = runtime/test_proof_gslt_article_v1-$(BUILD_OBJ_TAG)
PROOF_GSLT_PLAN_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_plan_v1.c
PROOF_GSLT_PLAN_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_plan_v1.h
PROOF_GSLT_PLAN_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_plan_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_PLAN_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_plan_v1.c
PROOF_GSLT_PLAN_V1_TEST_OBJ = runtime/bootstrap/test_proof_gslt_plan_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_PLAN_V1_TEST_BIN = runtime/test_proof_gslt_plan_v1-$(BUILD_OBJ_TAG)
PROOF_GSLT_PLAN_V1_TEST_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(PROOF_GSLT_ARTICLE_V1_OBJ) \
	$(PROOF_GSLT_PLAN_V1_OBJ)
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_sequence_evidence_v1.c
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_sequence_evidence_v1.h
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_sequence_evidence_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_sequence_evidence_v1.c
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_OBJ = runtime/bootstrap/test_proof_gslt_sequence_evidence_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_BIN = runtime/test_proof_gslt_sequence_evidence_v1-$(BUILD_OBJ_TAG)
PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_LINK_OBJ = \
	$(PROOF_GSLT_PLAN_V1_TEST_LINK_OBJ) \
	$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_OBJ)
PROOF_GSLT_RELATIONAL_ASSERTION_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_relational_assertion_v1.c
PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_relational_assertion_v1.h
PROOF_GSLT_RELATIONAL_ASSERTION_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_relational_assertion_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_relational_assertion_v1.c
PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_OBJ = runtime/bootstrap/test_proof_gslt_relational_assertion_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_BIN = runtime/test_proof_gslt_relational_assertion_v1-$(BUILD_OBJ_TAG)
PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_LINK_OBJ = \
	$(PROOF_GSLT_PLAN_V1_TEST_LINK_OBJ) \
	$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_OBJ)
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_relational_projection_v1.c
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_relational_projection_v1.h
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_relational_projection_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_relational_projection_v1.c
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_OBJ = runtime/bootstrap/test_proof_gslt_relational_projection_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_BIN = runtime/test_proof_gslt_relational_projection_v1-$(BUILD_OBJ_TAG)
PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_LINK_OBJ = \
	$(PROOF_GSLT_PLAN_V1_TEST_LINK_OBJ) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_OBJ)
PROOF_GSLT_RELATIONAL_DECLARATION_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_relational_declaration_v1.c
PROOF_GSLT_RELATIONAL_DECLARATION_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_relational_declaration_v1.h
PROOF_GSLT_RELATIONAL_DECLARATION_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_relational_declaration_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_MACHINE_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_relational_machine_v1.c
PROOF_GSLT_RELATIONAL_MACHINE_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_relational_machine_v1.h
PROOF_GSLT_RELATIONAL_MACHINE_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_relational_machine_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_relational_declaration_v1.c
PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_OBJ = runtime/bootstrap/test_proof_gslt_relational_declaration_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_BIN = runtime/test_proof_gslt_relational_declaration_v1-$(BUILD_OBJ_TAG)
PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_LINK_OBJ = \
	$(PROOF_GSLT_PLAN_V1_TEST_LINK_OBJ) \
	$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_OBJ) \
	$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_OBJ) \
	$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_OBJ) \
	$(PROOF_GSLT_RELATIONAL_MACHINE_V1_OBJ) \
	$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ) \
	$(GSLT_SPLIT_INDEXED_TABLE_V1_OBJ) \
	$(GSLT_U32_INDEX_V1_OBJ) \
	$(RELATIONAL_VALUE_LIST_V1_OBJ)
RELATIONAL_STACK_PROOF_V1_SRC = experiments/gslt2parse_foundation/native/relational_stack_proof_v1.c
RELATIONAL_STACK_PROOF_V1_HEADER = experiments/gslt2parse_foundation/native/relational_stack_proof_v1.h
RELATIONAL_STORE_V1_HEADER = experiments/gslt2parse_foundation/native/relational_store_v1.h
RELATIONAL_STACK_PROOF_V1_OBJ = experiments/gslt2parse_foundation/native/relational_stack_proof_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_DENSE_BITSET_V1_SRC = src/gslt_dense_bitset_v1.c
GSLT_DENSE_BITSET_V1_HEADER = src/gslt_dense_bitset_v1.h
GSLT_DENSE_BITSET_V1_OBJ = src/gslt_dense_bitset_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_DENSE_BITSET_V1_TEST_SRC = tests/support/test_gslt_dense_bitset_v1.c
GSLT_DENSE_BITSET_V1_TEST_OBJ = runtime/bootstrap/test_gslt_dense_bitset_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_DENSE_BITSET_V1_TEST_BIN = runtime/test_gslt_dense_bitset_v1-$(BUILD_OBJ_TAG)
GSLT_INDEXED_INSTRUCTION_DECODER_V1_SRC = src/gslt_indexed_instruction_decoder_v1.c
GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER = src/gslt_indexed_instruction_decoder_v1.h
GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ = src/gslt_indexed_instruction_decoder_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_SRC = tests/support/test_gslt_indexed_instruction_decoder_v1.c
GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_OBJ = runtime/bootstrap/test_gslt_indexed_instruction_decoder_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_BIN = runtime/test_gslt_indexed_instruction_decoder_v1-$(BUILD_OBJ_TAG)
GSLT_INDEXED_VALUE_TABLE_V1_SRC = src/gslt_indexed_value_table_v1.c
GSLT_INDEXED_VALUE_TABLE_V1_HEADER = src/gslt_indexed_value_table_v1.h
GSLT_INDEXED_VALUE_TABLE_V1_OBJ = src/gslt_indexed_value_table_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_INDEXED_VALUE_TABLE_V1_TEST_SRC = tests/support/test_gslt_indexed_value_table_v1.c
GSLT_INDEXED_VALUE_TABLE_V1_TEST_OBJ = runtime/bootstrap/test_gslt_indexed_value_table_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_INDEXED_VALUE_TABLE_V1_TEST_BIN = runtime/test_gslt_indexed_value_table_v1-$(BUILD_OBJ_TAG)
GSLT_SPLIT_INDEXED_TABLE_V1_SRC = src/gslt_split_indexed_table_v1.c
GSLT_SPLIT_INDEXED_TABLE_V1_HEADER = src/gslt_split_indexed_table_v1.h
GSLT_SPLIT_INDEXED_TABLE_V1_OBJ = src/gslt_split_indexed_table_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_SPLIT_INDEXED_TABLE_V1_TEST_SRC = tests/support/test_gslt_split_indexed_table_v1.c
GSLT_SPLIT_INDEXED_TABLE_V1_TEST_OBJ = runtime/bootstrap/test_gslt_split_indexed_table_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_SPLIT_INDEXED_TABLE_V1_TEST_BIN = runtime/test_gslt_split_indexed_table_v1-$(BUILD_OBJ_TAG)
GSLT_LITERAL_HOLE_PROGRAM_V1_SRC = src/gslt_literal_hole_program_v1.c
GSLT_LITERAL_HOLE_PROGRAM_V1_HEADER = src/gslt_literal_hole_program_v1.h
GSLT_LITERAL_HOLE_PROGRAM_V1_OBJ = src/gslt_literal_hole_program_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_SRC = tests/support/test_gslt_literal_hole_program_v1.c
GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_OBJ = runtime/bootstrap/test_gslt_literal_hole_program_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_BIN = runtime/test_gslt_literal_hole_program_v1-$(BUILD_OBJ_TAG)
GSLT_U32_INDEX_V1_SRC = src/gslt_u32_index_v1.c
GSLT_U32_INDEX_V1_HEADER = src/gslt_u32_index_v1.h
GSLT_U32_INDEX_V1_OBJ = src/gslt_u32_index_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_U32_INDEX_V1_TEST_SRC = tests/support/test_gslt_u32_index_v1.c
GSLT_U32_INDEX_V1_TEST_OBJ = runtime/bootstrap/test_gslt_u32_index_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_U32_INDEX_V1_TEST_BIN = runtime/test_gslt_u32_index_v1-$(BUILD_OBJ_TAG)
GSLT_U32_SLICE_ARENA_V1_SRC = src/gslt_u32_slice_arena_v1.c
GSLT_U32_SLICE_ARENA_V1_HEADER = src/gslt_u32_slice_arena_v1.h
GSLT_U32_SLICE_ARENA_V1_OBJ = src/gslt_u32_slice_arena_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_U32_SLICE_ARENA_V1_TEST_SRC = tests/support/test_gslt_u32_slice_arena_v1.c
GSLT_U32_SLICE_ARENA_V1_TEST_OBJ = runtime/bootstrap/test_gslt_u32_slice_arena_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_U32_SLICE_ARENA_V1_TEST_BIN = runtime/test_gslt_u32_slice_arena_v1-$(BUILD_OBJ_TAG)
GSLT_EPOCH_SLOTS_V1_SRC = src/gslt_epoch_slots_v1.c
GSLT_EPOCH_SLOTS_V1_HEADER = src/gslt_epoch_slots_v1.h
GSLT_EPOCH_SLOTS_V1_OBJ = src/gslt_epoch_slots_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_EPOCH_SLOTS_V1_TEST_SRC = tests/support/test_gslt_epoch_slots_v1.c
GSLT_EPOCH_SLOTS_V1_TEST_OBJ = runtime/bootstrap/test_gslt_epoch_slots_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_EPOCH_SLOTS_V1_TEST_BIN = runtime/test_gslt_epoch_slots_v1-$(BUILD_OBJ_TAG)
GSLT_GROUND_DENSE_TERM_V1_SRC = src/gslt_ground_dense_term_v1.c
GSLT_GROUND_DENSE_TERM_V1_HEADER = src/gslt_ground_dense_term_v1.h
GSLT_GROUND_DENSE_TERM_V1_OBJ = src/gslt_ground_dense_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_GROUND_DENSE_TERM_V1_TEST_SRC = tests/support/test_gslt_ground_dense_term_v1.c
GSLT_GROUND_DENSE_TERM_V1_TEST_OBJ = runtime/bootstrap/test_gslt_ground_dense_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
GSLT_GROUND_DENSE_TERM_V1_TEST_BIN = runtime/test_gslt_ground_dense_term_v1-$(BUILD_OBJ_TAG)
GSLT_GROUND_DENSE_TERM_V1_TEST_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(GSLT_EPOCH_SLOTS_V1_OBJ) \
	$(GSLT_GROUND_DENSE_TERM_V1_OBJ)
RELATIONAL_STATE_PROGRAM_V1_SRC = experiments/gslt2parse_foundation/native/relational_state_program_v1.c
RELATIONAL_STATE_PROGRAM_V1_HEADER = experiments/gslt2parse_foundation/native/relational_state_program_v1.h
RELATIONAL_STATE_PROGRAM_V1_OBJ = experiments/gslt2parse_foundation/native/relational_state_program_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
RELATIONAL_STATE_TRANSACTION_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_relational_state_transaction_v1.c
RELATIONAL_STATE_TRANSACTION_V1_TEST_OBJ = runtime/bootstrap/test_relational_state_transaction_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
RELATIONAL_STATE_TRANSACTION_V1_TEST_BIN = runtime/test_relational_state_transaction_v1-$(BUILD_OBJ_TAG)
RELATIONAL_STATE_TRANSACTION_V1_TEST_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(RELATIONAL_VALUE_LIST_V1_OBJ) \
	$(GSLT_DENSE_BITSET_V1_OBJ) \
	$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ) \
	$(GSLT_INDEXED_VALUE_TABLE_V1_OBJ) \
	$(GSLT_LITERAL_HOLE_PROGRAM_V1_OBJ) \
	$(GSLT_U32_INDEX_V1_OBJ) \
	$(GSLT_U32_SLICE_ARENA_V1_OBJ) \
	$(GSLT_EPOCH_SLOTS_V1_OBJ) \
	$(RELATIONAL_STACK_PROOF_V1_OBJ) \
	$(RELATIONAL_STATE_PROGRAM_V1_OBJ)
PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_SRC = experiments/gslt2parse_foundation/native/proof_gslt_relational_runtime_v1.c
PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_HEADER = experiments/gslt2parse_foundation/native/proof_gslt_relational_runtime_v1.h
PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_OBJ = experiments/gslt2parse_foundation/native/proof_gslt_relational_runtime_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_proof_gslt_relational_runtime_v1.c
PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN = runtime/test_proof_gslt_relational_runtime_v1-$(BUILD_OBJ_TAG)
INDEXED_CHECKER_EFFECT_V1_SRC = experiments/gslt2parse_foundation/native/indexed_checker_effect_v1.c
INDEXED_CHECKER_EFFECT_V1_OBJ = experiments/gslt2parse_foundation/native/indexed_checker_effect_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
INDEXED_INFERENCE_STATE_V1_SRC = experiments/gslt2parse_foundation/native/indexed_inference_state_v1.c
INDEXED_INFERENCE_STATE_V1_OBJ = experiments/gslt2parse_foundation/native/indexed_inference_state_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_CURSOR_COMPILE_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_cursor_compile_v1_stream.c
PARSER_PACK_CURSOR_COMPILE_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_cursor_compile_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN = runtime/parser_pack_cursor_compile_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_CURSOR_GENERIC_V1_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(GSLT_DENSE_BITSET_V1_OBJ) \
	src/lib_parse_native_grammar.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_abi_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_action_bytecode_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_native_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_lexical_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_gll_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_glr_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/regular_span_dfa_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/regular_span_nfa_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(LANGDEF_PARSER_V1_OBJ) \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) \
	$(PARSER_PACK_GUARD_RELATION_V1_OBJ) \
	$(PARSER_PACK_GUARD_PLAN_V1_OBJ) \
	$(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ) \
	$(PARSER_PACK_GUARD_REF_V1_OBJ) \
	$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ) \
	$(PARSER_PACK_CURSOR_C_EMITTER_V1_OBJ) \
	$(PARSER_OCCURRENCE_FOLD_V1_OBJ) \
	$(PARSER_OCCURRENCE_SPAN_MASK_V1_OBJ) \
	$(RELATIONAL_VALUE_LIST_V1_OBJ) \
	$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ) \
	$(GSLT_INDEXED_VALUE_TABLE_V1_OBJ) \
	$(GSLT_LITERAL_HOLE_PROGRAM_V1_OBJ) \
	$(GSLT_U32_INDEX_V1_OBJ) \
	$(GSLT_U32_SLICE_ARENA_V1_OBJ) \
	$(GSLT_EPOCH_SLOTS_V1_OBJ) \
	$(RELATIONAL_STACK_PROOF_V1_OBJ) \
	$(RELATIONAL_STATE_PROGRAM_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ) \
	$(SEMANTIC_MASK_NFA_V1_OBJ)
PARSER_PACK_CURSOR_COMPILE_V1_STREAM_LINK_OBJ = $(PARSER_PACK_CURSOR_GENERIC_V1_LINK_OBJ)
PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_guarded_lexical_exec_v1_stream.c
PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_guarded_lexical_exec_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN = runtime/parser_pack_guarded_lexical_exec_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ) $(PARSER_PACK_GUARD_REF_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ) $(PARSER_PACK_CURSOR_C_EMITTER_V1_OBJ) $(INDEXED_CHECKER_EFFECT_V1_OBJ) $(INDEXED_INFERENCE_STATE_V1_OBJ) $(PARSER_ATOM_PROJECTION_V1_OBJ) $(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) $(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ) $(SEMANTIC_MASK_NFA_V1_OBJ)
PARSER_PACK_CURSOR_GENERATED_V1_LINK_OBJ = $(PARSER_PACK_CURSOR_GENERIC_V1_LINK_OBJ)
PARSER_ATOM_PROJECTION_V1_SRC = experiments/gslt2parse_foundation/native/parser_atom_projection_v1.c
PARSER_ATOM_PROJECTION_V1_HEADER = experiments/gslt2parse_foundation/native/parser_atom_projection_v1.h
PARSER_ATOM_PROJECTION_V1_OBJ = experiments/gslt2parse_foundation/native/parser_atom_projection_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_ATOM_PROJECTION_EVENTS_V1_SRC = experiments/gslt2parse_foundation/native/parser_atom_projection_events_v1.c
PARSER_ATOM_PROJECTION_EVENTS_V1_HEADER = experiments/gslt2parse_foundation/native/parser_atom_projection_events_v1.h
PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ = experiments/gslt2parse_foundation/native/parser_atom_projection_events_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_ATOM_PROJECTION_ACTION_V1_SRC = experiments/gslt2parse_foundation/native/parser_atom_projection_action_v1.c
PARSER_ATOM_PROJECTION_ACTION_V1_HEADER = experiments/gslt2parse_foundation/native/parser_atom_projection_action_v1.h
PARSER_ATOM_PROJECTION_ACTION_V1_OBJ = experiments/gslt2parse_foundation/native/parser_atom_projection_action_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_ATOM_PROJECTION_DOMAIN_V1_SRC = experiments/gslt2parse_foundation/native/parser_atom_projection_domain_v1.c
PARSER_ATOM_PROJECTION_DOMAIN_V1_HEADER = experiments/gslt2parse_foundation/native/parser_atom_projection_domain_v1.h
PARSER_ATOM_PROJECTION_DOMAIN_V1_OBJ = runtime/bootstrap/parser_atom_projection_domain_v1.$(BUILD_OBJ_TAG).o
PARSER_ATOM_PROJECTION_CLOSURE_V1_SRC = experiments/gslt2parse_foundation/native/parser_atom_projection_closure_v1.c
PARSER_ATOM_PROJECTION_CLOSURE_V1_HEADER = experiments/gslt2parse_foundation/native/parser_atom_projection_closure_v1.h
PARSER_ATOM_PROJECTION_CLOSURE_V1_OBJ = runtime/bootstrap/parser_atom_projection_closure_v1.$(BUILD_OBJ_TAG).o
PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_atom_projection_closure_v1_stream.c
PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_OBJ = runtime/bootstrap/parser_atom_projection_closure_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_BIN = runtime/parser_atom_projection_closure_v1_stream-$(BUILD_OBJ_TAG)
PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_ATOM_PROJECTION_V1_OBJ) $(PARSER_ATOM_PROJECTION_DOMAIN_V1_OBJ) $(PARSER_ATOM_PROJECTION_CLOSURE_V1_OBJ)
SEMANTIC_MASK_NFA_V1_SRC = experiments/gslt2parse_foundation/native/semantic_mask_nfa_v1.c
SEMANTIC_MASK_NFA_V1_HEADER = experiments/gslt2parse_foundation/native/semantic_mask_nfa_v1.h
SEMANTIC_MASK_NFA_V1_OBJ = experiments/gslt2parse_foundation/native/semantic_mask_nfa_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
SEMANTIC_MASK_NFA_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/semantic_mask_nfa_v1_stream.c
SEMANTIC_MASK_NFA_V1_STREAM_OBJ = runtime/bootstrap/semantic_mask_nfa_v1_stream.$(BUILD_OBJ_TAG).o
SEMANTIC_MASK_NFA_V1_STREAM_BIN = runtime/semantic_mask_nfa_v1_stream-$(BUILD_OBJ_TAG)
SEMANTIC_MASK_NFA_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(SEMANTIC_MASK_NFA_V1_OBJ)
PARSER_PACK_SEMANTIC_MASK_BINDING_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_semantic_mask_binding_v1.c
PARSER_PACK_SEMANTIC_MASK_BINDING_V1_HEADER = experiments/gslt2parse_foundation/native/parser_pack_semantic_mask_binding_v1.h
PARSER_PACK_SEMANTIC_MASK_BINDING_V1_OBJ = runtime/bootstrap/parser_pack_semantic_mask_binding_v1.$(BUILD_OBJ_TAG).o
HE_DOCUMENT_PIPELINE_V1_SRC = experiments/gslt2parse_foundation/adapters/he_document_pipeline_v1.c
HE_DOCUMENT_PIPELINE_V1_HEADER = experiments/gslt2parse_foundation/adapters/he_document_pipeline_v1.h
HE_DOCUMENT_PIPELINE_V1_OBJ = runtime/bootstrap/he_document_pipeline_v1.$(BUILD_OBJ_TAG).o
HE_DOCUMENT_PIPELINE_V1_STREAM_SRC = experiments/gslt2parse_foundation/adapters/he_document_pipeline_v1_stream.c
HE_DOCUMENT_PIPELINE_V1_STREAM_OBJ = runtime/bootstrap/he_document_pipeline_v1_stream.$(BUILD_OBJ_TAG).o
HE_DOCUMENT_PIPELINE_V1_STREAM_BIN = runtime/he_document_pipeline_v1_stream-$(BUILD_OBJ_TAG)
HE_DOCUMENT_PIPELINE_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ) $(PARSER_PACK_GUARD_REF_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ) $(PARSER_ATOM_PROJECTION_V1_OBJ) $(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) $(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ) $(SEMANTIC_MASK_NFA_V1_OBJ) $(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_OBJ)
HE_DOCUMENT_PIPELINE_V1_BENCH_SRC = experiments/gslt2parse_foundation/adapters/he_document_pipeline_v1_bench.c
HE_DOCUMENT_PIPELINE_V1_BENCH_OBJ = runtime/bootstrap/he_document_pipeline_v1_bench.$(BUILD_OBJ_TAG).o
HE_DOCUMENT_PIPELINE_V1_BENCH_BIN = runtime/he_document_pipeline_v1_bench-$(BUILD_OBJ_TAG)
PETTA_DOCUMENT_PIPELINE_V1_SRC = experiments/gslt2parse_foundation/adapters/petta_document_pipeline_v1.c
PETTA_DOCUMENT_PIPELINE_V1_HEADER = experiments/gslt2parse_foundation/adapters/petta_document_pipeline_v1.h
PETTA_DOCUMENT_PIPELINE_V1_OBJ = runtime/bootstrap/petta_document_pipeline_v1.$(BUILD_OBJ_TAG).o
PETTA_DOCUMENT_PIPELINE_V1_BIN = runtime/petta_document_pipeline_v1-$(BUILD_OBJ_TAG)
PETTA_DOCUMENT_PIPELINE_V1_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ) $(PARSER_PACK_GUARD_REF_V1_OBJ) $(PARSER_PACK_GUARD_SCALAR_EXEC_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ) $(PARSER_ATOM_PROJECTION_V1_OBJ) $(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) $(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ) $(SEMANTIC_MASK_NFA_V1_OBJ)
PETTA_DOCUMENT_PIPELINE_V1_LIB = runtime/libcetta_petta_document_pipeline_v1-$(BUILD_OBJ_TAG).so
PETTA_DOCUMENT_PIPELINE_V1_SHARED_LINK_OBJ = $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GUARD_RELATION_V1_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ) $(PARSER_PACK_GUARD_REF_V1_OBJ) $(PARSER_PACK_GUARD_SCALAR_EXEC_V1_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ) $(PARSER_ATOM_PROJECTION_V1_OBJ) $(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) $(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ) $(SEMANTIC_MASK_NFA_V1_OBJ)
PARSER_PACK_GLL_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_gll_v1_stream.c
PARSER_PACK_GLL_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_gll_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_GLL_V1_STREAM_READER_SRC = experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.c
PARSER_PACK_GLL_V1_STREAM_READER_OBJ = experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
PARSER_PACK_GLL_V1_STREAM_BIN = runtime/parser_pack_gll_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_GLL_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_SLR_SUMMARY_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_slr_summary_v1_stream.c
PARSER_PACK_SLR_SUMMARY_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_slr_summary_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_SLR_SUMMARY_V1_STREAM_BIN = runtime/parser_pack_slr_summary_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_SLR_SUMMARY_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_GLR_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_pack_glr_v1_stream.c
PARSER_PACK_GLR_V1_STREAM_OBJ = runtime/bootstrap/parser_pack_glr_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_PACK_GLR_V1_STREAM_BIN = runtime/parser_pack_glr_v1_stream-$(BUILD_OBJ_TAG)
PARSER_PACK_GLR_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
PARSER_PACK_NATIVE_API_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_native_api_v1.c
PARSER_PACK_NATIVE_API_V1_HEADER = experiments/gslt2parse_foundation/native/parser_pack_native_api_v1.h
PARSER_PACK_NATIVE_API_V1_OBJ = runtime/bootstrap/parser_pack_native_api_v1.$(BUILD_OBJ_TAG).o
PARSER_PACK_NATIVE_API_V1_LIB = runtime/libcetta_parser_pack_native_v1-$(BUILD_OBJ_TAG).so
PARSER_PACK_NATIVE_API_V1_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
REGULAR_SPAN_DFA_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_regular_span_dfa_v1.c
REGULAR_SPAN_DFA_V1_TEST_OBJ = runtime/bootstrap/test_regular_span_dfa_v1.$(BUILD_OBJ_TAG).o
REGULAR_SPAN_DFA_V1_TEST_BIN = runtime/test_regular_span_dfa_v1-$(BUILD_OBJ_TAG)
REGULAR_SPAN_DFA_V1_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
REGULAR_SPAN_NFA_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_regular_span_nfa_v1.c
REGULAR_SPAN_NFA_V1_TEST_OBJ = runtime/bootstrap/test_regular_span_nfa_v1.$(BUILD_OBJ_TAG).o
REGULAR_SPAN_NFA_V1_TEST_BIN = runtime/test_regular_span_nfa_v1-$(BUILD_OBJ_TAG)
REGULAR_SPAN_NFA_V1_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
REGULAR_SPAN_DFA_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/regular_span_dfa_v1_stream.c
REGULAR_SPAN_DFA_V1_STREAM_OBJ = runtime/bootstrap/regular_span_dfa_v1_stream.$(BUILD_OBJ_TAG).o
REGULAR_SPAN_DFA_V1_STREAM_BIN = runtime/regular_span_dfa_v1_stream-$(BUILD_OBJ_TAG)
REGULAR_SPAN_DFA_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
FINITE_HORN_ANSWER_STREAM_V1_SRC = experiments/gslt2parse_foundation/native/finite_horn_answer_stream_v1.c
FINITE_HORN_ANSWER_STREAM_V1_OBJ = experiments/gslt2parse_foundation/native/finite_horn_answer_stream_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_PLAN_V1_SRC = experiments/gslt2parse_foundation/native/oslf_native_type_plan_v1.c
OSLF_NATIVE_TYPE_PLAN_V1_HEADER = experiments/gslt2parse_foundation/native/oslf_native_type_plan_v1.h
OSLF_NATIVE_TYPE_PLAN_V1_OBJ = experiments/gslt2parse_foundation/native/oslf_native_type_plan_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_VM_V1_SRC = experiments/gslt2parse_foundation/native/oslf_native_type_vm_v1.c
OSLF_NATIVE_TYPE_VM_V1_HEADER = experiments/gslt2parse_foundation/native/oslf_native_type_vm_v1.h
OSLF_NATIVE_TYPE_VM_V1_OBJ = experiments/gslt2parse_foundation/native/oslf_native_type_vm_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_oslf_native_type_program_v1.c
OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_OBJ = runtime/bootstrap/test_oslf_native_type_program_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_BIN = runtime/test_oslf_native_type_program_v1-$(BUILD_OBJ_TAG)
OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_PLAN_V1_OBJ)
OSLF_NATIVE_TYPE_VM_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_oslf_native_type_vm_v1.c
OSLF_NATIVE_TYPE_VM_V1_TEST_OBJ = runtime/bootstrap/test_oslf_native_type_vm_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_VM_V1_TEST_BIN = runtime/test_oslf_native_type_vm_v1-$(BUILD_OBJ_TAG)
OSLF_NATIVE_TYPE_VM_V1_MATCH_OBJ = runtime/bootstrap/oslf_native_type_vm_match.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_VM_V1_VARIANT_OBJ = runtime/bootstrap/oslf_native_type_vm_variant_shape.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_VM_V1_PRIME_NEED_OBJ = runtime/bootstrap/oslf_native_type_vm_prime_need.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_VM_V1_TEST_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_PLAN_V1_OBJ) \
	$(GSLT_EPOCH_SLOTS_V1_OBJ) \
	$(GSLT_GROUND_DENSE_TERM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_MATCH_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_VARIANT_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_PRIME_NEED_OBJ)
OSLF_NATIVE_TYPE_VM_V1_LDFLAGS = \
	-ldl -lm -pthread $(GMP_LDFLAGS) \
	$(if $(filter 1,$(ENABLE_SANITIZERS)),-fsanitize=$(SANITIZERS) -fno-sanitize-recover=all,)
PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_LINK_OBJ = \
	$(PARSER_PACK_CURSOR_GENERATED_V1_LINK_OBJ) \
	$(OSLF_NATIVE_TYPE_PLAN_V1_OBJ) \
	$(GSLT_GROUND_DENSE_TERM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_MATCH_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_VARIANT_OBJ) \
	$(OSLF_NATIVE_TYPE_VM_V1_PRIME_NEED_OBJ) \
	experiments/gslt2parse_foundation/native/finite_horn_gslt_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/gslt_provider_runtime.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/gslt_finite_fact_provider_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/gslt_horn_runtime.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/gslt_compiled_runtime.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/gslt_language_runtime.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_OBJ)
OSLF_NATIVE_TYPE_INSPECT_V1_SRC = tools/oslf_native_type_inspect_v1.c
OSLF_NATIVE_TYPE_INSPECT_V1_OBJ = runtime/bootstrap/oslf_native_type_inspect_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
OSLF_NATIVE_TYPE_INSPECT_V1_BIN = runtime/oslf_native_type_inspect_v1-$(BUILD_OBJ_TAG)
OSLF_NATIVE_TYPE_INSPECT_V1_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(OSLF_NATIVE_TYPE_PLAN_V1_OBJ)
PROOF_STORAGE_PLAN_V1_SRC = experiments/gslt2parse_foundation/native/proof_storage_plan_v1.c
PROOF_STORAGE_PLAN_V1_HEADER = experiments/gslt2parse_foundation/native/proof_storage_plan_v1.h
PROOF_STORAGE_PLAN_V1_OBJ = experiments/gslt2parse_foundation/native/proof_storage_plan_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
FIRST_ORDER_FRAME_DECODER_V1_SRC = experiments/gslt2parse_foundation/native/first_order_frame_decoder_v1.c
FIRST_ORDER_FRAME_DECODER_V1_HEADER = experiments/gslt2parse_foundation/native/first_order_frame_decoder_v1.h
FIRST_ORDER_FRAME_DECODER_V1_OBJ = experiments/gslt2parse_foundation/native/first_order_frame_decoder_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
FIRST_ORDER_FRAME_DECODER_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_first_order_frame_decoder_v1.c
FIRST_ORDER_FRAME_DECODER_V1_TEST_OBJ = runtime/bootstrap/test_first_order_frame_decoder_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
FIRST_ORDER_FRAME_DECODER_V1_TEST_BIN = runtime/test_first_order_frame_decoder_v1-$(BUILD_OBJ_TAG)
FIRST_ORDER_FRAME_DECODER_V1_TEST_LINK_OBJ = \
	$(PARSER_PACK_CURSOR_GENERIC_V1_LINK_OBJ) \
	$(OSLF_NATIVE_TYPE_PLAN_V1_OBJ) \
	$(PROOF_STORAGE_PLAN_V1_OBJ) \
	$(FIRST_ORDER_FRAME_DECODER_V1_OBJ)
RELATIONAL_STACK_PROOF_CACHE_V1_TEST_SRC = experiments/gslt2parse_foundation/native/test_relational_stack_proof_cache_v1.c
RELATIONAL_STACK_PROOF_CACHE_V1_TEST_OBJ = runtime/bootstrap/test_relational_stack_proof_cache_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
RELATIONAL_STACK_PROOF_CACHE_V1_TEST_BIN = runtime/test_relational_stack_proof_cache_v1-$(BUILD_OBJ_TAG)
RELATIONAL_STACK_PROOF_CACHE_V1_TEST_LINK_OBJ = \
	$(RELATIONAL_VALUE_LIST_V1_OBJ) \
	$(GSLT_DENSE_BITSET_V1_OBJ) \
	$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ) \
	$(GSLT_INDEXED_VALUE_TABLE_V1_OBJ) \
	$(GSLT_LITERAL_HOLE_PROGRAM_V1_OBJ) \
	$(GSLT_U32_INDEX_V1_OBJ) \
	$(GSLT_U32_SLICE_ARENA_V1_OBJ) \
	$(GSLT_EPOCH_SLOTS_V1_OBJ) \
	$(RELATIONAL_STACK_PROOF_V1_OBJ)
OSLF_NATIVE_TYPE_UNKNOWN_V1 = tests/langdef/fixtures/oslf_native_type_unknown_v1.answers
OSLF_NATIVE_TYPE_MALFORMED_ARITY_V1 = tests/langdef/fixtures/oslf_native_type_malformed_arity_v1.answers
OSLF_NATIVE_TYPE_DUPLICATE_HEAD_V1 = tests/langdef/fixtures/oslf_native_type_duplicate_head_v1.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_SOURCE = experiments/gslt2parse_foundation/presentations/canaries/oslf_native_program_v1.metta
OSLF_NATIVE_PROGRAM_CANARY_V1_DIR = runtime/bootstrap/oslf-native-program-canary-v1.$(BUILD_OBJ_TAG)
OSLF_NATIVE_PROGRAM_CANARY_V1_NTT = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/ntt.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_ARITY = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/bad-arity.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_CONSTRUCTOR = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/bad-constructor.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_BODY = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/bad-body.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_DELETED_STEP = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/deleted-step.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_WRONG_TRANSITION = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/wrong-transition.answers
OSLF_NATIVE_PROGRAM_CANARY_V1_MISSING_CARRIER = $(OSLF_NATIVE_PROGRAM_CANARY_V1_DIR)/missing-carrier.answers
OSLF_NATIVE_OPEN_PROGRAM_V1_SOURCE = experiments/gslt2parse_foundation/presentations/canaries/oslf_native_open_program_v1.metta
OSLF_NATIVE_OPEN_CAPABILITY_V1_SOURCE = experiments/gslt2parse_foundation/presentations/canaries/oslf_native_open_capability_v1.metta
OSLF_NATIVE_OPEN_PROGRAM_V1_DIR = runtime/bootstrap/oslf-native-open-program-v1.$(BUILD_OBJ_TAG)
OSLF_NATIVE_OPEN_PROGRAM_V1_NTT = $(OSLF_NATIVE_OPEN_PROGRAM_V1_DIR)/ntt.answers
OSLF_NATIVE_OPEN_CAPABILITY_V1_REFLECTION = $(OSLF_NATIVE_OPEN_PROGRAM_V1_DIR)/capability-reflection.answers
OSLF_NATIVE_OPEN_PROGRAM_V1_MISSING_INTERFACE = $(OSLF_NATIVE_OPEN_PROGRAM_V1_DIR)/missing-interface.answers
OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_RELATION = $(OSLF_NATIVE_OPEN_PROGRAM_V1_DIR)/wrong-relation.answers
OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_ARITY = $(OSLF_NATIVE_OPEN_PROGRAM_V1_DIR)/wrong-arity.answers
OSLF_NATIVE_OPEN_PROGRAM_V1_DUPLICATE_INTERFACE = $(OSLF_NATIVE_OPEN_PROGRAM_V1_DIR)/duplicate-interface.answers
PROOF_STORAGE_UNKNOWN_V1 = tests/langdef/fixtures/proof_storage_unknown_v1.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE = experiments/gslt2parse_foundation/presentations/canaries/first_order_frame_generated_guest_v1.metta
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR = runtime/bootstrap/first-order-frame-generated-guest-v1.$(BUILD_OBJ_TAG)
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_TABLES = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/tables.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_MACHINE = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/machine.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_ACTION = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/action.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/state.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PROOF = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/proof.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_EVIDENCE = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/evidence.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_RELATIONAL = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/relational.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_NTT = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/ntt.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_COMBINED = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/combined.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/payload.metta
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/required.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/denoted.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED_KEYS = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/required-keys.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED_KEYS = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/denoted-keys.answers
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STORAGE = $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DIR)/storage.answers
RELATIONAL_STATE_TRANSACTION_V1_SOURCE = experiments/gslt2parse_foundation/presentations/canaries/relational_state_transaction_v1.metta
RELATIONAL_STATE_TRANSACTION_V1_DIR = runtime/bootstrap/relational-state-transaction-v1.$(BUILD_OBJ_TAG)
RELATIONAL_STATE_TRANSACTION_V1_TABLES = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/tables.answers
RELATIONAL_STATE_TRANSACTION_V1_ACTIONS = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/actions.answers
RELATIONAL_STATE_TRANSACTION_V1_STATE = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/state.answers
RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_SOURCE = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/persistent-source.metta
RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_TABLES = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/persistent-tables.answers
RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_ACTIONS = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/persistent-actions.answers
RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE = $(RELATIONAL_STATE_TRANSACTION_V1_DIR)/persistent-state.answers
PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_SRC = experiments/gslt2parse_foundation/native/parser_pack_transparent_inline_native_v1.c
PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_HEADER = experiments/gslt2parse_foundation/native/parser_pack_transparent_inline_native_v1.h
PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_OBJ = runtime/bootstrap/parser_pack_transparent_inline_native_v1.$(BUILD_OBJ_TAG).o
PARSER_ACTION_BYTECODE_V1_STREAM_SRC = experiments/gslt2parse_foundation/native/parser_action_bytecode_v1_stream.c
PARSER_ACTION_BYTECODE_V1_STREAM_OBJ = runtime/bootstrap/parser_action_bytecode_v1_stream.$(BUILD_OBJ_TAG).o
PARSER_ACTION_BYTECODE_V1_STREAM_BIN = runtime/parser_action_bytecode_v1_stream-$(BUILD_OBJ_TAG)
PARSER_ACTION_BYTECODE_V1_STREAM_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ) $(PARSER_ATOM_PROJECTION_V1_OBJ) $(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) $(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ)
GSLT2PARSE_AUX_OBJ = \
	$(LIB_PARSE_GLL_UTF8_FOREST_TEST_OBJ) \
	$(LIB_PARSE_GLR_UTF8_FOREST_TEST_OBJ) \
	$(LIB_PARSE_SLR_PREPARED_TEST_OBJ) \
	$(PARSER_PACK_GLL_V1_TEST_OBJ) \
	$(PARSER_PACK_GLR_V1_TEST_OBJ) \
	$(PARSER_PACK_LEXICAL_V1_TEST_OBJ) \
	$(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_OBJ) \
	$(PARSER_PACK_GUARD_RELATION_V1_OBJ) \
	$(PARSER_PACK_GUARD_PLAN_V1_OBJ) \
	$(PARSER_PACK_GUARD_PLAN_V1_TEST_OBJ) \
	$(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) \
	$(PARSER_PACK_GUARD_PLAN_V1_STREAM_OBJ) \
	$(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ) \
	$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_OBJ) \
	$(PARSER_PACK_GUARD_REF_V1_OBJ) \
	$(PARSER_PACK_GUARD_REF_V1_STREAM_OBJ) \
	$(PARSER_PACK_GUARD_SCALAR_EXEC_V1_OBJ) \
	$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ) \
	$(PARSER_PACK_CURSOR_C_EMITTER_V1_OBJ) \
	$(INDEXED_CHECKER_EFFECT_V1_OBJ) \
	$(INDEXED_INFERENCE_STATE_V1_OBJ) \
	$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_OBJ) \
	$(PARSER_ATOM_PROJECTION_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_DOMAIN_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_CLOSURE_V1_OBJ) \
	$(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_OBJ) \
	$(SEMANTIC_MASK_NFA_V1_OBJ) \
	$(PARSER_OCCURRENCE_SPAN_MASK_V1_OBJ) \
	$(SEMANTIC_MASK_NFA_V1_STREAM_OBJ) \
	$(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_OBJ) \
	$(HE_DOCUMENT_PIPELINE_V1_OBJ) \
	$(HE_DOCUMENT_PIPELINE_V1_STREAM_OBJ) \
	$(HE_DOCUMENT_PIPELINE_V1_BENCH_OBJ) \
	$(PETTA_DOCUMENT_PIPELINE_V1_OBJ) \
	$(PARSER_PACK_GLL_V1_STREAM_OBJ) \
	$(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) \
	$(PARSER_PACK_SLR_SUMMARY_V1_STREAM_OBJ) \
	$(PARSER_PACK_GLR_V1_STREAM_OBJ) \
	$(PARSER_PACK_NATIVE_API_V1_OBJ) \
	$(REGULAR_SPAN_DFA_V1_TEST_OBJ) \
	$(REGULAR_SPAN_NFA_V1_TEST_OBJ) \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	$(PARSER_ACTION_BYTECODE_V1_STREAM_OBJ) \
	$(REGULAR_SPAN_DFA_V1_STREAM_OBJ)
STAGE0_BIN = runtime/cetta-stage0-$(BUILD_OBJ_TAG)
VARIANT_SHAPE_TEST_BIN = runtime/test_variant_shape_roundtrip-$(BUILD_OBJ_TAG)
BINDINGS_LOOKUP_INDEX_TEST_BIN = runtime/test_bindings_lookup_index-$(BUILD_OBJ_TAG)
ATOM_DEEP_COPY_TEST_BIN = runtime/test_atom_deep_copy_iterative-$(BUILD_OBJ_TAG)
ABT_TEST_BIN = runtime/test_abt-$(BUILD_OBJ_TAG)
ABT_MM2_BOUNDARY_TEST_BIN = runtime/test_abt_mm2_boundary-$(BUILD_OBJ_TAG)
ABT_BENCH_BIN = runtime/bench_abt-$(BUILD_OBJ_TAG)
NAME_KEY_TEST_BIN = runtime/test_name_key-$(BUILD_OBJ_TAG)
NAME_KEY_MUTATION_TEST_BIN = runtime/test_name_key_mutation-$(BUILD_OBJ_TAG)
PRIME_NEED_TEST_BIN = runtime/test_prime_need-$(BUILD_OBJ_TAG)
PRIME_CONTEXT_MUTATION_TEST_BIN = runtime/test_prime_context_mutation-$(BUILD_OBJ_TAG)
PRIME_NEED_UNIT_CPPFLAGS = -DCETTA_RUNTIME_STATS_NOOP=1
REGISTRY_RESOLVER_TEST_SRC = tests/test_registry_resolver.c
REGISTRY_RESOLVER_TEST_OBJ = runtime/bootstrap/test_registry_resolver.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
REGISTRY_RESOLVER_TEST_BIN = runtime/test_registry_resolver-$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),-runtime-stats,)
REGISTRY_RESOLVER_TEST_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
REGISTRY_LOOKUP_BENCH_SRC = benchmarks/prime/bench_registry_lookup.c
REGISTRY_LOOKUP_BENCH_OBJ = runtime/bootstrap/bench_registry_lookup.$(BUILD_OBJ_TAG).o
REGISTRY_LOOKUP_BENCH_BIN = runtime/bench_registry_lookup-$(BUILD_OBJ_TAG)
REGISTRY_LOOKUP_BENCH_LINK_OBJ = $(FALLBACK_EVAL_TEST_LINK_OBJ)
ABT_MUTATION_IDS = 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
ABT_MUTATION_TEST_BINS = $(foreach id,$(ABT_MUTATION_IDS),runtime/test_abt_mutation-$(BUILD_OBJ_TAG)-$(id))
GROUNDED_STANDALONE_SRC = src/grounded.c src/petta_semantics.c src/abt.c src/atom_blob.c
GROUNDED_STANDALONE_DEPS = $(GROUNDED_STANDALONE_SRC) $(ABT_DEFAULT_SIGNATURES_BLOB)
PARSER_STANDALONE_SRC = src/parser.c src/name_key.c
# Bindings owns a PrimeNeedSnapshot even in non-Prime harnesses, so every
# standalone linker that includes match.c must include the snapshot algebra.
MATCH_STANDALONE_SRC = src/match.c src/prime_need.c
MORK_BRIDGE_CONTEXTUAL_EXACT_ROWS_TEST_BIN = runtime/test_mork_bridge_contextual_exact_rows-$(BUILD_OBJ_TAG)
MORK_CURSOR_BYTE_BUFFER_COUNT_ABI_TEST_BIN = runtime/test_mork_cursor_byte_buffer_count_abi-$(BUILD_OBJ_TAG)
MORK_CURSOR_EXPR_ROW_STREAM_ABI_TEST_BIN = runtime/test_mork_cursor_expr_row_stream_abi-$(BUILD_OBJ_TAG)
MORK_QUERY_ROW_STREAM_ABI_TEST_BIN = runtime/test_mork_query_row_stream_abi-$(BUILD_OBJ_TAG)
SPACE_TERM_UNIVERSE_MEMBERSHIP_TEST_BIN = runtime/test_space_term_universe_membership-$(BUILD_OBJ_TAG)
TERM_UNIVERSE_STORE_ABI_TEST_BIN = runtime/test_term_universe_store_abi-$(BUILD_OBJ_TAG).runtime-stats
TERM_UNIVERSE_BACKEND_ADD_ABI_TEST_BIN = runtime/test_term_universe_backend_add_abi-$(BUILD_OBJ_TAG).runtime-stats
LET_BRANCH_ARENA_RESET_NO_ESCAPE_TEST_BIN = runtime/test_let_branch_arena_reset_no_escape-$(BUILD_OBJ_TAG)
PATHMAP_BACKEND_PRIMARY_DESTRUCTIVE_ABI_TEST_BIN = runtime/test_pathmap_backend_primary_destructive_abi-$(BUILD_OBJ_TAG)
PATHMAP_BACKEND_PRIMARY_REPLACE_ABI_TEST_BIN = runtime/test_pathmap_backend_primary_replace_abi-$(BUILD_OBJ_TAG)
PATHMAP_TYPED_QUERY_ABI_TEST_BIN = runtime/test_pathmap_typed_query_abi-$(BUILD_OBJ_TAG)
PATHMAP_SEMI_NAIVE_ABI_TEST_BIN = runtime/test_pathmap_semi_naive_abi-$(BUILD_OBJ_TAG)
EXECUTION_CONTRACTS_TEST_BIN = runtime/test_execution_contracts_generated-$(BUILD_OBJ_TAG)
EXECUTION_CONTRACTS_SPEC = lib/gslt_execution_contracts.metta
EXECUTION_CONTRACTS_GENERATOR = scripts/gen_execution_contracts.py
EXECUTION_CONTRACTS_GENERATED_H = src/generated/cetta_execution_contracts.generated.h
LIB_PARSE_INFERENCE_BENCH_BIN = runtime/bench_lib_parse_inference_native-$(BUILD_OBJ_TAG)
GSLT2PARSE_SCHEMA_V1_NATIVE_DIR = experiments/gslt2parse_foundation/native
GSLT2PARSE_SCHEMA_V1_NATIVE_BIN = runtime/test_finite_horn_gslt_v1-$(BUILD_OBJ_TAG)
GSLT2PARSE_CHART_V1_NATIVE_BIN = runtime/finite_horn_chart_v1-$(BUILD_OBJ_TAG)
GSLT_HORN_RUNTIME_TEST_BIN = runtime/test_gslt_horn_runtime-$(BUILD_OBJ_TAG)
GSLT_HORN_RUNTIME_CANARY_V1 = tests/fixtures/gslt_horn_runtime_canary_v1.metta
GSLT_LANGUAGE_RUNTIME_TEST_BIN = runtime/test_gslt_language_runtime-$(BUILD_OBJ_TAG)
GSLT_COMPILED_PACKET_CHECK_BIN = runtime/check_gslt_compiled_packet_v1-$(BUILD_OBJ_TAG)
NIK_RUNTIME_TEST_BIN = runtime/test_nik_runtime_v1-$(BUILD_OBJ_TAG)
GSLT_PROVIDER_RUNTIME_TEST_BIN = runtime/test_gslt_provider_runtime-$(BUILD_OBJ_TAG)
GSLT_ABT_PROVIDER_V1_TEST_BIN = runtime/test_gslt_abt_provider_v1-$(BUILD_OBJ_TAG)
ZERO_INTERACT_SPACE_PROVIDER_TEST_BIN = runtime/test_zero_interact_space_provider_v1-$(BUILD_OBJ_TAG)
GSLT_SUPPORT_TRANSFORM_RUNTIME_TEST_BIN = runtime/test_gslt_support_transform_runtime-$(BUILD_OBJ_TAG)
METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN = runtime/check_mettazero_compilation_certificate_v1-$(BUILD_OBJ_TAG)
SUBZERO_LANGDEF_V1 = langdef/subzero/langdef.metta
GSLT_LANGUAGE_GENERATOR_V1 = tools/generate_gslt_language_v1.py
PRIME_NIK_AUTHORITY_EXPORTER_V1 = tests/support/export_prime_nik_authority_catalog_v1.lean
PRIME_NIK_AUTHORITY_GENERATOR_V1 = tools/generate_nik_authority_runtime_v1.py
PRIME_NIK_AUTHORITY_GENERATION_TEST_V1 = tools/test_nik_authority_generation_v1.py
PRIME_NIK_AUTHORITY_CATALOG_V1 = langdef/prime/nik_authority_catalog_v1.metta
PRIME_NIK_AUTHORITY_SEMANTICS_V1 = langdef/prime/nik_authority_runtime_v1.metta
PRIME_NIK_RUNTIME_MANIFEST_V1 = langdef/prime/nik_runtime_v1.metta
PRIME_NIK_AUTHORITIES_GENERATED_H = src/generated/prime_nik_authorities_v1.generated.h
PRIME_NIK_AUTHORITIES_GENERATED_C = src/generated/prime_nik_authorities_v1.generated.c
PRIME_NIK_RUNTIME_GENERATED_H = src/generated/prime_nik_runtime_v1.generated.h
PRIME_NIK_RUNTIME_GENERATED_C = src/generated/prime_nik_runtime_v1.generated.c
PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_V1 = langdef/prime/nik_side_condition_provider_catalog_v1.metta
PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_H = src/generated/prime_nik_side_condition_provider_catalog_v1.generated.h
PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_C = src/generated/prime_nik_side_condition_provider_catalog_v1.generated.c
PRIME_NIK_MEGALODON_TEST_V1 = tools/test_nik_megalodon_implicational_v1.py
PRIME_NIK_MEGALODON_POSITIVE_V1 = tests/support/megalodon/positive_imp_modus_ponens.mg
PRIME_NIK_MEGALODON_NEGATIVE_V1 = tests/support/megalodon/negative_wrong_exact.mg
PRIME_NIK_MEGALODON_TERM_TEST_V1 = tools/test_nik_megalodon_term_quantified_v1.py
PRIME_NIK_MEGALODON_TERM_POSITIVE_V1 = tests/support/megalodon/positive_term_forall_identity.mg
PRIME_NIK_MEGALODON_TERM_WITNESS_V1 = tests/support/megalodon/positive_term_forall_identity.nik
PRIME_NIK_MEGALODON_TERM_EXPORTER_V1 = tests/support/export_prime_nik_megalodon_term_v1.lean
PRIME_NIK_MEGALODON_POLY_TEST_V1 = tools/test_nik_megalodon_polymorphic_v1.py
PRIME_NIK_MEGALODON_POLY_POSITIVE_V1 = tests/support/megalodon/positive_type_polymorphic_reuse.mg
PRIME_NIK_MEGALODON_POLY_WITNESS_V1 = tests/support/megalodon/positive_type_polymorphic_reuse.nik
PRIME_NIK_MEGALODON_POLY_EXPORTER_V1 = tests/support/export_prime_nik_megalodon_polymorphic_v1.lean
PRIME_NIK_MEGALODON_KNOWN_IMP_TEST_V1 = tools/test_nik_megalodon_known_implication_v1.py
PRIME_NIK_MEGALODON_KNOWN_IMP_POSITIVE_V1 = tests/support/megalodon/positive_known_implication_reuse.mg
PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1 = tests/support/megalodon/positive_known_implication_reuse.nik
PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1 = tests/support/export_prime_nik_megalodon_known_implication_v1.lean
PRIME_NIK_MEGALODON_DEFINITION_TEST_V1 = tools/test_nik_megalodon_definition_conversion_v1.py
PRIME_NIK_MEGALODON_DEFINITION_POSITIVE_V1 = tests/support/megalodon/positive_definition_conversion.mg
PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1 = tests/support/megalodon/positive_definition_conversion.nik
PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1 = tests/support/export_prime_nik_megalodon_definition_v1.lean
PRIME_NIK_MEGALODON_TACTICS_TEST_V1 = tools/test_nik_megalodon_tactics_package_v1.py
PRIME_NIK_MEGALODON_TACTICS_POSITIVE_V1 = tests/support/megalodon/positive_tactics_package.mg
PRIME_NIK_PROOF_DAG_COMPILER_V1 = tools/nik_proof_dag_v1.py
PRIME_NIK_PROOF_DAG_TEST_V1 = tools/test_nik_proof_dag_v1.py
MEGALODON_AUTO_BIN ?= $(abspath ../../Mettapedia/megalodon/bin/megalodon)
METTAPEDIA_LEAN_ROOT ?=
METTAPEDIA_LEAN_AUTO_ROOT ?= $(abspath ../../Mettapedia/lean/mettapedia)
GSLT_IL_LANGDEF_V1 = langdef/gslt-il/langdef.metta
GSLT_IL_FINITE_COMMAND_V1 = langdef/gslt-il/semantics/finite_indexed_command_v1.metta
GSLT_IL_GENERATED_LANGUAGE_V1_H = src/generated/gslt_il_language_v1.generated.h
GSLT_IL_GENERATED_LANGUAGE_V1_C = src/generated/gslt_il_language_v1.generated.c
GSLT_IL_SEMANTICS_TEST_V1 = tools/test_gslt_il_semantics_v1.py
GSLT_IL_RULE_MUTATIONS_TEST_V1 = tools/test_gslt_il_rule_mutations_v1.py
GSLT_IL_CLI_TEST_V1 = tools/test_gslt_il_cli_v1.py
ZEROUV_LANGDEF_V1 = langdef/zerouv/langdef.metta
ZEROUV_CONTROL_V1 = langdef/zerouv/semantics/control_v1.metta
ZEROUV_GENERATED_LANGUAGE_V1_H = src/generated/zerouv_language_v1.generated.h
ZEROUV_GENERATED_LANGUAGE_V1_C = src/generated/zerouv_language_v1.generated.c
ZEROUV_SEMANTICS_TEST_V1 = tools/test_zerouv_semantics_v1.py
ZEROUV_RULE_MUTATIONS_TEST_V1 = tools/test_zerouv_rule_mutations_v1.py
ZEROUV_CLI_TEST_V1 = tools/test_zerouv_cli_v1.py
METTA_INTERACT_LANGDEF_V1 = langdef/metta-interact/langdef.metta
METTA_INTERACT_CORE_V1 = langdef/metta-interact/semantics/interaction_core_v1.metta
METTA_INTERACT_SEQUENCE_V1 = langdef/metta-interact/semantics/sequence_handler_v1.metta
METTA_INTERACT_GENERATED_LANGUAGE_V1_H = src/generated/metta_interact_language_v1.generated.h
METTA_INTERACT_GENERATED_LANGUAGE_V1_C = src/generated/metta_interact_language_v1.generated.c
METTA_INTERACT_CLI_TEST_V1 = tools/test_metta_interact_cli_v1.py
METTA_INTERACT_SEMANTICS_TEST_V1 = tools/test_metta_interact_semantics_v1.py
METTA_INTERACT_RULE_MUTATIONS_TEST_V1 = tools/test_metta_interact_rule_mutations_v1.py
GSLT_SUPPORT_TRANSFORM_GENERATOR_V1 = tools/generate_gslt_support_transform_v1.py
GSLT_SUPPORT_TRANSFORM_GENERATION_TEST_V1 = tools/test_gslt_support_transform_generation_v1.py
MM2_GSLT_PROFILE_V1 = langdef/mm2/gslt_profile_v1.metta
MM2_GSLT_PROFILE_GENERATED_H = src/generated/mm2_gslt_profile_v1.generated.h
MM2_GSLT_PROFILE_GENERATED_C = src/generated/mm2_gslt_profile_v1.generated.c
GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1 = tools/generate_gslt_compilation_certificate_v1.py
GSLT_COMPILATION_CERTIFICATE_TEST_V1 = tools/test_gslt_compilation_certificate_v1.py
SUBZERO_GENERATED_LANGUAGE_V1_H = src/generated/subzero_language_v1.generated.h
SUBZERO_GENERATED_LANGUAGE_V1_C = src/generated/subzero_language_v1.generated.c
METTAZERO_LANGDEF_V1 = langdef/zero/langdef.metta
METTAZERO_QUOTE_MATCH_V1 = langdef/shared/finite_horn_quote_match_v1.metta
METTAZERO_QUERY_KERNEL_V1 = langdef/zero/semantics/query_kernel_v1.metta
METTAZERO_CLOSED_BAG_OBSERVATION_V1 = langdef/zero/semantics/closed_bag_observation_v1.metta
METTAZERO_GENERATED_LANGUAGE_V1_H = src/generated/zero_language_v1.generated.h
METTAZERO_GENERATED_LANGUAGE_V1_C = src/generated/zero_language_v1.generated.c
METTAZERO_COMPILATION_CERTIFICATE_V1 = src/generated/zero_language_v1.compilation-certificate.metta
METTAZERO_SEMANTIC_RUNNER_V1 = langdef/zero/semantics/semantic_runner_v1.metta
METTAZERO_EXP_GENERATED_LANGUAGE_V1_H = src/generated/zero_exp_language_v1.generated.h
METTAZERO_EXP_GENERATED_LANGUAGE_V1_C = src/generated/zero_exp_language_v1.generated.c
METTAZERO_EXP_COMPILATION_CERTIFICATE_V1 = src/generated/zero_exp_language_v1.compilation-certificate.metta
METTAZERO_REVISIONED_EMIT_V1 = langdef/zero/semantics/revisioned_emit_v1.metta
METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H = src/generated/zero_emit_language_v1.generated.h
METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C = src/generated/zero_emit_language_v1.generated.c
METTAZERO_EMIT_COMPILATION_CERTIFICATE_V1 = src/generated/zero_emit_language_v1.compilation-certificate.metta
METTAZERO_EMIT_SEMANTICS_TEST_V1 = tools/test_mettazero_emit_semantics_v1.py
METTAZERO_EMIT_RULE_MUTATIONS_TEST_V1 = tools/test_mettazero_emit_rule_mutations_v1.py
METTAZERO_OPEN_SUBSTITUTION_V1 = langdef/zero/semantics/open_substitution_v1.metta
METTAZERO_SUPPORT_INDEXED_ABT_MATCH_V1 = langdef/zero/semantics/support_indexed_abt_match_v1.metta
METTAZERO_REVISIONED_INTERACT_V1 = langdef/zero/semantics/revisioned_interact_v1.metta
METTAZERO_INTERACT_PROVIDER_CATALOG_V1 = langdef/zero/interact_provider_catalog_v1.metta
METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H = src/generated/zero_interact_language_v1.generated.h
METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C = src/generated/zero_interact_language_v1.generated.c
METTAZERO_INTERACT_COMPILATION_CERTIFICATE_V1 = src/generated/zero_interact_language_v1.compilation-certificate.metta
METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_H = src/generated/zero_interact_provider_catalog_v1.generated.h
METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_C = src/generated/zero_interact_provider_catalog_v1.generated.c
METTAZERO_INTERACT_PROVIDER_REFERENCE_V1 = tests/fixtures/metta_zero_interact_provider_reference_v1.metta
METTAZERO_INTERACT_SEMANTICS_TEST_V1 = tools/test_mettazero_interact_semantics_v1.py
METTAZERO_INTERACT_RULE_MUTATIONS_TEST_V1 = tools/test_mettazero_interact_rule_mutations_v1.py
METTAZERO_GROUND_LIBRARY_CANARY_V1_MANIFEST = tests/fixtures/metta_zero_ground_library_v1/langdef.metta
METTAZERO_GROUND_LIBRARY_CANARY_V1_SOURCE = tests/fixtures/metta_zero_ground_capability_v1.metta
METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_H = tests/generated/metta_zero_ground_library_v1.generated.h
METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C = tests/generated/metta_zero_ground_library_v1.generated.c
GSLT_COMPILED_CANARY_V1_MANIFEST = tests/fixtures/gslt_compiled_canary_v1/langdef.metta
GSLT_COMPILED_CANARY_V1_SOURCE = tests/fixtures/gslt_compiled_canary_v1/semantics/renamed_answer_v1.metta
GSLT_COMPILED_CANARY_V1_GENERATED_H = tests/generated/gslt_compiled_canary_v1.generated.h
GSLT_COMPILED_CANARY_V1_GENERATED_C = tests/generated/gslt_compiled_canary_v1.generated.c
GSLT_PIPELINE_CANARY_V1_MANIFEST = tests/fixtures/gslt_pipeline_canary_v1/langdef.metta
GSLT_PIPELINE_CANARY_V1_SOURCE = tests/fixtures/gslt_pipeline_canary_v1/semantics/renamed_pipeline_v1.metta
GSLT_PIPELINE_CANARY_V1_GENERATED_H = tests/generated/gslt_pipeline_canary_v1.generated.h
GSLT_PIPELINE_CANARY_V1_GENERATED_C = tests/generated/gslt_pipeline_canary_v1.generated.c
GSLT_PROVIDER_CANARY_V1_MANIFEST = tests/fixtures/gslt_provider_canary_v1/langdef.metta
GSLT_PROVIDER_CANARY_V1_SOURCE = tests/fixtures/gslt_provider_canary_v1/semantics/provider_call_v1.metta
GSLT_PROVIDER_CANARY_V1_GENERATED_H = tests/generated/gslt_provider_canary_v1.generated.h
GSLT_PROVIDER_CANARY_V1_GENERATED_C = tests/generated/gslt_provider_canary_v1.generated.c
PROOF_TRACE_COMPILED_CANARY_V1_MANIFEST = tests/fixtures/proof_trace_compiled_canary_v1/langdef.metta
PROOF_TRACE_COMPILED_CANARY_V1_GENERATED_H = tests/generated/proof_trace_compiled_canary_v1.generated.h
PROOF_TRACE_COMPILED_CANARY_V1_GENERATED_C = tests/generated/proof_trace_compiled_canary_v1.generated.c
PROOF_TRACE_COMPILED_RUNTIME_V1_TEST_BIN = runtime/test_proof_trace_compiled_runtime_v1-$(BUILD_OBJ_TAG)
METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST = langdef/metamath/proof_trace_language_v1.metta
METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_H = langdef/metamath/generated/proof_trace_language_v1.generated.h
METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_C = langdef/metamath/generated/proof_trace_language_v1.generated.c
METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1 = langdef/metamath/proof_trace_provider_catalog_v1.metta
METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_H = langdef/metamath/generated/proof_trace_provider_catalog_v1.generated.h
METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_C = langdef/metamath/generated/proof_trace_provider_catalog_v1.generated.c
METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST = langdef/metamath/proof_machine_language_v1.metta
METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_H = langdef/metamath/generated/proof_machine_language_v1.generated.h
METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_C = langdef/metamath/generated/proof_machine_language_v1.generated.c
METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1 = langdef/metamath/proof_machine_provider_catalog_v1.metta
METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_H = langdef/metamath/generated/proof_machine_provider_catalog_v1.generated.h
METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_C = langdef/metamath/generated/proof_machine_provider_catalog_v1.generated.c
GSLT_PROVIDER_CATALOG_GENERATOR_V1 = tools/generate_gslt_provider_catalog_v1.py
GSLT_PROVIDER_CATALOG_GENERATION_TEST_V1 = tools/test_gslt_provider_catalog_generation_v1.py
GSLT_PROVIDER_CANARY_CATALOG_V1 = tests/fixtures/gslt_provider_canary_v1/provider_catalog_v1.metta
GSLT_PROVIDER_CANARY_CATALOG_GENERATED_H = tests/generated/gslt_provider_canary_catalog_v1.generated.h
GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C = tests/generated/gslt_provider_canary_catalog_v1.generated.c
FINITE_HORN_REFLECTION_V1 = experiments/gslt2parse_foundation/presentations/reflection/finite_horn_reflection_v1.metta
OSLF_NATIVE_TYPE_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/oslf_native_type_compiler_v1.metta
SEMANTIC_MASK_SPAN_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/semantic_mask_span_compiler_v1.metta
PARSER_OCCURRENCE_SPAN_MASK_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/parser_occurrence_span_mask_compiler_v1.metta
PROOF_GSLT_FIRST_ORDER_DENOTATION_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_first_order_denotation_v1.metta
RULE_MACHINE_CORE_GSLT_V1 = experiments/gslt2parse_foundation/presentations/core/rule_machine_core_v1.metta
RULE_MACHINE_PROGRAM_GSLT_V1 = experiments/gslt2parse_foundation/presentations/specializations/rule_machine_hilbert_bfc_program_v1.metta
RULE_MACHINE_PROGRAM_GENERATOR_V1 = tools/generate_rule_machine_program_v1.py
RULE_MACHINE_PROGRAM_GENERATED_V1 = src/generated/rule_machine_program_v1.generated.h
SUBZERO_FREE_BAG_CORE_V1 = langdef/subzero/semantics/free_bag_rewrite_core_v1.metta
SUBZERO_ONE_STEP_OBSERVATION_V1 = langdef/subzero/semantics/one_step_observation_v1.metta
SUBZERO_PUBLIC_RESULT_BAG_V1 = langdef/subzero/semantics/public_result_bag_v1.metta
SUBZERO_FREE_BAG_TEST_V1 = tools/test_subzero_free_bag_v1.py
MATCH_DECISION_POLICY_GSLT_V1 = experiments/gslt2parse_foundation/presentations/core/match_decision_policy_v1.metta
MATCH_DECISION_POLICY_GENERATOR_V1 = tools/generate_match_decision_policy_v1.py
MATCH_DECISION_POLICY_GENERATED_V1 = src/generated/match_decision_policy_v1.generated.h
GSLT2PARSE_PARSER_PACK_ABI_V1_NATIVE_BIN = runtime/test_parser_pack_abi_v1-$(BUILD_OBJ_TAG)
GSLT2PARSE_PARSER_PACK_ABI_V1_STREAM_BIN = runtime/test_parser_pack_abi_v1_stream-$(BUILD_OBJ_TAG)
LANGDEF_COMPILER_V1_SRC = tools/langdef_compile_v1.c
LANGDEF_COMPILER_V1_OBJ = runtime/bootstrap/langdef_compile_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
LANGDEF_COMPILER_V1_BIN = runtime/langdef-compile-v1-$(BUILD_OBJ_TAG)
GSLT_RULE_MUTATOR_V1_SRC = tools/gslt_rule_mutate_v1.c
GSLT_RULE_MUTATOR_V1_BIN = runtime/gslt-rule-mutate-v1-$(BUILD_OBJ_TAG)
LANGDEF_FINITE_HORN_GSLT_V1_OBJ = runtime/bootstrap/langdef_finite_horn_gslt_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
LANGDEF_METTA_EQUATION_COMPILER_V1_OBJ = runtime/bootstrap/langdef_metta_equation_compiler_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
LANGDEF_ARTIFACT_V1_OBJ = runtime/bootstrap/langdef_artifact_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
LANGDEF_PARSER_V1_OBJ = runtime/bootstrap/langdef_parser_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
LANGDEF_COMPILER_V1_LINK_OBJ = \
	src/symbol.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/atom.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/name_key.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	src/native_sha256.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/finite_horn_ground_term_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(FINITE_HORN_ANSWER_STREAM_V1_OBJ) \
	experiments/gslt2parse_foundation/native/parser_pack_abi_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o \
	$(PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_OBJ) \
	$(LANGDEF_PARSER_V1_OBJ) \
	$(LANGDEF_ARTIFACT_V1_OBJ)
GSLT2PARSE_TERM_PROJECTION_V1_NATIVE_BIN = runtime/test_parser_term_projection_v1-$(BUILD_OBJ_TAG)
GSLT2PARSE_TERM_PROJECTION_V1_STREAM_BIN = runtime/parser_term_projection_v1_stream-$(BUILD_OBJ_TAG)
GSLT2PARSE_ATOM_PROJECTION_V1_NATIVE_BIN = runtime/test_parser_atom_projection_v1-$(BUILD_OBJ_TAG)
GSLT2PARSE_COMPILER_ROOT ?= $(CURDIR)/experiments/gslt2parse_foundation/prolog
GSLT2PARSE_PARSER_PACK_COMPILER_SOURCES = \
	$(GSLT2PARSE_COMPILER_ROOT)/parser_pack_abi_export_v1.pl \
	$(GSLT2PARSE_COMPILER_ROOT)/parser_pack_eval.pl \
	$(GSLT2PARSE_COMPILER_ROOT)/parser_pack_action.pl \
	$(GSLT2PARSE_COMPILER_ROOT)/parser_pack_reference_compile.pl \
	$(GSLT2PARSE_COMPILER_ROOT)/finite_horn_gslt_v1.pl \
	$(GSLT2PARSE_COMPILER_ROOT)/finite_horn_eval.pl
GSLT2PARSE_PETTA_ROOT ?=
PETTA_ORACLE_ROOT ?= $(GSLT2PARSE_PETTA_ROOT)
PETTA_CORPUS_MANIFEST = tests/petta/corpus/manifest.json
PETTA_TYPECHECK_V2_MANIFEST = tests/petta/typecheck_v2_acceptance_manifest.json
PETTA_TYPECHECK_V2_OMISSION_BIN = runtime/cetta-$(BUILD_CANON)-no-petta-typecheck-v2
PETTA_CORPUS_RESULTS ?= runtime/petta-corpus-differential
PETTA_NATIVE_CORE_RESULTS ?= runtime/petta-corpus-native-core-no-libpl
PETTA_CORPUS_TIMEOUT ?= 30
PETTA_CHAINER_ROOT ?=
PETTA_CHAINER_COMPAT_MANIFEST = tests/petta/chainer_compat/manifest.json
PETTA_CHAINER_COMPAT_RESULTS ?= runtime/petta-chainer-compat
MATCH_DECISION_LANE_TIMEOUT ?= 60
GSLT2PARSE_HE_ROOT ?=
GSLT2PARSE_HE_GENERATED_C_OUTPUT ?=
GSLT2PARSE_HE_CURSOR_GENERATED_C = src/generated/he_reader_cursor_v1.generated.c
GSLT2PARSE_HE_PROJECTION_GENERATED_H = src/generated/cetta_he_projection_v1.generated.h
GSLT2PARSE_HE_DIRECT_GENERATED_C = src/generated/he_reader_direct_v1.generated.c
GSLT2PARSE_HE_DIRECT_GENERATED_H = src/generated/he_reader_direct_v1.generated.h
GSLT2PARSE_HE_PROJECTION_SOURCE = experiments/gslt2parse_foundation/presentations/shared/cetta_he_atom_projection_v1.metta
GSLT2PARSE_HE_READER_SOURCE = experiments/gslt2parse_foundation/presentations/languages/he_reader_v1.metta
GSLT2PARSE_HE_SCALAR_SOURCE = experiments/gslt2parse_foundation/presentations/shared/he_reader_scalar_classes_v1.metta
GSLT2PARSE_PETTA_DIRECT_GENERATED_C = src/generated/petta_reader_direct_v1.generated.c
GSLT2PARSE_PETTA_DIRECT_GENERATED_H = src/generated/petta_reader_direct_v1.generated.h
GSLT2PARSE_PETTA_PROJECTION_SOURCE = experiments/gslt2parse_foundation/presentations/shared/cetta_petta_atom_projection_v1.metta
GSLT2PARSE_PETTA_SPLITTER_SOURCE = experiments/gslt2parse_foundation/presentations/languages/petta_document_splitter_v1.metta
GSLT2PARSE_PETTA_SPLITTER_SCALAR_SOURCE = experiments/gslt2parse_foundation/presentations/shared/petta_document_splitter_scalar_classes_v1.metta
GSLT2PARSE_PETTA_FORM_SOURCE = experiments/gslt2parse_foundation/presentations/languages/petta_form_reader_v1.metta
GSLT2PARSE_PETTA_FORM_SCALAR_SOURCE = experiments/gslt2parse_foundation/presentations/shared/petta_form_reader_scalar_classes_v1.metta
GSLT2PARSE_PRIME_DIRECT_GENERATED_C = src/generated/prime_reader_direct_v1.generated.c
GSLT2PARSE_PRIME_DIRECT_GENERATED_H = src/generated/prime_reader_direct_v1.generated.h
GSLT2PARSE_PRIME_PROJECTION_SOURCE = experiments/gslt2parse_foundation/presentations/shared/cetta_prime_atom_projection_v1.metta
GSLT2PARSE_PRIME_READER_SOURCE = experiments/gslt2parse_foundation/presentations/languages/cetta_prime_reader_v1.metta
GSLT2PARSE_PRIME_SCALAR_SOURCE = experiments/gslt2parse_foundation/presentations/shared/cetta_prime_scalar_classes_v1.metta
PROOF_GSLT_ARTICLE_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_article_core_v1.metta
PROOF_GSLT_ARTICLE_INTERFACE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_article_interface_v1.metta
PROOF_GSLT_ARTICLE_DOCUMENT_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_article_document_v1.metta
PROOF_GSLT_ARTICLE_DOCUMENT_INTERFACE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_article_document_interface_v1.metta
PROOF_GSLT_ARTICLE_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_article_compiler_v1.metta
PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_article_document_compiler_v1.metta
PROOF_GSLT_SEQUENCE_RELATIONS_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_sequence_relations_v1.metta
PROOF_GSLT_SEQUENCE_SCHEMA_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_sequence_assertion_v1.metta
PROOF_GSLT_TRACE_SEMANTICS_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_trace_semantics_v1.metta
PROOF_GSLT_FIRST_ORDER_SEMANTIC_INTERFACE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_first_order_semantic_interface_v1.metta
PROOF_GSLT_TRACE_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_trace_compiler_v1.metta
PROOF_GSLT_TRACE_INPUT_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_trace_input_v1.metta
PROOF_GSLT_TRACE_INPUT_INTERFACE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_trace_input_interface_v1.metta
PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_sequence_evidence_compiler_v1.metta
PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_relational_assertion_v1.metta
PROOF_GSLT_RELATIONAL_ASSERTION_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_relational_assertion_compiler_v1.metta
PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_relational_projection_v1.metta
PROOF_GSLT_RELATIONAL_PROJECTION_INTERFACE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_relational_projection_interface_v1.metta
PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_relational_projection_compiler_v1.metta
PROOF_GSLT_RELATIONAL_RUNTIME_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_gslt_relational_runtime_compiler_v1.metta
PROOF_STORAGE_PLAN_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/proof_storage_plan_compiler_v1.metta
PROOF_GSLT_METAMATH_CALCULUS_V1 = \
	$(METAMATH_LANGDEF_DIR_V1)/proof_calculus_v1.metta
PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1 = \
	$(METAMATH_LANGDEF_DIR_V1)/proof_relational_bridge_v1.metta
PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1 = \
	$(METAMATH_LANGDEF_DIR_V1)/proof_relational_projection_v1.metta
PROOF_GSLT_PROP_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_prop_v1.metta
PROOF_GSLT_EVEN_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_even_v1.metta
PROOF_GSLT_BINDER_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_binder_v1.metta
PROOF_GSLT_SEQUENCE_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_sequence_v1.metta
PROOF_GSLT_TRACE_ORDER_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_trace_order_v1.metta
PROOF_GSLT_TRACE_CODEC_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_trace_codec_v1.metta
PROOF_GSLT_TRACE_INPUT_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_trace_input_v1.metta
PROOF_GSLT_TRACE_SERVICE_V1 = experiments/gslt2parse_foundation/presentations/core/proof_gslt_trace_service_v1.metta
PROOF_GSLT_TRACE_EXECUTION_COMPOSITION_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_trace_execution_composition_v1.metta
PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_trace_compressed_composition_v1.metta
PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/proof_gslt_relational_projection_v1.metta
PROOF_GSLT_TRACE_ORDER_POSITIVE_V1 = tests/langdef/metamath/proof_trace_order_positive.query
PROOF_GSLT_TRACE_ORDER_SWAPPED_V1 = tests/langdef/metamath/proof_trace_order_swapped.query
PROOF_GSLT_TRACE_ORDER_MISSING_CONTEXT_V1 = tests/langdef/metamath/proof_trace_order_missing_context.query
PROOF_GSLT_TRACE_SAVED_POSITIVE_V1 = tests/langdef/metamath/proof_trace_saved_positive.query
PROOF_GSLT_TRACE_SAVED_RANGE_V1 = tests/langdef/metamath/proof_trace_saved_range.query
PROOF_GSLT_TRACE_COMPILE_NORMAL_V1 = tests/langdef/metamath/proof_trace_compile_normal.query
PROOF_GSLT_TRACE_COMPILE_COMPRESSED_V1 = tests/langdef/metamath/proof_trace_compile_compressed.query
PROOF_GSLT_TRACE_COMPILE_CONTINUATION_V1 = tests/langdef/metamath/proof_trace_compile_continuation.query
PROOF_GSLT_TRACE_COMPILE_OPEN_SAVE_V1 = tests/langdef/metamath/proof_trace_compile_open_save.query
PROOF_GSLT_TRACE_COMPILE_UNKNOWN_V1 = tests/langdef/metamath/proof_trace_compile_unknown.query
PROOF_GSLT_TRACE_COMPILE_ACCEPTED_V1 = tests/langdef/metamath/proof_trace_compile_accepted.query
PROOF_GSLT_TRACE_INPUT_CONTEXT_V1 = tests/langdef/metamath/proof_trace_input_context.query
PROOF_GSLT_TRACE_INPUT_FRAME_V1 = tests/langdef/metamath/proof_trace_input_frame.query
PROOF_GSLT_TRACE_INPUT_NORMAL_V1 = tests/langdef/metamath/proof_trace_input_normal.query
PROOF_GSLT_TRACE_INPUT_COMPRESSED_V1 = tests/langdef/metamath/proof_trace_input_compressed.query
PROOF_GSLT_TRACE_INPUT_INCOMPLETE_NORMAL_V1 = tests/langdef/metamath/proof_trace_input_incomplete_normal.query
PROOF_GSLT_TRACE_INPUT_INCOMPLETE_COMPRESSED_V1 = tests/langdef/metamath/proof_trace_input_incomplete_compressed.query
PROOF_GSLT_TRACE_INPUT_UNKNOWN_NOT_VERIFIED_V1 = tests/langdef/metamath/proof_trace_input_unknown_not_verified.query
PROOF_GSLT_TRACE_INPUT_WRONG_TARGET_V1 = tests/langdef/metamath/proof_trace_input_wrong_target.query
PROOF_GSLT_TRACE_INPUT_BAD_COUNT_V1 = tests/langdef/metamath/proof_trace_input_bad_count.query
PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_POSITIVE_V1 = tests/langdef/metamath/proof_trace_compressed_composition_positive.query
PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_RANGE_V1 = tests/langdef/metamath/proof_trace_compressed_composition_range.query
PROOF_GSLT_RELATIONAL_PROJECTION_CONTEXT_V1 = tests/langdef/metamath/proof_relational_projection_context.query
PROOF_GSLT_RELATIONAL_PROJECTION_FRAME_V1 = tests/langdef/metamath/proof_relational_projection_frame.query
PROOF_GSLT_RELATIONAL_PROJECTION_FILTERED_V1 = tests/langdef/metamath/proof_relational_projection_filtered.query
PROOF_GSLT_RELATIONAL_PROJECTION_ASSERTION_V1 = tests/langdef/metamath/proof_relational_projection_assertion.query
PROOF_GSLT_RELATIONAL_PROJECTION_PUSH_V1 = tests/langdef/metamath/proof_relational_projection_push.query
PROOF_GSLT_RELATIONAL_PROJECTION_NO_APARTNESS_V1 = tests/langdef/metamath/proof_relational_projection_no_apartness.query
PROOF_GSLT_STAGE_DIR_V1 = runtime/bootstrap/proof-gslt-article-v1.$(BUILD_OBJ_TAG)
PROOF_GSLT_PROP_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/prop.answers
PROOF_GSLT_EVEN_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/even.answers
PROOF_GSLT_BINDER_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/binder.answers
PROOF_GSLT_SEQUENCE_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence.answers
PROOF_GSLT_SEQUENCE_EVIDENCE_ABI_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-evidence-abi.answers
PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_SOURCE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-delete-abi-role.metta
PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-delete-abi-role.answers
PROOF_GSLT_PROP_DELETE_MP_SOURCE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/prop-delete-mp.metta
PROOF_GSLT_PROP_DELETE_MP_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/prop-delete-mp.answers
PROOF_GSLT_SEQUENCE_DELETE_APART_SOURCE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-delete-apart.metta
PROOF_GSLT_SEQUENCE_DELETE_APART_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-delete-apart.answers
PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_SOURCE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-delete-assertion.metta
PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_ANSWERS_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/sequence-delete-assertion.answers
PROOF_GSLT_METAMATH_PLAN_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_plan_v1.answers
PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_evidence_abi_v1.answers
PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_relational_abi_v1.answers
PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_relational_projection_v1.answers
PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_relational_runtime_v1.answers
PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_SOURCE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/metamath-relational-delete-role.metta
PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_V1 = $(PROOF_GSLT_STAGE_DIR_V1)/metamath-relational-delete-role.answers
TPTP_LANGDEF_MANIFEST_V1 = langdef/tptp/langdef.metta
TPTP_LANGDEF_SYNTAX_V1 = langdef/tptp/syntax_fof_cnf_v1.metta
TPTP_LANGDEF_ATP_BRIDGE_V1 = langdef/tptp/atp_bridge_v1.metta
TPTP_LANGDEF_ATP_PROGRAM_V1 = langdef/tptp/generated/atp_bridge_v1.generated.metta
TPTP_LANGDEF_PARSER_PACK_V1 = langdef/tptp/generated/parser_pack_v1.abi
TPTP_LANGDEF_LOCK_V1 = langdef/tptp/generated/langdef_lock_v1.metta
TPTP_LANGDEF_PARSER_SOURCES_V1 = \
	experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
	experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
	experiments/gslt2parse_foundation/presentations/shared/char_core_v1.metta \
	$(TPTP_LANGDEF_SYNTAX_V1)
METAMATH_LANGDEF_DIR_V1 = langdef/metamath
METAMATH_LANGDEF_GENERATED_DIR_V1 = $(METAMATH_LANGDEF_DIR_V1)/generated
METAMATH_SYNTAX_MANIFEST_V1 = $(METAMATH_LANGDEF_DIR_V1)/syntax_pack_v1.metta
METAMATH_SYNTAX_SOURCE_V1 = $(METAMATH_LANGDEF_DIR_V1)/syntax_v1.metta
METAMATH_LEXICAL_PROJECTION_V1 = $(METAMATH_LANGDEF_DIR_V1)/lexical_projection_v1.metta
METAMATH_SOURCE_FOLD_V1 = $(METAMATH_LANGDEF_DIR_V1)/source_fold_v1.metta
METAMATH_SOURCE_PACK_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/syntax_parser_pack_v1.abi
METAMATH_SYNTAX_LOCK_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/syntax_lock_v1.metta
METAMATH_NORMALIZED_PACK_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/normalized_parser_pack_v1.abi
METAMATH_SOURCE_PACK_FACTS_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/source_parser_pack_facts_v1.metta
METAMATH_NORMALIZED_PACK_FACTS_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/normalized_parser_pack_facts_v1.metta
METAMATH_LEXICAL_ROOTS_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/lexical_roots_v1.answers
METAMATH_LEXICAL_NFA_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/lexical_nfa_v1.answers
METAMATH_EMPTY_GUARD_ANSWERS_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/empty_guard_v1.answers
METAMATH_EMPTY_GUARD_EVIDENCE_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/empty_guard_v1.evidence
METAMATH_ACTION_BYTECODE_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/action_bytecode_v1.answers
METAMATH_OCCURRENCE_FOLD_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/occurrence_fold_v1.answers
METAMATH_OCCURRENCE_SPAN_MASK_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/occurrence_span_mask_v1.answers
METAMATH_STATE_PROGRAM_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/state_program_v1.answers
METAMATH_CURSOR_FOLD_GENERATED_C_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/syntax_cursor_fold_v1.generated.c
METAMATH_CURSOR_FOLD_PREFIX_V1 = metamath_syntax_cursor_fold_v1
METAMATH_COMPILED_CURSOR_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/syntax_cursor_fold_v1.generated.so
METAMATH_LANGDEF_MANIFEST_V1 = $(METAMATH_LANGDEF_DIR_V1)/langdef.metta
METAMATH_LANGDEF_LOCK_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/langdef_lock_v1.metta
METAMATH_AUTHORING_COMPOSITION_V1 = $(METAMATH_LANGDEF_DIR_V1)/authoring_composition_v1.metta
METAMATH_AUTHORING_NTT_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/authoring_ntt_v1.answers
METAMATH_PROOF_SEMANTIC_PAYLOAD_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_semantic_payload_v1.metta
METAMATH_PROOF_SEMANTIC_GSLT_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_semantic_gslt_v1.metta
METAMATH_PROOF_SEMANTIC_EXEC_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_semantic_exec_v1.metta
METAMATH_PROOF_SEMANTIC_NTT_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_semantic_ntt_v1.answers
METAMATH_PROOF_MACHINE_COMPOSITION_V1 = $(METAMATH_LANGDEF_DIR_V1)/proof_machine_composition_v1.metta
METAMATH_PROOF_TRACE_CODEC_V1 = $(METAMATH_LANGDEF_DIR_V1)/proof_trace_codec_v1.metta
METAMATH_PROOF_MACHINE_NTT_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_machine_ntt_v1.answers
METAMATH_PROOF_MACHINE_NTT_V1_SOURCES = \
	$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
	$(PROOF_GSLT_ARTICLE_CORE_V1) \
	$(PROOF_GSLT_ARTICLE_INTERFACE_V1) \
	$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
	$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_SOURCE_FOLD_V1) \
	$(METAMATH_RELATIONAL_STATE_CORE_V1) \
	$(METAMATH_SOURCE_STATE_V1) \
	$(METAMATH_SOURCE_PROOF_V1) \
	$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
	$(PROOF_GSLT_TRACE_COMPILER_V1) \
	$(PROOF_GSLT_TRACE_INPUT_V1) \
	$(PROOF_GSLT_TRACE_INPUT_INTERFACE_V1) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_INTERFACE_V1) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
	$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
	$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
	$(METAMATH_PROOF_TRACE_CODEC_V1)
METAMATH_PROOF_MACHINE_CAPABILITY_CANARY_V1 = experiments/gslt2parse_foundation/presentations/canaries/metamath_proof_machine_capability_v1.metta
METAMATH_PROOF_MACHINE_CAPABILITY_V1 = runtime/bootstrap/metamath-proof-machine-capability-v1.$(BUILD_OBJ_TAG)/reflection.answers
METAMATH_PROOF_MACHINE_CAPABILITY_DELETED_V1 = runtime/bootstrap/metamath-proof-machine-capability-v1.$(BUILD_OBJ_TAG)/reflection-deleted.answers
METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES = \
	$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
	$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
	$(PROOF_GSLT_TRACE_COMPILER_V1) \
	$(PROOF_GSLT_TRACE_INPUT_V1) \
	$(PROOF_GSLT_TRACE_INPUT_INTERFACE_V1) \
	$(METAMATH_PROOF_TRACE_CODEC_V1)
METAMATH_PROOF_MACHINE_COMPILED_V1_SOURCES = \
	$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES) \
	$(PROOF_GSLT_ARTICLE_CORE_V1) \
	$(PROOF_GSLT_ARTICLE_INTERFACE_V1) \
	$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
	$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_SOURCE_FOLD_V1) \
	$(METAMATH_RELATIONAL_STATE_CORE_V1) \
	$(METAMATH_SOURCE_PROOF_V1) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_INTERFACE_V1) \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
	$(METAMATH_SOURCE_STATE_V1) \
	$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
	$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
	$(PROOF_GSLT_TRACE_SERVICE_V1)
METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_DIR = runtime/bootstrap/metamath-proof-trace-native-v1.$(BUILD_OBJ_TAG)
METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_NTT = $(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_DIR)/ntt.answers
METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_V1 = $(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_DIR)/capability-reflection.answers
METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_DELETED_V1 = $(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_DIR)/capability-reflection-deleted.answers
METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1 = runtime/bootstrap/metamath-proof-semantic-v1.$(BUILD_OBJ_TAG)
METAMATH_PROOF_SEMANTIC_OPERATOR_PART_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/operator.answers
METAMATH_PROOF_SEMANTIC_RULE_PART_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/rule.answers
METAMATH_PROOF_SEMANTIC_REFLECTION_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/reflection.answers
METAMATH_PROOF_SEMANTIC_REQUIRED_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/required.answers
METAMATH_PROOF_SEMANTIC_DENOTED_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/denoted.answers
METAMATH_PROOF_SEMANTIC_REQUIRED_KEYS_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/required-keys.answers
METAMATH_PROOF_SEMANTIC_DENOTED_KEYS_V1 = $(METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1)/denoted-keys.answers
METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_storage_analysis_payload_v1.metta
METAMATH_PROOF_STORAGE_PLAN_V1 = $(METAMATH_LANGDEF_GENERATED_DIR_V1)/proof_storage_plan_v1.answers
METAMATH_PROOF_STORAGE_STAGE_DIR_V1 = runtime/bootstrap/metamath-proof-storage-v1.$(BUILD_OBJ_TAG)
METAMATH_PROOF_STORAGE_COMBINED_V1 = $(METAMATH_PROOF_STORAGE_STAGE_DIR_V1)/combined.answers
METAMATH_PROOF_STORAGE_REQUIRED_V1 = $(METAMATH_PROOF_STORAGE_STAGE_DIR_V1)/required.answers
METAMATH_PROOF_STORAGE_DENOTED_V1 = $(METAMATH_PROOF_STORAGE_STAGE_DIR_V1)/denoted.answers
METAMATH_PROOF_STORAGE_REQUIRED_KEYS_V1 = $(METAMATH_PROOF_STORAGE_STAGE_DIR_V1)/required-keys.answers
METAMATH_PROOF_STORAGE_DENOTED_KEYS_V1 = $(METAMATH_PROOF_STORAGE_STAGE_DIR_V1)/denoted-keys.answers
LANGDEF_COMPILED_CURSOR_EXPORT_V1 = native/langdef_compiled_cursor_export_v1.c
LANGDEF_COMPILED_CURSOR_HEADER_V1 = native/langdef_compiled_cursor_v1.h
METAMATH_CURSOR_FOLD_TEST_BIN_V1 = runtime/test_metamath_syntax_cursor_fold_v1-$(BUILD_OBJ_TAG)
METAMATH_FOLD_PART_DIR_V1 = runtime/bootstrap/metamath-occurrence-fold-v1.$(BUILD_OBJ_TAG)
METAMATH_FOLD_PRODUCTION_PART_V1 = $(METAMATH_FOLD_PART_DIR_V1)/production.answers
METAMATH_FOLD_SHIFT_PART_V1 = $(METAMATH_FOLD_PART_DIR_V1)/shift.answers
METAMATH_FOLD_TOKEN_PART_V1 = $(METAMATH_FOLD_PART_DIR_V1)/token.answers
METAMATH_FOLD_VALUE_TOKEN_PART_V1 = $(METAMATH_FOLD_PART_DIR_V1)/value-token.answers
METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1 = runtime/bootstrap/metamath-occurrence-span-mask-v1.$(BUILD_OBJ_TAG)
METAMATH_OCCURRENCE_SPAN_MASK_FOLD_FACTS_V1 = $(METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1)/fold-facts.metta
METAMATH_OCCURRENCE_SPAN_MASK_NFA_WRAPPER_V1 = $(METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1)/nfa-wrapper.answers
METAMATH_OCCURRENCE_SPAN_MASK_NFA_PART_V1 = $(METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1)/nfa.answers
METAMATH_OCCURRENCE_SPAN_MASK_LINK_WRAPPER_V1 = $(METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1)/link-wrapper.answers
METAMATH_OCCURRENCE_SPAN_MASK_LINK_PART_V1 = $(METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1)/link.answers
METAMATH_STATE_PART_DIR_V1 = runtime/bootstrap/metamath-state-v1.$(BUILD_OBJ_TAG)
METAMATH_STATE_TABLE_PART_V1 = $(METAMATH_STATE_PART_DIR_V1)/table.answers
METAMATH_STATE_PROOF_MACHINE_PART_V1 = $(METAMATH_STATE_PART_DIR_V1)/proof-machine.answers
METAMATH_STATE_ACTION_PART_V1 = $(METAMATH_STATE_PART_DIR_V1)/action.answers
METAMATH_STATE_FINAL_PART_V1 = $(METAMATH_STATE_PART_DIR_V1)/final.answers
METAMATH_SYNTAX_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta
METAMATH_LOOKAHEAD_CORE_V1 = experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta
METAMATH_SYNTAX_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/syntax_compiler_v1.metta
METAMATH_REGULAR_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/regular_span_compiler_v1.metta
METAMATH_GUARDED_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/guarded_regular_span_compiler_v1.metta
METAMATH_LEXICAL_PROJECTION_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/lexical_projection_core_v1.metta
METAMATH_LEXICAL_PROJECTION_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/lexical_projection_compiler_v1.metta
METAMATH_ACTION_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/parser_action_bytecode_compiler_v1.metta
METAMATH_OCCURRENCE_FOLD_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/occurrence_fold_core_v1.metta
METAMATH_OCCURRENCE_FOLD_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/parser_occurrence_fold_compiler_v1.metta
METAMATH_OCCURRENCE_FOLD_PLAN_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/parser_occurrence_fold_plan_compiler_v1.metta
METAMATH_RELATIONAL_STATE_CORE_V1 = experiments/gslt2parse_foundation/presentations/core/relational_state_program_core_v1.metta
METAMATH_RELATIONAL_STATE_COMPILER_V1 = experiments/gslt2parse_foundation/presentations/compiler/relational_state_program_compiler_v1.metta
METAMATH_SOURCE_STATE_V1 = $(METAMATH_LANGDEF_DIR_V1)/state_v1.metta
METAMATH_SOURCE_PROOF_V1 = $(METAMATH_LANGDEF_DIR_V1)/proof_v1.metta
METAMATH_COGSLT_AUTHORED_RULE_SOURCES_V1 = \
	$(METAMATH_SYNTAX_SOURCE_V1) \
	$(METAMATH_LEXICAL_PROJECTION_V1) \
	$(METAMATH_SOURCE_FOLD_V1) \
	$(METAMATH_SOURCE_STATE_V1) \
	$(METAMATH_SOURCE_PROOF_V1) \
	$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
	$(PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1)
METAMATH_COGSLT_MUTATION_MODES_V1 ?= delete falsify
METAMATH_COGSLT_MUTATION_SOURCE_FILTER_V1 ?=
METAMATH_COGSLT_MUTATION_RULE_FILTER_V1 ?=
METAMATH_COGSLT_MUTATION_SHARD_COUNT_V1 ?= 1
METAMATH_COGSLT_MUTATION_SHARD_INDEX_V1 ?= 0
METAMATH_COGSLT_MUTATION_JOBS_V1 ?= 4
METAMATH_COGSLT_GENERATED_FORM_ARTIFACTS_V1 = \
	$(METAMATH_LEXICAL_NFA_V1) \
	$(METAMATH_ACTION_BYTECODE_V1) \
	$(METAMATH_OCCURRENCE_FOLD_V1) \
	$(METAMATH_OCCURRENCE_SPAN_MASK_V1) \
	$(METAMATH_STATE_PROGRAM_V1) \
	$(PROOF_GSLT_METAMATH_PLAN_V1)
METAMATH_COGSLT_GENERATED_MUTATION_MODES_V1 ?= delete falsify
METAMATH_COGSLT_GENERATED_MUTATION_ARTIFACT_FILTER_V1 ?=
METAMATH_COGSLT_GENERATED_MUTATION_FORM_FILTER_V1 ?=
METAMATH_COGSLT_GENERATED_MUTATION_SHARD_COUNT_V1 ?= 1
METAMATH_COGSLT_GENERATED_MUTATION_SHARD_INDEX_V1 ?= 0
METAMATH_COGSLT_GENERATED_MUTATION_JOBS_V1 ?= 4
METAMATH_SET_MM_V1 ?=
METAMATH_SET_MM_SHA256_V1 = 6ecbc6c3c8c4beb42f905a49f6442be16087b606eed6933b41b3c930a078f18b
METAMATH_SET_MM_IMPORT_COUNT_V1 = 274071
METAMATH_COGSLT_CORE_BIN_V1 = runtime/cetta-metamath-core-v1
METAMATH_COGSLT_DIAGNOSTIC_BIN_V1 = runtime/cetta-metamath-diagnostic-$(BUILD_OBJ_TAG)
METAMATH_TEST_ROOT_V1 ?=
METAMATH_MMLEAN4_ROOT_V1 ?=
METAMATH_MMLEAN4_BIN_V1 ?=
METAMATH_MMLEAN4_COMMIT_V1 = deaf4bfcfc9e9b75fe41b2f1366034d1542862cd
LANGDEF_IMPORT_VERDICT_DRIVER_V1 = tools/langdef_import_verdict_v1.sh
LANGDEF_PROOF_VERDICT_DRIVER_V1 = tools/langdef_proof_verdict_v1.sh
METAMATH_REGULAR_COMPILER_DIGEST_V1 = $(shell sha256sum $(METAMATH_REGULAR_COMPILER_V1) | cut -d' ' -f1)
METAMATH_GUARDED_COMPILER_DIGEST_V1 = $(shell sha256sum $(METAMATH_GUARDED_COMPILER_V1) | cut -d' ' -f1)
METAMATH_ACTION_COMPILER_DIGEST_V1 = $(shell sha256sum $(METAMATH_ACTION_COMPILER_V1) | cut -d' ' -f1)
METAMATH_OCCURRENCE_SPAN_MASK_COMPILER_DIGEST_V1 = $(shell (printf '%s\n' ParserOccurrenceSpanMaskCompilerV1; sha256sum $(SEMANTIC_MASK_SPAN_COMPILER_V1) $(PARSER_OCCURRENCE_SPAN_MASK_COMPILER_V1)) | sha256sum | cut -d' ' -f1)
METAMATH_REGULAR_SOURCES_V1 = \
	$(METAMATH_SYNTAX_CORE_V1) \
	$(METAMATH_LOOKAHEAD_CORE_V1) \
	$(METAMATH_SYNTAX_COMPILER_V1) \
	$(METAMATH_SYNTAX_SOURCE_V1) \
	$(METAMATH_REGULAR_COMPILER_V1)
METAMATH_FOLD_SOURCES_V1 = \
	$(METAMATH_REGULAR_SOURCES_V1) \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_SOURCE_FOLD_V1) \
	$(METAMATH_ACTION_COMPILER_V1) \
	$(METAMATH_SOURCE_PACK_FACTS_V1) \
	$(METAMATH_NORMALIZED_PACK_FACTS_V1) \
	$(METAMATH_OCCURRENCE_FOLD_COMPILER_V1) \
	$(METAMATH_OCCURRENCE_FOLD_PLAN_COMPILER_V1)
METAMATH_OCCURRENCE_SPAN_MASK_SOURCES_V1 = \
	$(METAMATH_SYNTAX_CORE_V1) \
	$(METAMATH_LOOKAHEAD_CORE_V1) \
	$(FINITE_HORN_REFLECTION_V1) \
	$(METAMATH_SYNTAX_COMPILER_V1) \
	$(METAMATH_SYNTAX_SOURCE_V1) \
	$(SEMANTIC_MASK_SPAN_COMPILER_V1) \
	$(METAMATH_OCCURRENCE_SPAN_MASK_FOLD_FACTS_V1) \
	$(PARSER_OCCURRENCE_SPAN_MASK_COMPILER_V1)
METAMATH_STATE_SOURCES_V1 = \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_SOURCE_FOLD_V1) \
	$(METAMATH_RELATIONAL_STATE_CORE_V1) \
	$(METAMATH_SOURCE_STATE_V1) \
	$(METAMATH_SOURCE_PROOF_V1) \
	$(METAMATH_RELATIONAL_STATE_COMPILER_V1)
METAMATH_AUTHORING_COMPOSED_SOURCES_V1 = \
	$(METAMATH_SYNTAX_CORE_V1) \
	$(METAMATH_LOOKAHEAD_CORE_V1) \
	$(METAMATH_LEXICAL_PROJECTION_CORE_V1) \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_RELATIONAL_STATE_CORE_V1) \
	$(METAMATH_SYNTAX_SOURCE_V1) \
	$(METAMATH_LEXICAL_PROJECTION_V1) \
	$(METAMATH_SOURCE_FOLD_V1) \
	$(METAMATH_SOURCE_STATE_V1) \
	$(METAMATH_SOURCE_PROOF_V1) \
	$(PROOF_GSLT_ARTICLE_CORE_V1) \
	$(PROOF_GSLT_ARTICLE_INTERFACE_V1) \
	$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
	$(PROOF_GSLT_ARTICLE_DOCUMENT_INTERFACE_V1) \
	$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
	$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
	$(PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1) \
	$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
	$(PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1)
GSLT2PARSE_GENERATED_CHECK_DIR = runtime/gslt2parse-generated-check
GSLT2PARSE_GENERIC_ENGINE_SOURCES = \
	$(filter-out $(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_%,$(wildcard $(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/*.c)) \
	$(filter-out $(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_%,$(wildcard $(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/*.h)) \
	src/lib_parse_native_grammar.c src/lib_parse_native_grammar.h \
	src/native_sha256.c src/native_sha256.h
GSLT2PARSE_GENERIC_COMPILER_SOURCES = \
	$(wildcard experiments/gslt2parse_foundation/presentations/compiler/*.metta) \
	$(wildcard experiments/gslt2parse_foundation/presentations/parserpack/*.metta) \
	$(wildcard experiments/gslt2parse_foundation/presentations/reflection/*.metta)
SPACE_ENGINES = native native-candidate-exact
ifeq ($(ENABLE_PATHMAP_SPACE),1)
SPACE_ENGINES += pathmap
endif
D4_PROBE_TIMEOUT ?= 60
SANITIZER_REPEATABLE := 0
ifeq ($(ENABLE_SANITIZERS),1)
ifneq ($(filter address thread,$(SANITIZER_WORDS)),)
SANITIZER_REPEATABLE := 1
endif
endif
CETTA_EXEC_WRAPPER := ./scripts/cetta_exec.sh
# Under sanitizer-repeatable mode the invocation carries a per-call env prefix
# (CETTA_WRAPPED_BIN selects the binary, so it cannot be dropped in favour of the
# global export -- variant test bins pass their own $1).  Lead with `env` so the
# whole expansion is a plain command that `timeout`/`/usr/bin/time` wrappers can
# exec directly; without it those wrappers try to exec "CETTA_...=1" as a program.
define cetta_exec
$(if $(filter 1,$(SANITIZER_REPEATABLE)),env CETTA_SANITIZER_REPEATABLE=1 CETTA_WRAPPED_BIN=$1 $(CETTA_EXEC_WRAPPER),$1)
endef
CETTA_BIN_INVOKE = $(call cetta_exec,./$(BIN))
METAMATH_COGSLT_DIAGNOSTIC_BIN_INVOKE = $(call cetta_exec,./$(METAMATH_COGSLT_DIAGNOSTIC_BIN_V1))
ifeq ($(SANITIZER_REPEATABLE),1)
export CETTA_SANITIZER_REPEATABLE := 1
export CETTA_WRAPPED_BIN := $(abspath $(BIN))
ifneq ($(filter address,$(SANITIZER_WORDS)),)
# A few integration recipes invoke the selected binary directly rather than
# through cetta_exec.sh.  Give those calls the same repeatable-ASan policy as
# the wrapper; leak qualification remains a separate, process-lifetime gate.
export ASAN_OPTIONS := $(if $(strip $(ASAN_OPTIONS)),$(ASAN_OPTIONS),detect_leaks=0)
endif
CETTA_SCRIPT_RUN_ENV = CETTA_SANITIZER_REPEATABLE=1 CETTA_BIN="$(abspath $(CETTA_EXEC_WRAPPER))" CETTA_WRAPPED_BIN="$(abspath $(BIN))"
CETTA_SCRIPT_BIN = $(abspath $(CETTA_EXEC_WRAPPER))
else
CETTA_SCRIPT_RUN_ENV = CETTA_BIN="$(abspath $(BIN))"
CETTA_SCRIPT_BIN = $(abspath $(BIN))
endif
GIT_TEST_FIXTURE_ROOT = $(CURDIR)/runtime/git_module_fixture
GIT_TEST_CACHE_DIR = $(CURDIR)/runtime/test-git-module-cache
GIT_TEST_URL = file://$(GIT_TEST_FIXTURE_ROOT)
GIT_TEST_DYNAMIC = $(CURDIR)/runtime/test-git-module-dynamic.metta
GIT_TEST_COMPAT_DYNAMIC = $(CURDIR)/runtime/test-git-module-compat.metta
HE_CONTRACT_GENERATED_DIR = tests/generated/he_contract
HE_COMPAT_GENERATED_DIR = tests/generated/he_compat
HE_COMPAT_CATALOG = $(HE_COMPAT_GENERATED_DIR)/he_compat_cases_2026-06-25.json
HE_NATIVE_CONTRACTS = $(HE_COMPAT_GENERATED_DIR)/he_native_contracts_2026-06-25.json
TEST_MANIFEST = tests/test_manifest.tsv
PRIME_CONFORMANCE_TESTS = \
	tests/prime_02_completion_resources.metta \
	tests/prime/need_application.metta \
	tests/prime/need_explicit_control.metta \
	tests/prime/need_explicit_control_negative.metta \
	tests/prime/need_demand_modes.metta \
	tests/prime/first_class_contexts.metta \
	tests/prime/need_gc_lifetime.metta \
	tests/prime/need_storage_boundary.metta \
	tests/prime/need_quote_preservation.metta \
	tests/prime/need_sequential_unification_refinement.metta \
	tests/prime/nik_plural_checking.metta \
	tests/prime/nil_rule_machine_guests.generated.metta \
	tests/prime/rule_machine_malformed_artifact_delta.metta \
	tests/prime/rule_machine_malformed_program.metta \
	tests/prime/prepared_match_decision.metta \
	tests/prime/prepared_pure_control_cache.metta \
	tests/prime/prepared_pure_shared_decision.metta \
	tests/prime/rule_machine_compile.metta \
	tests/prime/rule_machine_duplicate_id.metta \
	tests/prime/rule_machine_revision_identity.metta \
	tests/prime/rule_machine_stale_program.metta \
	tests/prime/system_timed_force.metta \
	tests/prime/conformance/abt_chain_scope.metta \
	tests/prime/conformance/abt_let_scope.metta \
	tests/prime/conformance/abt_sealed_boundary.metta \
	tests/prime/conformance/cell_lexical_refinement.metta \
	tests/prime/conformance/canonical_binders.metta \
	tests/prime/conformance/empty_observation.metta \
	tests/prime/conformance/resource_policy.metta \
	tests/prime/conformance/occurs_check.metta \
	tests/prime/conformance/syntax_algebra.metta \
	tests/prime/conformance/typed_equality.metta \
	tests/prime/conformance/unbounded_search.metta
PRIME_EXAMPLE_TESTS = \
	examples/prime/exact_gradual.metta \
	examples/prime/dependent_telescope.metta \
	examples/prime/nondeterministic_judgments.metta \
	examples/prime/context_tutorial.metta
PRIME_PRACTICAL_TESTS = \
	tests/prime/practical/typed_pln_chainer.metta \
	tests/prime/practical/atp_guided_inhabitation.metta \
	tests/prime/practical/atp_direct_library.metta \
	tests/prime/practical/atp_indexed_library.metta \
	tests/prime/practical/atp_resolution_replay.metta \
	tests/prime/practical/atp_resolution_search.metta \
	tests/prime/practical/atp_superposition_replay.metta \
	tests/prime/practical/atp_agenda_library.metta \
	tests/prime/practical/need_control_branch_discriminators.metta
PRIME_FAST_TESTS = $(PRIME_CONFORMANCE_TESTS) $(PRIME_EXAMPLE_TESTS) $(PRIME_PRACTICAL_TESTS)
# Per-test wall-clock cap for the prime conformance/completion gates.  A clean
# prime_02_completion_resources run is a few seconds; a pathological blow-up
# (e.g. the O(n^3) reify-judge regression) must fail LOUD instead of grinding
# for hours and wedging the whole verification sweep.
PRIME_COMPLETION_TIMEOUT ?= 60
PYTHON_TESTS = tests/test_py_ops_surface.metta tests/test_import_foreign_python_file.metta tests/test_import_foreign_pkg_error.metta tests/test_namespace_sugar_guardrails.metta
PATHMAP_REQUIRED_TESTS = \
	tests/test_space_type.metta \
	tests/test_space_engine_backend.metta \
	tests/test_add_atom_nodup_pathmap_alpha_regression.metta \
	tests/test_bigint_bridge_roundtrip_regression.metta \
	tests/test_rational_bridge_roundtrip_regression.metta \
	tests/test_import_act_module_surface.metta \
	tests/test_include_mm2_space_target.metta \
	tests/test_module_inventory_act_registered_root.metta \
	tests/test_mork_act_roundtrip.metta \
	tests/test_pathmap_counted_space_surface.metta \
	tests/test_pathmap_contextual_var_projection_remove.metta \
	tests/test_pathmap_indexed_query_work.metta \
	tests/test_pathmap_indexed_opening_identity_regression.metta \
	tests/test_space_batch_copy_optimizer_guards.metta \
	tests/test_pathmap_backend_primary_growth_regression.metta \
	tests/test_pathmap_fc_depth3_count_regression.metta \
	tests/test_pathmap_match_copy_var_identity_regression.metta \
	tests/test_pathmap_program_shadow_sync_work.metta \
	tests/test_pathmap_pull_consumers_work.metta \
	tests/test_pathmap_stored_variable_exact_query_regression.metta \
	tests/test_pathmap_typed_query_surface.metta \
	tests/test_match_chain_cross_space_pathmap_regression.metta \
	tests/test_effect_append_batch_fastpath.metta \
	tests/test_size_atom_collapse_cross_engine_regression.metta \
	tests/test_space_batch_copy_surfaces.metta \
	tests/test_rational_bridge_roundtrip_regression.metta \
	tests/test_mork_fc_depth3_witness_regression.metta \
	tests/test_mork_recursive_bc_micro_regression.metta \
	tests/test_mork_recursive_bc_regression.metta

PATHMAP_PROBE_TESTS = \
	tests/test_mork_nil_parity_regression.metta \
	tests/test_mm2_match_order_fragile.metta \
	tests/test_pathmap_backend_primary_destructive_regression.metta

CORE_PROBE_TESTS = \
	tests/test_print_nondet_probe.metta

# Empty is intentional; populate only for strict known-failing regressions.
CORE_XFAIL_TESTS =

RUNTIME_STATS_METTA_TESTS = \
	tests/spec_profile_runtime_stats_extension.metta \
	tests/test_dispatch_fastpath_equation_guard_regression.metta \
	tests/test_fc_native_depth3_count_regression.metta \
	tests/test_hyperpose_handle_fallback_runtime_stats.metta \
	tests/test_hyperpose_prime_runtime_stats.metta \
	tests/test_hyperpose_threaded_stats.metta \
	tests/test_lts_rho_cost_parallel_runtime_stats.metta \
	tests/test_native_count_collapse_match_regression.metta \
	tests/test_outcome_variant_composition_regression.metta \
	tests/test_outcome_variant_observation_seam_regression.metta \
	tests/test_pathmap_imported_bridge_v2.metta \
	tests/test_rhometta_payload_new_space_affine_runtime_stats.metta \
	tests/test_rhometta_payload_scratch_discard_runtime_stats.metta \
	tests/test_rhometta_threaded_runtime_stats.metta \
	tests/test_runtime_stats_surface.metta \
	tests/test_table_delayed_query_replay_regression.metta \
	tests/test_table_delayed_single_tail_reenter_regression.metta \
	tests/test_table_incremental_stage.metta \
	tests/test_table_invalidation_add.metta \
	tests/test_table_invalidation_remove.metta \
	tests/test_table_nodup_no_invalidation.metta \
	tests/test_table_reuse_after_stale.metta

PATHMAP_RUNTIME_STATS_METTA_TESTS = \
	tests/test_imported_conjunction_bridge_init_regression.metta \
	tests/test_imported_match_chain_conjunction_lowering.metta \
	tests/test_pathmap_direct_transfer_runtime_stats.metta

GC_ADVERSARIAL_TESTS = \
	tests/gc/test_eval_gc_adversarial.metta \
	tests/gc/test_eval_gc_indirect_state_stream.metta

GC_SURVIVOR_RESET_TEST = tests/gc/diagnostics/test_eval_gc_survivor_reset.metta

BACKEND_DEDICATED_TESTS = \
	tests/test_rhocalc_lib_parse_translator_v3.metta \
	tests/test_closed_stream_fastpath.metta \
	tests/test_closed_stream_runtime_stats.metta \
	$(RUNTIME_STATS_METTA_TESTS) \
	$(PATHMAP_RUNTIME_STATS_METTA_TESTS) \
	tests/test_pretty_vars_surface.metta \
	tests/test_import_act_module_surface.metta \
	tests/test_import_mm2_module_surface.metta \
	tests/test_include_mm2_space_target.metta \
	tests/test_mm2_kiss_add_remove.metta \
	tests/test_mm2_kiss_fractal_priority.metta \
	tests/test_mm2_kiss_inline_basic.metta \
	tests/test_mm2_kiss_priority.metta \
	tests/test_module_inventory_act_registered_root.metta \
	tests/test_mork_act_roundtrip.metta \
	tests/test_mork_attached_exact_match_regression.metta \
	tests/test_mork_algebra_surface.metta \
	tests/test_mork_counterexample_loom_surface.metta \
	tests/test_mork_encoding_boundary_surface.metta \
	tests/test_mork_full_pipeline_surface.metta \
	tests/test_mork_handle_errors_surface.metta \
	tests/test_mork_kiss_examples.metta \
	tests/test_mork_lib_surface.metta \
	tests/test_mork_long_string_surface.metta \
	tests/test_mork_native_handle_fresh_id_regression.metta \
	tests/test_mork_add_atoms_runtime_stats.metta \
	tests/test_mm2_match_order_is_unordered.metta \
	tests/test_mork_mm2_metta_showcase.metta \
	tests/test_mork_open_act_surface.metta \
	tests/test_mork_overlay_zipper_surface.metta \
	tests/test_mork_product_zipper_surface.metta \
	tests/test_mork_zipper_surface.metta \
	tests/test_import_mm2_mork_session_lowering.metta \
	tests/test_mork_runtime_stats_isolation.metta \
	tests/test_pathmap_direct_store_runtime_stats.metta \
	tests/test_new_space_mork_surface.metta \
	tests/test_step_space_surface.metta

BACKEND_HEAVY_GOLDEN_TESTS = \
	tests/test_bio_bc_let_hidden_env_regression.metta \
	benchmarks/bc_depth10_spine_regression.metta \
	benchmarks/genomic_pln/bench_drug_hypothesis_1m.metta \
	benchmarks/genomic_pln/test_proof_route_convergence.metta \
	benchmarks/genomic_pln/test_stv_revision.metta \
	benchmarks/genomic_pln/test_hypothesis_key_uniqueness.metta \
	tests/test_checkpoint_group_extract_cross_form_regression.metta \
	tests/test_gparse_native_metamath_corpus.metta \
	tests/test_lib_parse_metamath_a1_db_v0.metta \
	tests/test_lib_parse_metamath_defs_db_v0.metta \
	tests/test_lib_parse_metamath_grammar_v0.metta \
	tests/test_lib_parse_metamath_prefix_frontier_native.metta \
	tests/test_lib_parse_metamath_stmt_prefix_frontier_native.metta \
	tests/test_lib_parse_metamath_theorem_compressed_v0.metta \
	tests/test_lib_parse_metamath_theorem_length_ladder_native.metta \
	tests/test_lib_parse_metamath_theorem_normal_v0.metta \
	benchmarks/test_tilepuzzle.metta

BACKEND_HEAVY_DIAGNOSTIC_TESTS = \
	benchmarks/genomic_pln/bench_drug_hypothesis_1_4m.metta \
	benchmarks/genomic_pln/bench_drug_hypothesis_1_4m_petta.metta \
	benchmarks/genomic_pln/bench_drug_hypothesis_petta_flat.metta \
	benchmarks/genomic_pln/bench_drug_hypothesis_petta_top.metta \
	benchmarks/genomic_pln/test_flat_loader.metta \
	tests/test_checkpoint_disease_route_probe.metta \
	tests/test_lib_parse_metamath_a1_block_pair_db_v0.metta \
	tests/test_lib_parse_metamath_anatomy_db_v0.metta \
	tests/test_lib_parse_metamath_axioms_db_v0.metta \
	tests/test_lib_parse_metamath_block_db_v0.metta \
	tests/test_lib_parse_metamath_defs_a1_block_pair_db_v0.metta \
	tests/test_lib_parse_metamath_defs_a1_block_triplet_db_v0.metta \
	tests/test_lib_parse_metamath_defs_block_pair_db_v0.metta \
	tests/test_lib_parse_metamath_dv_multivar_db_v0.metta \
	tests/test_lib_parse_metamath_dv_scope_db_v0.metta \
	tests/test_lib_parse_metamath_local_var_good_db_v0.metta \
	tests/test_lib_parse_metamath_mini_db_v0.metta \
	tests/test_lib_parse_metamath_mini_thm_compressed_db_v0.metta \
	tests/test_lib_parse_metamath_mini_thm_db_v0.metta \
	benchmarks/test_lib_parse_metamath_mmtest_compressed_simple_db_v0.metta \
	tests/test_lib_parse_metamath_mmtest_compressed_z_db_v0.metta \
	tests/test_lib_parse_metamath_mmtest_d_before_float_db_v0.metta \
	tests/test_lib_parse_metamath_mmtest_min_found_db_v0.metta \
	tests/test_lib_parse_metamath_mmtest_toplevel_e_db_v0.metta \
	tests/test_lib_parse_metamath_nested_scope_db_v0.metta \
	tests/test_lib_parse_metamath_raw_stmt_v0.metta \
	tests/test_lib_parse_metamath_repeat_vars_db_v0.metta \
	benchmarks/test_tilepuzzle_pathmap.metta

BACKEND_HEAVY_TESTS = \
	$(BACKEND_HEAVY_GOLDEN_TESTS) \
	$(BACKEND_HEAVY_DIAGNOSTIC_TESTS)

BACKEND_DIAGNOSTIC_TESTS = \
	tests/test_mm2_match_order_fragile.metta \
	tests/test_print_nondet_probe.metta

BACKEND_PENDING_CORRECTNESS_TESTS =

BACKEND_PARAMETRIC_TEST_PATTERNS = tests/test_*.metta tests/spec_*.metta tests/he_*.metta
BACKEND_PARAMETRIC_SKIP_TESTS = $(PATHMAP_REQUIRED_TESTS) $(PATHMAP_PROBE_TESTS) $(CORE_PROBE_TESTS) $(CORE_XFAIL_TESTS) $(BACKEND_DEDICATED_TESTS) $(BACKEND_HEAVY_TESTS) $(BACKEND_DIAGNOSTIC_TESTS) $(BACKEND_PENDING_CORRECTNESS_TESTS)
BACKEND_PARAMETRIC_BACKENDS ?= $(SPACE_ENGINES)
BACKEND_PARAMETRIC_TIMEOUT ?= 60
BACKEND_PARAMETRIC_DIFF_LINES ?= 24
ifneq ($(ENABLE_PYTHON),1)
BACKEND_PARAMETRIC_SKIP_TESTS += $(PYTHON_TESTS)
endif

# Two-stage bootstrap: cetta compiles its own stdlib
STDLIB_SRC = lib/stdlib.metta
STDLIB_BLOB = src/stdlib_blob.h
STDLIB_BLOB_STAMP = $(BOOTSTRAP_TMPDIR)/stdlib_blob.$(BUILD_CANON).stamp
ABT_DEFAULT_SIGNATURES_SRC = lib/abt_default_signatures.metta
ABT_DEFAULT_SIGNATURES_BLOB = src/abt_default_signatures_blob.h
ABT_DEFAULT_SIGNATURES_BLOB_STAMP = $(BOOTSTRAP_TMPDIR)/abt_default_signatures_blob.$(BUILD_CANON).stamp

all: $(BIN)
	@echo "Built ./$(BIN) [BUILD=$(BUILD_CANON), runtime-stats=$(ENABLE_RUNTIME_STATS)]"
	@echo "Tip: ./$(BIN) -v prints the active version and build mode"

core:
	@$(MAKE) BUILD=core $(BIN)

python:
	@$(MAKE) BUILD=python $(BIN)

mork:
	@$(MAKE) BUILD=mork $(BIN)

main:
	@$(MAKE) BUILD=main $(BIN)

pathmap:
	@$(MAKE) BUILD=pathmap $(BIN)

full:
	@$(MAKE) BUILD=full $(BIN)

profile:
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 ENABLE_RUNTIME_TIMING=1 $(BIN)

bench-metamath-d5: $(BIN)
	@./scripts/bench_metamath_d5.sh

bench-rho-fanout: $(BIN)
	@./benchmarks/rho/fanout/run.sh

bench-rho-comm-frontier: $(BIN)
	@./benchmarks/rho/comm-frontier/run.sh

bench-rho-rhometta-deduction-farm: $(BIN)
	@./benchmarks/rho/rhometta-pln-deduction-farm/run.sh

bench-rho-hot-frontier: $(BIN)
	@./benchmarks/rho/hot-frontier/run.sh

bench-rho-hot-successors: $(BIN)
	@./benchmarks/rho/hot-successors/run.sh

bench-rho-comm-contention: $(BIN)
	@./benchmarks/rho/comm-contention/run.sh

bench-rho-pipeline-forward: $(BIN)
	@./benchmarks/rho/pipeline-forward/run.sh

bench-rho-route-synthesis: $(BIN)
	@./benchmarks/rho/route-synthesis/run.sh

bench-rho-demand-index: $(BIN)
	@./benchmarks/rho/demand-index/run.sh

bench-rho-indexed-demand: $(BIN)
	@./benchmarks/rho/indexed-demand/run.sh

bench-rho-route-policy: $(BIN)
	@./benchmarks/rho/route-policy/run.sh

bench-rho-certificate-quorum: $(BIN)
	@./benchmarks/rho/certificate-quorum/run.sh

bench-rho-threaded: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_threaded_bench.py "$(CETTA_SCRIPT_BIN)" --mode quick --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" --seed "$(RHO_BENCH_SEED)" $(RHO_BENCH_CSV_ARG)

bench-rho-threaded-heavy: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_threaded_bench.py "$(CETTA_SCRIPT_BIN)" --mode standard --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" --seed "$(RHO_BENCH_SEED)" --require-speedup $(RHO_BENCH_CSV_ARG)

bench-rho-cost-threaded: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_threaded_bench.py "$(CETTA_SCRIPT_BIN)" --mode quick --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" $(RHO_COST_BENCH_CSV_ARG)

bench-rho-cost-threaded-heavy: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_threaded_bench.py "$(CETTA_SCRIPT_BIN)" --mode heavy --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" --require-speedup $(RHO_COST_BENCH_CSV_ARG)

bench-rho-threaded-corpus: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_threaded_corpus_bench.py "$(CETTA_SCRIPT_BIN)" --suite core --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" $(RHO_BENCH_CSV_ARG)

bench-rho-threaded-generated: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_threaded_generated_bench.py "$(CETTA_SCRIPT_BIN)" --size-mode "$(RHO_BENCH_GENERATED_SIZE_MODE)" --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" $(RHO_BENCH_CSV_ARG)

bench-rho-threaded-generated-runtime-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_threaded_generated_bench.py "$(CETTA_SCRIPT_BIN)" --emit-runtime-stats --size-mode "$(RHO_BENCH_GENERATED_SIZE_MODE)" --runs "$(RHO_BENCH_RUNS)" --threads "$(RHO_BENCH_THREADS)" $(RHO_BENCH_CSV_ARG)
else
	@echo "INFO: generated rho threaded benchmark runtime stats requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

bench-weird-audit: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@./scripts/bench_weird_audit.sh
else
	$(call reexec_mork_bridge_or_skip,weird benchmark audit,$@)
endif

bench-answer-ref-demand: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@./scripts/bench_answer_ref_demand.sh
else
	@echo "INFO: answer-ref demand benchmark requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

bench-pathmap-fc-d3: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@./scripts/bench_pathmap_fc_d3.sh
else
	$(call reexec_pathmap_bridge_or_skip,pathmap FC depth-3 benchmark,$@)
endif

bench-fc-backend-matrix:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_fc_backend_matrix.sh

bench-space-backend-matrix:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_space_backend_matrix.sh

bench-space-transfer-matrix:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_space_transfer_matrix.sh

bench-space-scale-ladder:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_space_scale_ladder.sh

bench-ffi-friction-light:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_ffi_friction_suite.sh light $(or $(BENCH_FFI_LIGHT_N),1000) $(or $(BENCH_FFI_LIGHT_ROUNDS),1)

bench-ffi-friction-basic:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_ffi_friction_suite.sh basic $(or $(BENCH_FFI_BASIC_N),10000) $(or $(BENCH_FFI_BASIC_ROUNDS),3)

bench-ffi-friction-stress:
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_ffi_friction_suite.sh stress $(or $(BENCH_FFI_STRESS_N),50000) $(or $(BENCH_FFI_STRESS_ROUNDS),3)

bench-ffi-friction-heavy:
	@if [ "$(BENCH_FFI_ALLOW_HEAVY)" != "1" ]; then \
		echo "Refusing heavy FFI benchmark without BENCH_FFI_ALLOW_HEAVY=1"; \
		echo "Try: BENCH_FFI_ALLOW_HEAVY=1 make bench-ffi-friction-heavy"; \
		exit 2; \
	fi
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=0 $(BIN)
	@BENCH_FFI_ALLOW_HEAVY=1 ./scripts/bench_ffi_friction_suite.sh heavy $(or $(BENCH_FFI_HEAVY_N),100000) $(or $(BENCH_FFI_HEAVY_ROUNDS),3)

perf-runtime-stats:
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) ./scripts/bench_runtime_stats_probe.sh
else
	@echo "INFO: runtime-stats probe requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

probe-epoch-runtime-witness: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(if $(filter 1,$(SANITIZER_REPEATABLE)),CETTA_SANITIZER_REPEATABLE=1 CETTA_WRAPPED_BIN="$(abspath $(BIN))",) \
		bash ./scripts/probe_epoch_runtime_witness.sh "$(if $(filter 1,$(SANITIZER_REPEATABLE)),$(abspath $(CETTA_EXEC_WRAPPER)),$(abspath $(BIN)))"
else
	@echo "INFO: epoch runtime witness requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

perf-stable: perf-runtime-stats

bench: bench-light

bench-light:
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-correctness
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-performance-light

bench-correctness:
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d3
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d3-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-conj-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-conj12-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-dup-conj-backends

bench-performance-light:
	@$(MAKE) -s BUILD=$(BUILD_CANON) perf-bench-tu
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-lib-parse-inference-native
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-prime-light
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-rho-threaded
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-rho-cost-threaded
	@if [ "$(AUTO_BUILD_OPTIONAL)" = "1" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) bench-optional-bridge-light; \
	fi

bench-optional-bridge-light:
	@$(MAKE) -s BUILD=main bench-ffi-friction-light

bench-capacity:
	@$(MAKE) -s BUILD=$(BUILD_CANON) perf-capacity-tu

bench-heavy:
	@if [ "$(BENCH_ALLOW_HEAVY)" != "1" ]; then \
		echo "Refusing heavy benchmark suite without BENCH_ALLOW_HEAVY=1"; \
		echo "Try: BENCH_ALLOW_HEAVY=1 make bench-heavy"; \
		exit 2; \
	fi
	@if [ "$(RHO_BENCH_ENFORCE_BASELINE)" = "1" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) perf-bench-rhocalc; \
	else \
		$(MAKE) -s BUILD=$(BUILD_CANON) bench-rho-threaded-heavy; \
		$(MAKE) -s BUILD=$(BUILD_CANON) bench-rho-cost-threaded-heavy; \
	fi
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-ffi-friction-stress
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-space-backend-matrix
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-space-transfer-matrix
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-space-scale-ladder
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d3-nodup-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d4-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d4-nodup-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) probe-d4-nodup-capability-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-rho-threaded-corpus
	@$(MAKE) -s BUILD=$(BUILD_CANON) RHO_BENCH_GENERATED_SIZE_MODE=largest bench-rho-threaded-generated
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 RHO_BENCH_GENERATED_SIZE_MODE=largest bench-rho-threaded-generated-runtime-stats
	@BENCH_ALLOW_HEAVY=1 $(MAKE) -s BUILD=$(BUILD_CANON) bench-prime-heavy
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-mam-linear-fibonacci

bench-prime-light: $(BIN)
	@$(CETTA_BIN_INVOKE) --lang prime benchmarks/prime/bench_typed_search.metta

bench-prime-heavy: $(BIN)
	@if [ "$(BENCH_ALLOW_HEAVY)" != "1" ]; then \
		echo "Refusing heavy Prime benchmark without BENCH_ALLOW_HEAVY=1"; \
		echo "Try: BENCH_ALLOW_HEAVY=1 make bench-prime-heavy"; \
		exit 2; \
	fi
	@$(CETTA_BIN_INVOKE) --lang prime benchmarks/prime/bench_typed_search_capacity.metta

test-symbolid-guard:
	@./scripts/check_symbolid_guards.sh

$(VARIANT_SHAPE_TEST_BIN): tests/test_variant_shape_roundtrip.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_variant_shape_roundtrip.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(LDFLAGS)

test-variant-shape-roundtrip: $(VARIANT_SHAPE_TEST_BIN)
	@$(call cetta_exec,./$(VARIANT_SHAPE_TEST_BIN))

$(BINDINGS_LOOKUP_INDEX_TEST_BIN): tests/test_bindings_lookup_index.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) -DCETTA_TEST_HOOKS=1 $(CFLAGS) -o $@ tests/test_bindings_lookup_index.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(LDFLAGS)

test-bindings-lookup-index: $(BINDINGS_LOOKUP_INDEX_TEST_BIN)
	@enabled=$$($(call cetta_exec,./$(BINDINGS_LOOKUP_INDEX_TEST_BIN))); \
	disabled=$$(CETTA_BINDINGS_LOOKUP_INDEX=0 $(call cetta_exec,./$(BINDINGS_LOOKUP_INDEX_TEST_BIN))); \
	audited=$$(CETTA_BINDINGS_DERIVED_AUDIT=1 $(call cetta_exec,./$(BINDINGS_LOOKUP_INDEX_TEST_BIN))); \
	expected='(BindingsLookupIndexSummary 72 72 0)'; \
	printf '%s\n' "$$enabled"; \
	test "$$enabled" = "$$expected" && test "$$disabled" = "$$expected" && \
		test "$$audited" = "$$expected"
.PHONY: test-bindings-lookup-index

$(ATOM_DEEP_COPY_TEST_BIN): tests/test_atom_deep_copy_iterative.c src/symbol.c src/atom.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_atom_deep_copy_iterative.c src/symbol.c src/atom.c $(LDFLAGS)

test-atom-deep-copy-iterative: $(ATOM_DEEP_COPY_TEST_BIN)
	@$(call cetta_exec,./$(ATOM_DEEP_COPY_TEST_BIN))

$(NAME_KEY_TEST_BIN): tests/test_name_key.c src/name_key.c src/name_key.h src/symbol.c src/atom.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_RUNTIME_STATS_NOOP=1 \
		-o $@ tests/test_name_key.c src/name_key.c src/symbol.c src/atom.c $(LDFLAGS)

$(NAME_KEY_MUTATION_TEST_BIN): tests/test_name_key.c src/name_key.c src/name_key.h src/symbol.c src/atom.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_RUNTIME_STATS_NOOP=1 \
		-DCETTA_NAME_KEY_MUTATION=1 -o $@ \
		tests/test_name_key.c src/name_key.c src/symbol.c src/atom.c $(LDFLAGS)

test-name-key: $(NAME_KEY_TEST_BIN) $(NAME_KEY_MUTATION_TEST_BIN)
	@result=$$(./$(NAME_KEY_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(NameKeySummary 19 19 0)')" -ne 1 ]; then \
		echo "FAIL: structural NameId exact summary absent or duplicated"; \
		exit 1; \
	fi; \
	if ./$(NAME_KEY_MUTATION_TEST_BIN) >/dev/null 2>&1; then \
		echo "FAIL: digest-only NameId mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: digest-only NameId mutation rejected"

$(PRIME_NEED_TEST_BIN): tests/test_prime_need.c src/prime_need.c src/prime_need.h src/symbol.c src/atom.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(PRIME_NEED_UNIT_CPPFLAGS) $(CFLAGS) -o $@ tests/test_prime_need.c src/prime_need.c src/symbol.c src/atom.c $(LDFLAGS)

$(PRIME_CONTEXT_MUTATION_TEST_BIN): tests/test_prime_need.c src/prime_need.c src/prime_need.h src/symbol.c src/atom.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(PRIME_NEED_UNIT_CPPFLAGS) $(CFLAGS) -DCETTA_PRIME_CONTEXT_MUTATION=1 \
		-o $@ tests/test_prime_need.c src/prime_need.c src/symbol.c src/atom.c $(LDFLAGS)

test-prime-need-algebra: $(PRIME_NEED_TEST_BIN)
	@result=$$(./$(PRIME_NEED_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(PrimeNeedAlgebraSummary $(PRIME_NEED_ALGEBRA_CHECKS) $(PRIME_NEED_ALGEBRA_CHECKS) 0)')" -ne 1 ]; then \
		echo "FAIL: Prime Need algebra exact summary absent or duplicated"; \
		exit 1; \
	fi

.PHONY: test-prime-need-quote-preservation
test-prime-need-quote-preservation: $(BIN)
	@actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_quote_preservation.metta 2>&1); \
	expected=$$(cat tests/prime/need_quote_preservation.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: Prime Need forced syntax below quote"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: Prime Need preserves syntax below quote and evaluates demanded values"

test-prime-need-closure-capture: $(BIN)
	@if [ "$(ENABLE_PRIME_NEED_CLOSURE_CAPTURE)" != 1 ]; then \
		echo "FAIL: enable ENABLE_PRIME_NEED_CLOSURE_CAPTURE=1 for this experimental gate"; \
		exit 1; \
	fi
	@actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_closure_capture.metta 2>&1); \
	expected=$$(cat tests/prime/need_closure_capture.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: Prime Need exact closure-capture contract drifted"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: Prime Need exact closure captures preserve positive, negative, and transitive cases"

test-prime-need-closure-capture-stats: $(BIN)
	@if [ "$(ENABLE_PRIME_NEED_CLOSURE_CAPTURE)" != 1 ] || \
	    [ "$(ENABLE_RUNTIME_STATS)" != 1 ]; then \
		echo "FAIL: enable exact closure capture and runtime stats for this gate"; \
		exit 1; \
	fi
	@CETTA_BIN="$(abspath $(BIN))" \
		./scripts/test_prime_need_capture_stats.sh

test-prime-need-heap-index-stats: $(BIN)
	@if [ "$(ENABLE_PRIME_NEED_HEAP_INDEX)" != 1 ] || \
	    [ "$(ENABLE_RUNTIME_STATS)" != 1 ]; then \
		echo "FAIL: enable the Prime Need-heap index and runtime stats for this gate"; \
		exit 1; \
	fi
	@CETTA_PRIME_NEED_HEAP_INDEX=1 CETTA_BIN="$(abspath $(BIN))" \
		./scripts/test_prime_need_heap_stats.sh
.PHONY: test-prime-need-heap-index-stats

test-prime-need-planner-stats: $(BIN)
	@if [ "$(ENABLE_RUNTIME_STATS)" != 1 ]; then \
		echo "FAIL: enable runtime stats for the Prime one-pass planner gate"; \
		exit 1; \
	fi
	@CETTA_BIN="$(abspath $(BIN))" \
		./scripts/test_prime_need_planner_stats.sh
.PHONY: test-prime-need-planner-stats

test-prime-eval-stack: $(BIN)
	@if [ "$(ENABLE_PRIME_EVAL_STACK)" != 1 ]; then \
		echo "FAIL: enable ENABLE_PRIME_EVAL_STACK=1 for this gate"; \
		exit 1; \
	fi
	@set -eu; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		-e '!(if (superpose (True False True)) A B)' 2>&1); \
	if [ "$$actual" != '[A, B, A]' ]; then \
		echo "FAIL: explicit Prime stack changed if branch order or multiplicity"; \
		printf '%s\n' "$$actual"; \
		exit 1; \
	fi; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		-e '!(+ (superpose (1 2 1)) 10)' 2>&1); \
	if [ "$$actual" != '[11, 12, 11]' ]; then \
		echo "FAIL: explicit Prime stack changed strict argument order or multiplicity"; \
		printf '%s\n' "$$actual"; \
		exit 1; \
	fi; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		-e '!(let $$x (superpose (1 2)) (+ $$x $$x))' 2>&1); \
	if [ "$$actual" != '[2, 4]' ]; then \
		echo "FAIL: explicit Prime stack changed call-time choice"; \
		printf '%s\n' "$$actual"; \
		exit 1; \
	fi; \
	actual=$$($(CETTA_BIN_INVOKE) --lang he --profile he-extended \
		-e '!(+ 1 2)' 2>&1); \
	if [ "$$actual" != '[3]' ]; then \
		echo "FAIL: Prime-only explicit stack changed HE evaluation"; \
		printf '%s\n' "$$actual"; \
		exit 1; \
	fi; \
	echo "PASS: explicit Prime stack preserves branch order, bag multiplicity, call-time choice, and HE isolation"

test-prime-eval-stack-stats: $(BIN)
	@if [ "$(ENABLE_PRIME_EVAL_STACK)" != 1 ] || \
	    [ "$(ENABLE_RUNTIME_STATS)" != 1 ]; then \
		echo "FAIL: enable the explicit Prime stack and runtime stats for this gate"; \
		exit 1; \
	fi
	@CETTA_BIN="$(abspath $(BIN))" \
		./scripts/test_prime_eval_stack_stats.sh

test-prime-need-he-noninterference: $(BIN)
	@set -eu; \
	source=tests/prime/need_he_noninterference.metta; \
	for profile in he he-compat he-extended he-prime; do \
		actual=$$($(CETTA_BIN_INVOKE) --lang he --profile "$$profile" \
			"$$source" 2>&1); \
		expected=$$(cat "tests/prime/need_he_noninterference.$$profile.expected"); \
		if [ "$$actual" != "$$expected" ]; then \
			echo "FAIL: Prime Need changed HE profile $$profile"; \
			diff <(printf '%s\n' "$$expected") \
			     <(printf '%s\n' "$$actual") | head -40; \
			exit 1; \
		fi; \
		echo "PASS: HE profile $$profile unchanged by Prime Need"; \
	done

test-prime-need-correspondence: $(BIN)
	@set -eu; \
	canonical=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_correspondence_canonical.metta 2>&1); \
	ordinary=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_correspondence_ordinary.metta 2>&1); \
	expected=$$(cat tests/prime/need_correspondence.expected); \
	if [ "$$canonical" != "$$expected" ]; then \
		echo "FAIL: canonical App observation contract drifted"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$canonical") | head -40; \
		exit 1; \
	fi; \
	if [ "$$ordinary" != "$$expected" ]; then \
		echo "FAIL: ordinary equation application diverged from canonical App"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$ordinary") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: canonical App and ordinary application agree on 12 full observations"

probe-prime-need-observation-boundary: $(BIN)
	@set -eu; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_observation_boundary_tournament.metta 2>&1); \
	expected=$$(cat \
		tests/prime/need_observation_boundary_tournament.current.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: open Prime suspension-observation characterization drifted"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: current suspension-observation boundary characterized (law remains open)"

probe-prime-equation-call-sharing-tournament: $(BIN)
	@set -eu; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_equation_call_sharing_tournament.metta 2>&1); \
	expected=$$(cat \
		tests/prime/need_equation_call_sharing_tournament.current.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: open Prime equation-call sharing characterization drifted"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: current equation-call sharing frontier characterized (law remains open)"

test-prime-equation-call-sharing-tournament: $(BIN)
	@set -eu; \
	result=$$(CETTA_BIN="$(abspath $(BIN))" \
		python3 tests/prime/run_need_equation_call_tournament.py 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | \
		grep -Fxc '(PrimeNeedEquationCallTournamentSummary 48 48 0 frontiers 3 order-invariance monolithic-red candidate-local-green demand-cohort-green)')" -ne 1 ]; then \
		echo "FAIL: Prime equation-call tournament exact summary absent or duplicated"; \
		exit 1; \
	fi; \
	if [ "$$(printf '%s\n' "$$result" | \
		grep -Fxc '(PrimeNativeReceiptMutationSummary 8 8 0)')" -ne 1 ]; then \
		echo "FAIL: Prime native receipt mutation summary absent or duplicated"; \
		exit 1; \
	fi

test-prime-cell-causal-reference:
	@python3 tests/prime/test_cell_causal_reference.py

test-prime-shared-cause-probability: $(BIN)
	@CETTA_BIN="$(abspath $(BIN))" \
		python3 tests/prime/test_shared_cause_probability.py

test-prime-evaluation-strategy-contrast: $(BIN)
	@CETTA="$(abspath $(BIN))" \
		./scripts/check_prime_evaluation_strategy_contrast.sh

test-prime-need-gc-lifetime: $(BIN)
	@set -eu; \
	actual=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
		$(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_gc_lifetime.metta 2>&1); \
	expected=$$(cat tests/prime/need_gc_lifetime.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: Prime Need heap did not survive tiny-budget scratch GC"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: Prime Need episode heap is independent of scratch-GC lifetime"

test-prime-need-effect-isolation: $(BIN)
	@set -eu; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_branch_effect_isolation.metta 2>&1); \
	expected=$$(cat tests/prime/need_branch_effect_isolation.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: Prime sibling state effects escaped their branch receipt"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: Prime sibling state effects remain branch-local"

test-prime-need-boundaries: $(BIN)
	@set -eu; \
		for source in \
			tests/prime/need_ref_forgery.metta \
			tests/prime/need_origin_boundary.metta \
			tests/prime/need_sharing_boundary.metta; do \
			expected_file="$${source%.metta}.expected"; \
			actual=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
			expected=$$(cat "$$expected_file"); \
			if [ "$$actual" != "$$expected" ]; then \
				echo "FAIL: $$source"; \
				diff <(printf '%s\n' "$$expected") \
				     <(printf '%s\n' "$$actual") | head -40; \
				exit 1; \
			fi; \
			echo "PASS: $$source"; \
		done

test-prime-suspension-rights: $(BIN)
	@set -eu; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/need_suspension_rights.metta 2>&1); \
	expected=$$(cat tests/prime/need_suspension_rights.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: Prime suspension rights and origin observation"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: Prime suspension rights attenuate and origin views do not force"

test-prime-contexts: $(BIN) $(PRIME_CONTEXT_MUTATION_TEST_BIN)
	@set -eu; \
	actual=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/first_class_contexts.metta 2>&1); \
	expected=$$(cat tests/prime/first_class_contexts.expected); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: Prime first-class context laws"; \
		diff <(printf '%s\n' "$$expected") \
		     <(printf '%s\n' "$$actual") | head -40; \
		exit 1; \
	fi; \
	he_expected=$$(cat tests/prime/first_class_contexts_he_guard.expected); \
	for profile in he he-compat he-extended he-prime; do \
		he_actual=$$($(CETTA_BIN_INVOKE) --lang he --profile "$$profile" \
			tests/prime/first_class_contexts_he_guard.metta 2>&1); \
		if [ "$$he_actual" != "$$he_expected" ]; then \
			echo "FAIL: Prime context surface changed HE profile $$profile"; \
			diff <(printf '%s\n' "$$he_expected") \
			     <(printf '%s\n' "$$he_actual") | head -40; \
			exit 1; \
		fi; \
	done; \
	inert=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt \
		tests/support/mork_mm2/test_unsupported_factory.mm2 2>&1); \
	for marker in '(0 unknown-source)' '(0 unknown-sink)'; do \
		if ! printf '%s\n' "$$inert" | grep -Fq "$$marker"; then \
			echo "FAIL: unsupported MM2 factory directive was consumed: $$marker"; \
			printf '%s\n' "$$inert"; \
			exit 1; \
		fi; \
	done; \
	mutant_output=$$(./$(PRIME_CONTEXT_MUTATION_TEST_BIN) 2>&1 || true); \
	if ! printf '%s\n' "$$mutant_output" | \
		grep -Fq 'FAIL: newest context binding shadows without mutating its parent'; then \
		echo "FAIL: oldest-binding context mutation survived or failed for the wrong reason"; \
		printf '%s\n' "$$mutant_output" | tail -20; \
		exit 1; \
	fi; \
	echo "PASS: Prime contexts are lazy, persistent, boundedly observable, and HE-inert"

test-prime-context-tutorial: $(BIN)
	@set -eu; \
	for source in \
		examples/prime/context_tutorial.metta \
		examples/prime/context_tutorial/01_persistent_values.metta \
		examples/prime/context_tutorial/02_lazy_sharing.metta \
		examples/prime/context_tutorial/03_proof_environments.metta \
		examples/prime/context_tutorial/04_counterfactual_planning.metta \
		examples/prime/context_tutorial/05_inspection_and_persistence.metta \
		examples/prime/context_tutorial/06_memoized_subgoals.metta \
		examples/prime/context_tutorial/07_explicit_suspensions.metta; do \
		expected_file="$${source%.metta}.expected"; \
		actual=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
		expected=$$(cat "$$expected_file"); \
		if [ "$$actual" != "$$expected" ]; then \
			echo "FAIL: $$source"; \
			diff <(printf '%s\n' "$$expected") \
			     <(printf '%s\n' "$$actual") | head -40; \
			exit 1; \
		fi; \
		echo "PASS: $$source"; \
	done

test-prime-rewrite-frontier-tutorial: $(BIN)
	@set -eu; \
	for source in \
		examples/prime/rewrite_frontier_tutorial/01_directional_rules.metta \
		examples/prime/rewrite_frontier_tutorial/02_rule_occurrences.metta; do \
		expected_file="$${source%.metta}.expected"; \
		actual=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
		expected=$$(cat "$$expected_file"); \
		if [ "$$actual" != "$$expected" ]; then \
			echo "FAIL: $$source"; \
			diff <(printf '%s\n' "$$expected") \
			     <(printf '%s\n' "$$actual") | head -40; \
			exit 1; \
		fi; \
		echo "PASS: $$source"; \
	done; \
	for frontier in monolithic candidate-local demand-cohort; do \
		for source in \
			examples/prime/rewrite_frontier_tutorial/03_disjoint_supports.metta \
			examples/prime/rewrite_frontier_tutorial/04_overlapping_supports.metta; do \
			expected_file="$${source%.metta}.$${frontier}.expected"; \
			actual=$$($(CETTA_BIN_INVOKE) --lang prime \
				--prime-rewrite-frontier "$$frontier" "$$source" 2>&1); \
			expected=$$(cat "$$expected_file"); \
			if [ "$$actual" != "$$expected" ]; then \
				echo "FAIL: $$source ($$frontier)"; \
				diff <(printf '%s\n' "$$expected") \
				     <(printf '%s\n' "$$actual") | head -40; \
				exit 1; \
			fi; \
			echo "PASS: $$source ($$frontier)"; \
		done; \
	done; \
	if $(CETTA_BIN_INVOKE) --lang he --profile he-extended \
		--prime-rewrite-frontier monolithic \
		examples/prime/rewrite_frontier_tutorial/01_directional_rules.metta \
		>/dev/null 2>&1; then \
		echo "FAIL: Prime rewrite-frontier switch changed an HE profile"; \
		exit 1; \
	fi; \
	echo "PASS: Prime rewrite-frontier tutorial and CLI boundary"

test-prime-equation-call-constitution: \
	test-prime-equation-call-sharing-tournament \
	test-prime-cell-causal-reference \
	test-prime-shared-cause-probability \
	test-prime-need-algebra \
	$(PRIME_NEED_CLOSURE_CAPTURE_GATE) \
	$(PRIME_NEED_CLOSURE_CAPTURE_STATS_GATE) \
	$(PRIME_EVAL_STACK_GATE) \
	$(PRIME_EVAL_STACK_STATS_GATE) \
	test-prime-contexts \
	test-prime-context-tutorial \
	test-prime-rewrite-frontier-tutorial \
	test-prime-evaluation-strategy-contrast \
	test-prime-internal-graduality \
	test-prime-need-he-noninterference
	@echo "PASS: Prime equation-call constitution focused aggregate"

test-prime-need-equation-choice-sharing:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 \
		test-prime-need-equation-choice-sharing-body

test-prime-need-equation-choice-sharing-body: $(BIN)
	@set -eu; \
		stdout_file=$$(mktemp); stderr_file=$$(mktemp); \
		trap 'rm -f "$$stdout_file" "$$stderr_file"' EXIT; \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang prime \
			tests/prime/need_equation_choice_sharing.metta \
			>"$$stdout_file" 2>"$$stderr_file"; \
		expected=$$(cat tests/prime/need_equation_choice_sharing.expected); \
		actual=$$(cat "$$stdout_file"); \
		if [ "$$actual" != "$$expected" ]; then \
			echo "FAIL: Prime mixed equation answers changed"; \
			diff <(printf '%s\n' "$$expected") \
			     <(printf '%s\n' "$$actual") | head -40; \
			exit 1; \
		fi; \
		writes=$$(sed -n \
			's/^runtime-counter prime-need-receipt-state-write //p' \
			"$$stderr_file"); \
		if [ "$$writes" != 1 ]; then \
			echo "FAIL: mixed equations invoked the source producer $$writes times"; \
			exit 1; \
	fi; \
	echo "PASS: strict and wildcard equations share one source suspension"

test-prime-need-equation-choice-sharing-mutation:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 \
		test-prime-need-equation-choice-sharing-mutation-body

test-prime-need-equation-choice-sharing-mutation-body: $(BIN)
	@set -eu; \
		mutation_dir=runtime/prime-need-mutations; \
		mkdir -p "$$mutation_dir"; \
		object="$$mutation_dir/eval-RULE_LOCAL_THUNKS-runtime-stats.o"; \
		binary="$$mutation_dir/cetta-RULE_LOCAL_THUNKS-runtime-stats"; \
		$(CC) $(CPPFLAGS) $(CFLAGS) \
			-DCETTA_PRIME_NEED_MUTATION_RULE_LOCAL_THUNKS=1 \
			-c src/eval.c -o "$$object"; \
		$(CC) $(filter-out src/eval.$(BUILD_OBJ_TAG).o src/eval.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
			"$$object" -o "$$binary" $(LDFLAGS); \
		stdout_file=$$(mktemp); stderr_file=$$(mktemp); \
		trap 'rm -f "$$stdout_file" "$$stderr_file"' EXIT; \
		"$$binary" --emit-runtime-stats --lang prime \
			tests/prime/need_equation_choice_sharing.metta \
			>"$$stdout_file" 2>"$$stderr_file"; \
		writes=$$(sed -n \
			's/^runtime-counter prime-need-receipt-state-write //p' \
			"$$stderr_file"); \
		if [ "$$writes" != 2 ]; then \
			echo "FAIL: rule-local-thunk mutation produced $$writes writes, expected 2"; \
			exit 1; \
		fi; \
		echo "PASS: rule-local-thunk mutation repeats the source producer and is killed"

test-prime-need-mutations: $(BIN) test-prime-need-algebra \
		test-prime-need-correspondence test-prime-need-gc-lifetime \
		test-prime-need-boundaries \
		test-prime-need-quote-preservation \
		test-prime-need-effect-isolation \
		test-prime-need-equation-choice-sharing \
		test-prime-need-equation-choice-sharing-mutation
	@set -eu; \
	mutation_dir=runtime/prime-need-mutations; \
	mkdir -p "$$mutation_dir"; \
	for mutation in EAGER CBN STORAGE_LEAK SCRATCH_OWNER FUNCTION_BINDING_LEAK RULE_LOCAL_THUNKS DROP_RESIDUAL REGISTRY_PATTERN_INERT DROP_RESULT_CONTRACT DYNAMIC_SCOPE; do \
		case "$$mutation" in \
			EAGER) source=tests/prime/need_application.metta; \
			       expected=tests/prime/need_application.expected ;; \
			CBN) source=tests/prime/need_explicit_control.metta; \
			     expected=tests/prime/need_explicit_control.expected ;; \
			STORAGE_LEAK) source=tests/prime/need_storage_boundary.metta; \
			              expected=tests/prime/need_storage_boundary.expected ;; \
			SCRATCH_OWNER) source=tests/prime/need_gc_lifetime.metta; \
			               expected=tests/prime/need_gc_lifetime.expected ;; \
			FUNCTION_BINDING_LEAK) source=tests/prime/need_application.metta; \
			                       expected=tests/prime/need_application.expected ;; \
			RULE_LOCAL_THUNKS|DROP_RESIDUAL|REGISTRY_PATTERN_INERT) source=tests/prime/need_application.metta; \
			                                                          expected=tests/prime/need_application.expected ;; \
			DROP_RESULT_CONTRACT) source=tests/prime/gradual/annotation_boundary.metta; \
			                      expected=tests/prime/gradual/annotation_boundary.expected ;; \
			DYNAMIC_SCOPE) source=tests/prime/conformance/cell_lexical_refinement.metta; \
			               expected=tests/prime/conformance/cell_lexical_refinement.expected ;; \
		esac; \
		object="$$mutation_dir/eval-$$mutation.o"; \
		binary="$$mutation_dir/cetta-$$mutation"; \
		$(CC) $(CPPFLAGS) $(CFLAGS) \
			-DCETTA_PRIME_NEED_MUTATION_$$mutation=1 \
			-c src/eval.c -o "$$object"; \
		$(CC) $(filter-out src/eval.$(BUILD_OBJ_TAG).o src/eval.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
			"$$object" -o "$$binary" $(LDFLAGS); \
		set +e; \
		if [ "$$mutation" = SCRATCH_OWNER ]; then \
			actual=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
				"$$binary" --lang prime "$$source" 2>&1); \
		else \
			actual=$$("$$binary" --lang prime "$$source" 2>&1); \
		fi; \
		status=$$?; set -e; \
		if [ $$status -eq 0 ] && [ "$$actual" = "$$(cat "$$expected")" ]; then \
			echo "FAIL: Prime Need $$mutation mutation survived"; \
			exit 1; \
		fi; \
		echo "PASS: Prime Need $$mutation mutation killed"; \
	done; \
	sibling_bin="$$mutation_dir/test-sibling-merge"; \
	$(CC) $(CPPFLAGS) $(PRIME_NEED_UNIT_CPPFLAGS) $(CFLAGS) \
		-DCETTA_PRIME_NEED_MUTATION_SIBLING_MERGE=1 \
		-o "$$sibling_bin" tests/test_prime_need.c src/prime_need.c \
		src/symbol.c src/atom.c $(LDFLAGS); \
	set +e; sibling_out=$$("$$sibling_bin" 2>&1); sibling_status=$$?; set -e; \
	if [ $$sibling_status -eq 0 ] || \
	   ! printf '%s\n' "$$sibling_out" | \
	     grep -Fq 'FAIL: sibling heaps cannot merge'; then \
		echo "FAIL: sibling-heap mutation was not killed by its law"; \
		exit 1; \
	fi; \
	echo "PASS: Prime Need SIBLING_MERGE mutation killed"; \
	fault_bin="$$mutation_dir/test-fault-not-cached"; \
	$(CC) $(CPPFLAGS) $(PRIME_NEED_UNIT_CPPFLAGS) $(CFLAGS) \
		-DCETTA_PRIME_NEED_MUTATION_FAULT_NOT_CACHED=1 \
		-o "$$fault_bin" tests/test_prime_need.c src/prime_need.c \
		src/symbol.c src/atom.c $(LDFLAGS); \
	set +e; fault_out=$$("$$fault_bin" 2>&1); fault_status=$$?; set -e; \
	if [ $$fault_status -eq 0 ] || \
	   ! printf '%s\n' "$$fault_out" | \
	     grep -Fq 'FAIL: repeated fault lookup returns the cached receipt'; then \
		echo "FAIL: fault-cache mutation was not killed by its law"; \
		exit 1; \
	fi; \
	echo "PASS: Prime Need FAULT_NOT_CACHED mutation killed"

$(REGISTRY_RESOLVER_TEST_OBJ): $(REGISTRY_RESOLVER_TEST_SRC) src/registry_resolver.h src/space.h src/name_key.h $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime/bootstrap
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(REGISTRY_RESOLVER_TEST_BIN): $(REGISTRY_RESOLVER_TEST_OBJ) $(REGISTRY_RESOLVER_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CFLAGS) -o $@ $(REGISTRY_RESOLVER_TEST_OBJ) $(REGISTRY_RESOLVER_TEST_LINK_OBJ) $(LDFLAGS)

test-registry-resolver: $(REGISTRY_RESOLVER_TEST_BIN)
	@result=$$(./$(REGISTRY_RESOLVER_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(RegistryResolverSummary 29 29 0)')" -ne 1 ]; then \
		echo "FAIL: structural registry resolver exact summary absent or duplicated"; \
		exit 1; \
	fi; \
	mutation_dir=runtime/registry-resolver-mutation; \
	mkdir -p "$$mutation_dir"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_REGISTRY_RESOLVER_MUTATION=1 \
		-c src/registry_resolver.c -o "$$mutation_dir/registry_resolver.o"; \
	$(CC) $(CFLAGS) -o "$$mutation_dir/test_registry_resolver" \
		$(REGISTRY_RESOLVER_TEST_OBJ) \
		$(filter-out src/registry_resolver.$(BUILD_OBJ_TAG).o src/registry_resolver.$(BUILD_OBJ_TAG).runtime-stats.o,$(REGISTRY_RESOLVER_TEST_LINK_OBJ)) \
		"$$mutation_dir/registry_resolver.o" $(LDFLAGS); \
	if "$$mutation_dir/test_registry_resolver" >/dev/null 2>&1; then \
		echo "FAIL: recognition-bypass resolver mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: recognition-bypass resolver mutation rejected"

test-prime-universal-name-resolver: test-registry-resolver
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@source=tests/prime/universal_name_registry_branch_clone.metta; \
	if [ "$$(grep -Ec '^!\((assertEqual|assertEqualToResult)' "$$source")" -ne 5 ]; then \
		echo "FAIL: structural registry branch-clone guard assertion inventory drifted"; \
		exit 1; \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
	printf '%s\n' "$$result"; \
	if printf '%s\n' "$$result" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc '(PrimeUniversalNameRegistryBranchCloneGuardSummary 5 5 0)')" -ne 1 ]; then \
		echo "FAIL: structural registry branch-clone safety guard"; \
		exit 1; \
	fi
else
	@echo "INFO: structural registry branch-clone guard requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

$(REGISTRY_LOOKUP_BENCH_OBJ): $(REGISTRY_LOOKUP_BENCH_SRC) src/space.h src/name_key.h $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime/bootstrap
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(REGISTRY_LOOKUP_BENCH_BIN): $(REGISTRY_LOOKUP_BENCH_OBJ) $(REGISTRY_LOOKUP_BENCH_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CFLAGS) -o $@ $(REGISTRY_LOOKUP_BENCH_OBJ) $(REGISTRY_LOOKUP_BENCH_LINK_OBJ) $(LDFLAGS)

bench-prime-universal-name-resolver: test-prime-universal-name-resolver $(REGISTRY_LOOKUP_BENCH_BIN)
	@./$(REGISTRY_LOOKUP_BENCH_BIN)

test-runtime-named-var: $(RUNTIME_NAMED_VAR_TEST_BIN)
	@result=$$("$(RUNTIME_NAMED_VAR_TEST_BIN)" 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(RuntimeNamedVarSummary 29 29 0)')" -ne 1 ]; then \
		echo "FAIL: compiled structural-variable reconstruction"; \
		exit 1; \
	fi

test-prime-bare-dollar-parser: $(PRIME_BARE_DOLLAR_PARSER_TEST_BIN)
	@result=$$("$(PRIME_BARE_DOLLAR_PARSER_TEST_BIN)" 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(PrimeBareDollarParserSummary 25 25 0)')" -ne 1 ]; then \
		echo "FAIL: Prime bare-dollar parser tournament"; \
		exit 1; \
	fi

test-prime-bare-dollar-gslt: $(PRIME_SYNTAX_GSLT_ENGINE) \
		$(PRIME_SYNTAX_GSLT_PRESENTATION) \
		$(PRIME_SYNTAX_GSLT_DOLLAR_SYMBOL) \
		tests/support/prime_syntax_gslt_core.metta \
		tests/prime/test_bare_dollar_gslt.py
	@python3 tests/prime/test_bare_dollar_gslt.py \
		--engine "$(PRIME_SYNTAX_GSLT_ENGINE)" \
		--core tests/support/prime_syntax_gslt_core.metta \
		--symbol-presentation "$(PRIME_SYNTAX_GSLT_DOLLAR_SYMBOL)" \
		--variable-presentation "$(PRIME_SYNTAX_GSLT_PRESENTATION)"

test-prime-bare-dollar-reference:
	@python3 tests/prime/test_bare_dollar_reference.py

test-prime-bare-dollar-evaluator: $(BIN) \
		$(PRIME_BARE_DOLLAR_LITERAL_BIN) \
		$(PRIME_BARE_DOLLAR_SHARED_BIN) \
		tests/prime/test_bare_dollar_evaluator.py
	@python3 tests/prime/test_bare_dollar_evaluator.py \
		--literal "$(PRIME_BARE_DOLLAR_LITERAL_BIN)" \
		--fresh "$(BIN)" \
		--shared "$(PRIME_BARE_DOLLAR_SHARED_BIN)"

test-prime-bare-dollar-mutations: $(BIN) \
		$(PRIME_BARE_DOLLAR_LITERAL_BIN) \
		$(PRIME_BARE_DOLLAR_SHARED_BIN) \
		tests/prime/test_bare_dollar_evaluator.py
	@if python3 tests/prime/test_bare_dollar_evaluator.py \
		--literal "$(PRIME_BARE_DOLLAR_LITERAL_BIN)" \
		--fresh "$(PRIME_BARE_DOLLAR_SHARED_BIN)" \
		--shared "$(PRIME_BARE_DOLLAR_SHARED_BIN)" >/dev/null 2>&1; then \
		echo "FAIL: fresh-identity-collapse mutation survived"; \
		exit 1; \
	fi
	@if python3 tests/prime/test_bare_dollar_evaluator.py \
		--literal "$(PRIME_BARE_DOLLAR_LITERAL_BIN)" \
		--fresh "$(BIN)" \
		--shared "$(BIN)" >/dev/null 2>&1; then \
		echo "FAIL: shared-identity-split mutation survived"; \
		exit 1; \
	fi
	@if python3 tests/prime/test_bare_dollar_evaluator.py \
		--literal "$(BIN)" \
		--fresh "$(BIN)" \
		--shared "$(PRIME_BARE_DOLLAR_SHARED_BIN)" >/dev/null 2>&1; then \
		echo "FAIL: literal-to-variable mutation survived"; \
		exit 1; \
	fi
	@echo "PASS: fresh-collapse, shared-split, and literal-variable mutants killed"

test-prime-bare-dollar-tournament: \
		test-prime-bare-dollar-parser \
		test-prime-bare-dollar-gslt \
		test-prime-bare-dollar-reference \
		test-prime-bare-dollar-evaluator \
		test-prime-bare-dollar-mutations

test-prime-universal-name-compile: $(BIN) test-runtime-named-var
	@source=tests/prime/universal_name_compile.metta; \
	ir=$$($(CETTA_BIN_INVOKE) --lang prime --compile "$$source" 2>&1); \
	if command -v "$(LLVM_OPT)" >/dev/null 2>&1; then \
		printf '%s\n' "$$ir" | "$(LLVM_OPT)" -S -o /dev/null 2>/dev/null || exit 1; \
	elif command -v "$(LLVM_CLANG)" >/dev/null 2>&1; then \
		printf '%s\n' "$$ir" | "$(LLVM_CLANG)" -Wno-override-module -x ir -c -o /dev/null - 2>/dev/null || exit 1; \
	else \
		echo "FAIL: structural-name compile test requires $(LLVM_OPT) or $(LLVM_CLANG)"; \
		exit 1; \
	fi; \
	if [ "$$(printf '%s\n' "$$ir" | grep -Fc 'call %Atom* @cetta_atom_named_var')" -ne 1 ] || \
	   ! printf '%s\n' "$$ir" | grep -Fq 'c"(generated \22y\22)\00"'; then \
		echo "FAIL: structural variable identity/presentation was erased by AOT compilation"; \
		exit 1; \
	fi; \
	echo "PASS: structural variable survives AOT IR construction"

test-prime-universal-name-surface: $(BIN) test-name-key test-prime-universal-name-compile
	@source=tests/prime/universal_name_surface.metta; \
	if [ "$$(grep -c '^!(assertEqual' "$$source")" -ne 14 ]; then \
		echo "FAIL: Prime universal-name assertion inventory drifted"; \
		exit 1; \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
	printf '%s\n' "$$result"; \
	if printf '%s\n' "$$result" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc '(PrimeUniversalNameSurfaceSummary 14 14 0)')" -ne 1 ]; then \
		echo "FAIL: Prime universal-name surface"; \
		exit 1; \
	fi; \
	he_result=$$($(CETTA_BIN_INVOKE) --lang he --profile he-compat -e '! @doc' 2>&1); \
	if [ "$$he_result" != '[@doc]' ]; then \
		echo "FAIL: Prime reader option leaked into HE"; \
		exit 1; \
	fi; \
	for display_flag in --pretty-vars --pretty-namespaces; do \
		displayed=$$($(CETTA_BIN_INVOKE) --lang prime "$$display_flag" \
			-e '! $$@(mm-var "ph")' 2>&1); \
		if [ "$$displayed" != '[$$@(mm-var "ph")]' ]; then \
			echo "FAIL: $$display_flag erased a structural variable name"; \
			exit 1; \
		fi; \
	done; \
	if $(CETTA_BIN_INVOKE) --lang prime -e '! $$@(open $$x)' >/dev/null 2>&1; then \
		echo "FAIL: open structural variable name was accepted"; \
		exit 1; \
	fi; \
	backend_source=tests/prime/universal_name_match_backends.metta; \
	if [ "$$(grep -c '^!(assertEqual' "$$backend_source")" -ne 6 ]; then \
		echo "FAIL: Prime universal-name matcher-backend assertion inventory drifted"; \
		exit 1; \
	fi; \
	backend_result=$$($(CETTA_BIN_INVOKE) --lang prime "$$backend_source" 2>&1); \
	printf '%s\n' "$$backend_result"; \
	if printf '%s\n' "$$backend_result" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$backend_result" | grep -Fxc '(PrimeUniversalNameMatchBackendSummary 6 6 0)')" -ne 1 ]; then \
		echo "FAIL: Prime structural names through native matcher backends"; \
		exit 1; \
	fi; \
	registry_source=tests/prime/universal_name_registry.metta; \
	if [ "$$(grep -c '^!(assertEqual' "$$registry_source")" -ne 6 ]; then \
		echo "FAIL: Prime structural-reference assertion inventory drifted"; \
		exit 1; \
	fi; \
	registry_result=$$($(CETTA_BIN_INVOKE) --lang prime "$$registry_source" 2>&1); \
	printf '%s\n' "$$registry_result"; \
	if printf '%s\n' "$$registry_result" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$registry_result" | grep -Fxc '(PrimeUniversalNameRegistrySummary 6 6 0)')" -ne 1 ]; then \
		echo "FAIL: Prime structural-reference registry"; \
		exit 1; \
	fi; \
	unbound=$$($(CETTA_BIN_INVOKE) --lang prime -e '! &@missing' 2>&1); \
	if [ "$$unbound" != '[(resolve-name (quote missing))]' ]; then \
		echo "FAIL: unresolved descriptor was not inert"; \
		exit 1; \
	fi; \
	if $(CETTA_BIN_INVOKE) --lang prime -e '! &@(open $$x)' >/dev/null 2>&1; then \
		echo "FAIL: open structural reference name was accepted"; \
		exit 1; \
	fi; \
	he_ref=$$($(CETTA_BIN_INVOKE) --lang he --profile he-compat -e '! &@doc' 2>&1); \
	if [ "$$he_ref" != '[&@doc]' ]; then \
		echo "FAIL: Prime structural-reference reader leaked into HE"; \
		exit 1; \
	fi; \
	echo "PASS: Prime universal-name profile isolation, malformed-name rejection, native matcher transport, and explicit structural references"

test-prime-universal-name-mutation: $(BIN)
	@mutation_dir=runtime/prime-universal-name-mutation; \
	mkdir -p "$$mutation_dir"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_UNIVERSAL_NAME_MUTATION=1 \
		-c src/parser.c -o "$$mutation_dir/parser.o"; \
	$(CC) $(filter-out src/parser.$(BUILD_OBJ_TAG).o src/parser.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/parser.o" -o "$$mutation_dir/cetta-name-alias" $(LDFLAGS); \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime tests/prime/universal_name_surface.metta 2>&1); \
	if printf '%s\n' "$$baseline" | grep -Eq 'Error|❌'; then \
		echo "FAIL: structural-name mutation baseline is not green"; \
		exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-name-alias" --lang prime tests/prime/universal_name_surface.metta 2>&1); \
	if [ "$$mutant" = "$$baseline" ] || ! printf '%s\n' "$$mutant" | grep -Eq 'Error|❌'; then \
		echo "FAIL: all-structural-names-alias mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: all-structural-names-alias mutation rejected"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_STRUCTURAL_NAME_TRANSPORT_MUTATION=1 \
		-c src/atom.c -o "$$mutation_dir/atom.o"; \
	$(CC) $(filter-out src/atom.$(BUILD_OBJ_TAG).o src/atom.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/atom.o" -o "$$mutation_dir/cetta-name-transport-drop" $(LDFLAGS); \
	transport=$$("$$mutation_dir/cetta-name-transport-drop" --lang prime \
		--pretty-vars -e '! $$@(mm-var "ph")' 2>&1); \
	if [ "$$transport" = '[$$@(mm-var "ph")]' ]; then \
		echo "FAIL: structural-name transport-loss mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: structural-name transport-loss mutation rejected"

test-syn-lanes: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) ./scripts/check_syn_lanes.py \
		--cetta "$(CETTA_SCRIPT_BIN)"

test-prime-syntax-mutation: $(BIN) test-syn-lanes
	@set -eu; \
	mutation_dir=runtime/prime-syntax-mutation; \
	mkdir -p "$$mutation_dir"; \
	for mutation in 1 2 3 4 5 6; do \
		object="$$mutation_dir/parser-$$mutation.o"; \
		binary="$$mutation_dir/cetta-syntax-$$mutation"; \
		$(CC) $(CPPFLAGS) $(CFLAGS) \
			-DCETTA_SYNTAX_NOTATION_MUTATION=$$mutation \
			-c src/parser.c -o "$$object"; \
		$(CC) $(filter-out src/parser.$(BUILD_OBJ_TAG).o src/parser.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
			"$$object" -o "$$binary" $(LDFLAGS); \
		./scripts/check_syn_lanes.py --cetta "$$binary" \
			--expect-mutant-killed $$mutation; \
	done; \
	echo "PASS: all five notation mappings and structural-variable sharing have killed mutations"

test-prime-universal-name-metadata: $(BIN)
	@source=tests/prime/universal_name_metadata.metta; \
	if [ "$$(grep -c '^!(assertEqual' "$$source")" -ne 13 ]; then \
		echo "FAIL: Prime metadata assertion inventory drifted"; \
		exit 1; \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
	printf '%s\n' "$$result"; \
	if printf '%s\n' "$$result" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc '(PrimeUniversalNameMetadataSummary 13 13 0)')" -ne 1 ]; then \
		echo "FAIL: Prime checked metadata records"; \
		exit 1; \
	fi

test-prime-universal-name-syntax-gslt: $(PRIME_SYNTAX_GSLT_ENGINE) \
		$(PRIME_SYNTAX_GSLT_PRESENTATION) $(PRIME_SYNTAX_GSLT_MUTANT) \
		$(PRIME_READER_AST_ORACLE_BIN) tests/support/prime_syntax_gslt_core.metta \
		tests/support/check_prime_syntax_gslt.py
	@python3 tests/support/check_prime_syntax_gslt.py \
		--engine "$(PRIME_SYNTAX_GSLT_ENGINE)" \
		--engine-source tools/gslt2parse.c \
		--core tests/support/prime_syntax_gslt_core.metta \
		--presentation "$(PRIME_SYNTAX_GSLT_PRESENTATION)" \
		--mutant "$(PRIME_SYNTAX_GSLT_MUTANT)" \
		--oracle "$(PRIME_READER_AST_ORACLE_BIN)" \
		--artifact-dir "$(PRIME_SYNTAX_GSLT_DIR)/certificates"

test-prime-universal-name-metadata-mutation: $(BIN)
	@mutation_dir=runtime/prime-universal-name-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_metadata.py \
		--library lib/prime_metadata.metta \
		--gate tests/prime/universal_name_metadata_recognition_gate.metta \
		--output "$$mutation_dir/metadata-baseline.metta"; \
	python3 scripts/mutate_prime_metadata.py \
		--library lib/prime_metadata.metta \
		--gate tests/prime/universal_name_metadata_recognition_gate.metta \
		--output "$$mutation_dir/metadata-bypass-recognition.metta" \
		--bypass-recognition; \
	python3 scripts/mutate_prime_metadata.py \
		--library lib/prime_metadata.metta \
		--gate tests/prime/universal_name_metadata_recognition_gate.metta \
		--output "$$mutation_dir/metadata-drop-effect-demand.metta" \
		--drop-effect-demand; \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime \
		"$$mutation_dir/metadata-baseline.metta" 2>&1); \
	if printf '%s\n' "$$baseline" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$baseline" | grep -Fxc '(PrimeMetadataRecognitionGateSummary 2 2 0)')" -ne 1 ]; then \
		echo "FAIL: metadata recognition mutation baseline is not green"; \
		exit 1; \
	fi; \
	mutant=$$($(CETTA_BIN_INVOKE) --lang prime \
		"$$mutation_dir/metadata-bypass-recognition.metta" 2>&1); \
	if ! printf '%s\n' "$$mutant" | grep -Eq 'Error|❌'; then \
		echo "FAIL: metadata recognition-bypass mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: metadata recognition-bypass mutation rejected"; \
	mutant=$$($(CETTA_BIN_INVOKE) --lang prime \
		"$$mutation_dir/metadata-drop-effect-demand.metta" 2>&1); \
	if ! printf '%s\n' "$$mutant" | grep -Eq 'Error|❌'; then \
		echo "FAIL: metadata lazy-effect-sequencing mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: metadata lazy-effect-sequencing mutation rejected"

$(ABT_MM2_BOUNDARY_TEST_BIN): tests/test_abt_mm2_boundary.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c $(ABT_DEFAULT_SIGNATURES_BLOB) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_abt_mm2_boundary.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c $(LDFLAGS)

test-abt-mm2-boundary: $(ABT_MM2_BOUNDARY_TEST_BIN)
	@result=$$(./$(ABT_MM2_BOUNDARY_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(ABTMM2BoundarySummary 10 10 0)')" -ne 1 ] || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc 'PASS: closed ABT/MM2/MORK boundary')" -ne 1 ]; then \
		echo "FAIL: ABT/MM2/MORK boundary exact summary absent or duplicated"; \
		exit 1; \
	fi

$(ABT_TEST_BIN): tests/test_abt.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(ABT_DEFAULT_SIGNATURES_BLOB) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_abt.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(LDFLAGS)

runtime/test_abt_mutation-$(BUILD_OBJ_TAG)-%: tests/test_abt.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(ABT_DEFAULT_SIGNATURES_BLOB) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_ABT_MUTATION=$* -o $@ tests/test_abt.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(LDFLAGS)

test-abt-mutations: $(ABT_MUTATION_TEST_BINS)
	@set -e; caught=0; \
	for binary in $(ABT_MUTATION_TEST_BINS); do \
		if "$$binary" >/dev/null 2>&1; then \
			echo "FAIL: ABT mutation survived: $$binary"; exit 1; \
		fi; \
		echo "PASS: ABT mutation rejected: $$binary"; \
		caught=$$((caught + 1)); \
	done; \
	echo "PASS: ABT mutation gate ($$caught/$(words $(ABT_MUTATION_TEST_BINS)))"

test-abt-differential: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/check_abt_differential.py "$(CETTA_SCRIPT_BIN)"

test-abt-default-signatures: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/check_abt_default_signatures.py "$(CETTA_SCRIPT_BIN)"

test-abt-integration-ledger:
	@result=$$(python3 scripts/check_abt_integration_status.py --mutation-suite 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(ABTIntegrationLedgerSummary 3 3 0)')" -ne 1 ] || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc '(ABTIntegrationLedgerMutationSummary 5 5 0)')" -ne 1 ]; then \
		echo "FAIL: ABT integration-ledger exact summary absent or duplicated"; \
		exit 1; \
	fi

# Experimental A/B/C adjudication input. This target validates the generic
# declaration-driven constructor without making it part of a language profile
# or selecting a production binder-name domain.
test-abt-scope-construction-candidates: $(BIN)
	@source=tests/abt/scope_construction_candidates.metta; \
	if [ "$$(grep -c '^!(assertEqual' "$$source")" -ne 27 ]; then \
		echo "FAIL: ABT scope-construction candidate assertion inventory drifted"; \
		exit 1; \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --lang prime "$$source" 2>&1); \
	printf '%s\n' "$$result"; \
	if printf '%s\n' "$$result" | grep -Eq 'Error|❌' || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc '(ABTScopeConstructionCandidateSummary 27 27 0)')" -ne 1 ]; then \
		echo "FAIL: declaration-driven ABT scope-construction candidate"; \
		exit 1; \
	fi

test-abt: $(ABT_TEST_BIN) test-abt-mm2-boundary test-rhocalc-abt-substitution test-abt-mutations test-abt-default-signatures test-abt-differential test-lib-parse-abt-bridge test-abt-integration-ledger
	@result=$$(./$(ABT_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(ABTCoreSummary 114 114 0)')" -ne 1 ] || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc 'PASS: iterative capture-avoiding ABT core')" -ne 1 ]; then \
		echo "FAIL: ABT core exact summary absent or duplicated"; \
		exit 1; \
	fi

$(ABT_BENCH_BIN): tests/bench_abt.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(ABT_DEFAULT_SIGNATURES_BLOB) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_abt.c src/symbol.c src/atom.c src/atom_blob.c src/abt.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(LDFLAGS)

bench-abt: $(ABT_BENCH_BIN)
	@result=$$(./$(ABT_BENCH_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fc '(ABTBenchSummary ')" -ne 1 ] || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc 'PASS: ABT balanced-tree performance smoke')" -ne 1 ]; then \
		echo "FAIL: ABT benchmark exact summary absent or duplicated"; \
		exit 1; \
	fi

runtime/bench_mork_bridge_add: tests/bench_mork_bridge_add.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_add.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

$(LIB_PARSE_INFERENCE_BENCH_BIN): tests/bench_lib_parse_inference_native.c src/symbol.c src/atom.c src/lib_parse_inference_native.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_lib_parse_inference_native.c src/symbol.c src/atom.c src/lib_parse_inference_native.c $(LDFLAGS)

bench-lib-parse-inference-native: $(LIB_PARSE_INFERENCE_BENCH_BIN)
	@./$(LIB_PARSE_INFERENCE_BENCH_BIN)

runtime/bench_mork_bridge_query: tests/bench_mork_bridge_query.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c $(PARSER_STANDALONE_SRC) src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_query.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c $(PARSER_STANDALONE_SRC) src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

runtime/bench_mork_bridge_scalar_cursor: tests/bench_mork_bridge_scalar_cursor.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_scalar_cursor.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

runtime/bench_mork_bridge_space_ops: tests/bench_mork_bridge_space_ops.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_space_ops.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

$(MORK_BRIDGE_CONTEXTUAL_EXACT_ROWS_TEST_BIN): tests/test_mork_bridge_contextual_exact_rows.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_mork_bridge_contextual_exact_rows.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-mork-bridge-contextual-exact-rows:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) $(MORK_BRIDGE_CONTEXTUAL_EXACT_ROWS_TEST_BIN)
	@$(call cetta_exec,./$(MORK_BRIDGE_CONTEXTUAL_EXACT_ROWS_TEST_BIN))
else
	$(call reexec_pathmap_bridge_or_skip,mork bridge contextual exact rows packet,$@)
endif

$(MORK_CURSOR_BYTE_BUFFER_COUNT_ABI_TEST_BIN): tests/test_mork_cursor_byte_buffer_count_abi.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_mork_cursor_byte_buffer_count_abi.c $(LDFLAGS)

test-mork-cursor-byte-buffer-count-abi:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) $(MORK_CURSOR_BYTE_BUFFER_COUNT_ABI_TEST_BIN)
	@$(call cetta_exec,./$(MORK_CURSOR_BYTE_BUFFER_COUNT_ABI_TEST_BIN))
else
	$(call reexec_pathmap_bridge_or_skip,mork cursor byte-buffer count ABI,$@)
endif

$(MORK_CURSOR_EXPR_ROW_STREAM_ABI_TEST_BIN): tests/test_mork_cursor_expr_row_stream_abi.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_mork_cursor_expr_row_stream_abi.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-mork-cursor-expr-row-stream-abi:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) $(MORK_CURSOR_EXPR_ROW_STREAM_ABI_TEST_BIN)
	@$(call cetta_exec,./$(MORK_CURSOR_EXPR_ROW_STREAM_ABI_TEST_BIN))
else
	$(call reexec_pathmap_bridge_or_skip,mork cursor expr-row stream ABI,$@)
endif

$(MORK_QUERY_ROW_STREAM_ABI_TEST_BIN): tests/test_mork_query_row_stream_abi.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_mork_query_row_stream_abi.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-mork-query-row-stream-abi:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) $(MORK_QUERY_ROW_STREAM_ABI_TEST_BIN)
	@$(call cetta_exec,./$(MORK_QUERY_ROW_STREAM_ABI_TEST_BIN))
else
	$(call reexec_pathmap_bridge_or_skip,mork query row stream ABI,$@)
endif

$(SPACE_TERM_UNIVERSE_MEMBERSHIP_TEST_BIN): CPPFLAGS += -DCETTA_RUNTIME_STATS_IMPL=1
$(SPACE_TERM_UNIVERSE_MEMBERSHIP_TEST_BIN): tests/test_space_term_universe_membership.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c $(PARSER_STANDALONE_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_space_term_universe_membership.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c $(PARSER_STANDALONE_SRC) $(LDFLAGS)

test-space-term-universe-membership: $(SPACE_TERM_UNIVERSE_MEMBERSHIP_TEST_BIN)
	@$(call cetta_exec,./$(SPACE_TERM_UNIVERSE_MEMBERSHIP_TEST_BIN))

$(TERM_UNIVERSE_STORE_ABI_TEST_BIN): CPPFLAGS += -DCETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS=1 -DCETTA_RUNTIME_STATS_IMPL=1
$(TERM_UNIVERSE_STORE_ABI_TEST_BIN): tests/test_term_universe_store_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c $(PARSER_STANDALONE_SRC) src/cetta_stdlib.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_term_universe_store_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c $(PARSER_STANDALONE_SRC) src/cetta_stdlib.c $(LDFLAGS)

test-term-universe-store-abi: $(TERM_UNIVERSE_STORE_ABI_TEST_BIN)
	@$(call cetta_exec,./$(TERM_UNIVERSE_STORE_ABI_TEST_BIN))

$(TERM_UNIVERSE_BACKEND_ADD_ABI_TEST_BIN): CPPFLAGS += -DCETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS=1 -DCETTA_RUNTIME_STATS_IMPL=1
$(TERM_UNIVERSE_BACKEND_ADD_ABI_TEST_BIN): tests/test_term_universe_backend_add_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_term_universe_backend_add_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) $(LDFLAGS)

test-term-universe-backend-add-abi: $(TERM_UNIVERSE_BACKEND_ADD_ABI_TEST_BIN)
	@$(call cetta_exec,./$(TERM_UNIVERSE_BACKEND_ADD_ABI_TEST_BIN))

$(LET_BRANCH_ARENA_RESET_NO_ESCAPE_TEST_BIN): CPPFLAGS += -DCETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS=1
$(LET_BRANCH_ARENA_RESET_NO_ESCAPE_TEST_BIN): tests/test_let_branch_arena_reset_no_escape.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c $(PARSER_STANDALONE_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_let_branch_arena_reset_no_escape.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c $(PARSER_STANDALONE_SRC) $(LDFLAGS)

test-let-branch-arena-reset-no-escape: $(LET_BRANCH_ARENA_RESET_NO_ESCAPE_TEST_BIN)
	@$(call cetta_exec,./$(LET_BRANCH_ARENA_RESET_NO_ESCAPE_TEST_BIN))

$(PATHMAP_BACKEND_PRIMARY_DESTRUCTIVE_ABI_TEST_BIN): tests/test_pathmap_backend_primary_destructive_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_backend_primary_destructive_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

ifeq ($(ENABLE_PATHMAP_SPACE),1)
test-pathmap-backend-primary-destructive-abi: $(PATHMAP_BACKEND_PRIMARY_DESTRUCTIVE_ABI_TEST_BIN)
	@$(call cetta_exec,./$(PATHMAP_BACKEND_PRIMARY_DESTRUCTIVE_ABI_TEST_BIN))
else
test-pathmap-backend-primary-destructive-abi:
	$(call reexec_pathmap_bridge_or_skip,pathmap backend-primary destructive ABI,$@)
endif

test-pathmap-batch-mutations:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@mutation_dir=runtime/pathmap-batch-mutations; \
	mkdir -p "$$mutation_dir"; \
	for mutation in store-drop-last remove-drop-last; do \
		python3 scripts/mutate_pathmap_batch.py \
			"$$mutation" src/space_match_backend.c \
			"$$mutation_dir/space_match_backend.c" || exit 1; \
		$(CC) $(CPPFLAGS) $(CFLAGS) -o "$$mutation_dir/test-$$mutation" \
			tests/test_pathmap_backend_primary_destructive_abi.c \
			src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) \
			src/subst_tree.c src/term_canon.c src/variant_shape.c \
			src/variant_instance.c src/term_universe.c \
			$(GROUNDED_STANDALONE_SRC) src/native_sha256.c \
			src/search_machine.c src/space.c \
			"$$mutation_dir/space_match_backend.c" \
			$(PARSER_STANDALONE_SRC) src/mm2_lower.c \
			src/mork_space_bridge_runtime.c $(LDFLAGS) || exit 1; \
		if "$$mutation_dir/test-$$mutation" >/dev/null 2>&1; then \
			echo "FAIL: PathMap batch $$mutation mutation survived"; \
			exit 1; \
		fi; \
	done; \
	echo "PASS: counted PathMap batch occurrence-loss mutations are killed"
else
	$(call reexec_pathmap_bridge_or_skip,counted PathMap batch mutation gate,$@)
endif
.PHONY: test-pathmap-batch-mutations

test-pathmap-streaming-d4-mutation: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
ifeq ($(ENABLE_SANITIZERS),1)
	@echo "SKIP: PathMap D4 progress mutation requires a normal optimized binary; sanitizer builds are correctness evidence, not timing evidence"
else
	@mutation_dir=runtime/pathmap-stream-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_pathmap_stream.py src/eval.c \
		"$$mutation_dir/eval.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/eval.c" \
		-o "$$mutation_dir/eval.o" || exit 1; \
	$(CC) $(filter-out src/eval.$(BUILD_OBJ_TAG).o,$(OBJ)) \
		"$$mutation_dir/eval.o" -o "$$mutation_dir/cetta-no-stream" \
		$(LDFLAGS) || exit 1; \
	env $(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV)=1 \
		python3 scripts/bench_d4_progress.py "$(CETTA_SCRIPT_BIN)" \
		tests/nil_pc_fc_d4.metta --backends pathmap --duration 3.5 \
		--output "$$mutation_dir/baseline.json" >/dev/null || exit 1; \
	if env $(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV)=1 \
		python3 scripts/bench_d4_progress.py \
		"$$mutation_dir/cetta-no-stream" tests/nil_pc_fc_d4.metta \
		--backends pathmap --duration 3.5 \
		--output "$$mutation_dir/mutant.json" >/dev/null; then \
		echo "FAIL: PathMap chain-flatten stream mutation survived"; \
		exit 1; \
	fi; \
	python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); r=d["results"][0]; assert d["status"] == "failed" and r["status"] == "bounded-progress" and r["checkpoint_count"] == 0' \
		"$$mutation_dir/mutant.json" || exit 1; \
	echo "PASS: bounded D4 witness kills the PathMap chain-flatten stream mutation"
endif
else
	$(call reexec_pathmap_bridge_or_skip,PathMap chain-flatten stream mutation gate,$@)
endif
.PHONY: test-pathmap-streaming-d4-mutation

$(PATHMAP_BACKEND_PRIMARY_REPLACE_ABI_TEST_BIN): tests/test_pathmap_backend_primary_replace_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_backend_primary_replace_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

ifeq ($(ENABLE_PATHMAP_SPACE),1)
test-pathmap-backend-primary-replace-abi: $(PATHMAP_BACKEND_PRIMARY_REPLACE_ABI_TEST_BIN)
	@$(call cetta_exec,./$(PATHMAP_BACKEND_PRIMARY_REPLACE_ABI_TEST_BIN))
else
test-pathmap-backend-primary-replace-abi:
	$(call reexec_pathmap_bridge_or_skip,pathmap backend-primary replace ABI,$@)
endif

$(PATHMAP_TYPED_QUERY_ABI_TEST_BIN): tests/test_pathmap_typed_query_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_typed_query_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

ifeq ($(ENABLE_PATHMAP_SPACE),1)
test-pathmap-typed-query-abi: $(PATHMAP_TYPED_QUERY_ABI_TEST_BIN)
	@$(call cetta_exec,./$(PATHMAP_TYPED_QUERY_ABI_TEST_BIN))
else
test-pathmap-typed-query-abi:
	$(call reexec_pathmap_bridge_or_skip,pathmap typed query ABI,$@)
endif

$(PATHMAP_SEMI_NAIVE_ABI_TEST_BIN): CPPFLAGS += -DCETTA_RUNTIME_STATS_IMPL=1
$(PATHMAP_SEMI_NAIVE_ABI_TEST_BIN): tests/test_pathmap_semi_naive_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_DEPS) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_semi_naive_abi.c src/symbol.c src/atom.c $(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(GROUNDED_STANDALONE_SRC) src/native_sha256.c src/search_machine.c src/space.c src/space_match_backend.c $(PARSER_STANDALONE_SRC) src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

.PHONY: test-pathmap-semi-naive-abi
ifeq ($(ENABLE_PATHMAP_SPACE),1)
test-pathmap-semi-naive-abi: $(PATHMAP_SEMI_NAIVE_ABI_TEST_BIN)
	@$(call cetta_exec,./$(PATHMAP_SEMI_NAIVE_ABI_TEST_BIN))
else
test-pathmap-semi-naive-abi:
	$(call reexec_pathmap_bridge_or_skip,pathmap semi-naive ABI,$@)
endif

# Stage 0: kernel-only binary (no precompiled stdlib)
STAGE0_OBJ = $(SRC:.c=.$(BUILD_OBJ_TAG).stage0.o)
BUILD_CONFIG_INPUTS = Makefile $(VERSION_FILE)
DEPS = $(OBJ:.o=.d) $(STAGE0_OBJ:.o=.d) \
	$(FALLBACK_EVAL_TEST_OBJ:.o=.d) \
	$(REGISTRY_RESOLVER_TEST_OBJ:.o=.d) \
	$(HE_COMPILED_READER_TEST_OBJ:.o=.d) \
	$(PETTA_COMPILED_READER_TEST_OBJ:.o=.d) \
	$(PETTA_SEARCH_MACHINE_TEST_OBJ:.o=.d) \
	$(PETTA_SPECIALIZER_PREPARE_TEST_OBJ:.o=.d) \
	$(PRIME_COMPILED_READER_TEST_OBJ:.o=.d) \
	$(HE_COMPILED_READER_BENCH_OBJ:.o=.d) \
	$(PRIME_DELAYED_AMBIGUITY_TEST_OBJ:.o=.d) \
	$(PRIME_PACKAGE_VALIDATION_TEST_OBJ:.o=.d) \
	$(RUNTIME_NAMED_VAR_TEST_OBJ:.o=.d) \
	$(PRIME_READER_AST_ORACLE_OBJ:.o=.d) \
	$(PAYLOAD_MAP_CAPACITY_TEST_OBJ:.o=.d) \
	$(RHOCALC_ABT_SUBSTITUTION_TEST_OBJ:.o=.d) \
	$(OSLF_NATIVE_TYPE_VM_V1_OBJ:.o=.d) \
	$(OSLF_NATIVE_TYPE_VM_V1_TEST_OBJ:.o=.d) \
	$(OSLF_NATIVE_TYPE_VM_V1_MATCH_OBJ:.o=.d) \
	$(OSLF_NATIVE_TYPE_VM_V1_VARIANT_OBJ:.o=.d) \
	$(OSLF_NATIVE_TYPE_VM_V1_PRIME_NEED_OBJ:.o=.d) \
	$(OSLF_NATIVE_TYPE_VM_V1_TEST_LINK_OBJ:.o=.d) \
	$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_LINK_OBJ:.o=.d) \
	$(LANGDEF_COMPILER_V1_OBJ:.o=.d) \
	$(LANGDEF_FINITE_HORN_GSLT_V1_OBJ:.o=.d) \
	$(LANGDEF_METTA_EQUATION_COMPILER_V1_OBJ:.o=.d) \
	$(LANGDEF_COMPILER_V1_LINK_OBJ:.o=.d) \
	$(GSLT2PARSE_AUX_OBJ:.o=.d)

# Remake configuration stamps before evaluating object dependencies.  The
# stamp recipes update their generated headers as a side effect; treating the
# stamps as included makefiles makes GNU Make restart with those headers in
# place, so one invocation cannot relink a stale configuration.
-include $(BUILD_CONFIG_STAMP) $(STAGE0_BUILD_CONFIG_STAMP) $(DEPS)

FORCE:

$(BUILD_CONFIG_HEADER): $(BUILD_CONFIG_STAMP)

$(BUILD_CONFIG_STAMP): $(BUILD_CONFIG_INPUTS)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_cfg=$$(mktemp "$(BOOTSTRAP_TMPDIR)/build_config.XXXXXX"); \
	printf '%s\n' '/* autogenerated by Makefile; do not edit */' > "$$tmp_cfg"; \
	printf '#define CETTA_VERSION_STRING "%s"\n' "$(CETTA_VERSION)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_MODE_STRING "%s"\n' "$(BUILD_CANON)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PYTHON %s\n' "$(ENABLE_PYTHON)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_MORK_STATIC %s\n' "$(ENABLE_MORK_STATIC)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PATHMAP_SPACE %s\n' "$(ENABLE_PATHMAP_SPACE)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_GMP %s\n' "$(ENABLE_GMP)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_LIB_PROLOG %s\n' "$(LIB_PROLOG_ENABLED)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PETTA_TYPECHECK_V2 %s\n' "$(ENABLE_PETTA_TYPECHECK_V2)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS %s\n' "$(ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_RUNTIME_STATS %s\n' "$(ENABLE_RUNTIME_STATS)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_RUNTIME_TIMING %s\n' "$(ENABLE_RUNTIME_TIMING)" >> "$$tmp_cfg"; \
	printf '#define CETTA_RHOCOST_COMMIT_AUDIT %s\n' "$(RHOCOST_COMMIT_AUDIT)" >> "$$tmp_cfg"; \
	if [ -f "$(BUILD_CONFIG_HEADER)" ] && cmp -s "$$tmp_cfg" "$(BUILD_CONFIG_HEADER)"; then \
		rm -f "$$tmp_cfg"; \
	else \
		mv "$$tmp_cfg" "$(BUILD_CONFIG_HEADER)"; \
	fi; \
	touch "$@"

$(STAGE0_BUILD_CONFIG_HEADER): $(STAGE0_BUILD_CONFIG_STAMP)

$(STAGE0_BUILD_CONFIG_STAMP): $(BUILD_CONFIG_INPUTS)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_cfg=$$(mktemp "$(BOOTSTRAP_TMPDIR)/build_config.stage0.XXXXXX"); \
	printf '%s\n' '/* autogenerated by Makefile; do not edit */' > "$$tmp_cfg"; \
	printf '#define CETTA_VERSION_STRING "%s"\n' "$(CETTA_VERSION)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_MODE_STRING "%s"\n' "$(BUILD_CANON)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PYTHON %s\n' "$(ENABLE_PYTHON)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_MORK_STATIC %s\n' "$(ENABLE_MORK_STATIC)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PATHMAP_SPACE %s\n' "$(ENABLE_PATHMAP_SPACE)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_GMP %s\n' "$(ENABLE_GMP)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_LIB_PROLOG %s\n' "$(LIB_PROLOG_ENABLED)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PETTA_TYPECHECK_V2 %s\n' "$(ENABLE_PETTA_TYPECHECK_V2)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS %s\n' "$(ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_RUNTIME_STATS 0\n' >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_RUNTIME_TIMING 0\n' >> "$$tmp_cfg"; \
	printf '#define CETTA_RHOCOST_COMMIT_AUDIT %s\n' "$(RHOCOST_COMMIT_AUDIT)" >> "$$tmp_cfg"; \
	if [ -f "$(STAGE0_BUILD_CONFIG_HEADER)" ] && cmp -s "$$tmp_cfg" "$(STAGE0_BUILD_CONFIG_HEADER)"; then \
		rm -f "$$tmp_cfg"; \
	else \
		mv "$$tmp_cfg" "$(STAGE0_BUILD_CONFIG_HEADER)"; \
	fi; \
	touch "$@"

%.$(BUILD_OBJ_TAG).stage0.o: %.c $(STAGE0_BUILD_CONFIG_HEADER)
	$(CC) -Isrc -I. -Iexperiments/gslt2parse_foundation/native $(BRIDGE_CFLAGS) $(PY_CFLAGS) $(GMP_CFLAGS) $(LIB_PROLOG_CFLAGS) -include $(STAGE0_BUILD_CONFIG_HEADER) $(CFLAGS) $(DEPFLAGS) -DCETTA_NO_STDLIB -MF $(@:.o=.d) -c -o $@ $<

$(STAGE0_BIN): $(STAGE0_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

# Stage 1: compile stdlib using stage0
$(STDLIB_BLOB): $(STDLIB_BLOB_STAMP)

ifeq ($(TSAN_ENABLED),1)
$(STDLIB_BLOB_STAMP): $(STDLIB_SRC)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@if [ ! -s "$(STDLIB_BLOB)" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_SANITIZERS=0 $(STDLIB_BLOB); \
	fi
	@touch "$@"
else
$(STDLIB_BLOB_STAMP): $(STAGE0_BIN) $(STDLIB_SRC)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_stage0=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.run.XXXXXX"); \
	tmp_blob=$$(mktemp "$(BOOTSTRAP_TMPDIR)/stdlib_blob.XXXXXX"); \
	trap 'rm -f "$$tmp_stage0" "$$tmp_blob"' EXIT INT TERM; \
	cp "$(STAGE0_BIN)" "$$tmp_stage0"; \
	chmod +x "$$tmp_stage0"; \
	if ! $(call cetta_exec,"$$tmp_stage0") --compile-stdlib $(STDLIB_SRC) > "$$tmp_blob"; then \
		exit 1; \
	fi; \
	if [ -f "$(STDLIB_BLOB)" ] && cmp -s "$$tmp_blob" "$(STDLIB_BLOB)"; then \
		rm -f "$$tmp_blob"; \
	else \
		mv "$$tmp_blob" "$(STDLIB_BLOB)"; \
	fi; \
	touch "$@"
endif

# The same importable MeTTa equation is compiled into a small native atom blob.
$(ABT_DEFAULT_SIGNATURES_BLOB): $(ABT_DEFAULT_SIGNATURES_BLOB_STAMP)

ifeq ($(TSAN_ENABLED),1)
$(ABT_DEFAULT_SIGNATURES_BLOB_STAMP): $(ABT_DEFAULT_SIGNATURES_SRC)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@if [ ! -s "$(ABT_DEFAULT_SIGNATURES_BLOB)" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_SANITIZERS=0 $(ABT_DEFAULT_SIGNATURES_BLOB); \
	fi
	@touch "$@"
else
$(ABT_DEFAULT_SIGNATURES_BLOB_STAMP): $(STAGE0_BIN) $(ABT_DEFAULT_SIGNATURES_SRC)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_stage0=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.abt-signatures.XXXXXX"); \
	tmp_raw=$$(mktemp "$(BOOTSTRAP_TMPDIR)/abt-default-signatures.raw.XXXXXX"); \
	tmp_blob=$$(mktemp "$(BOOTSTRAP_TMPDIR)/abt-default-signatures.XXXXXX"); \
	trap 'rm -f "$$tmp_stage0" "$$tmp_raw" "$$tmp_blob"' EXIT INT TERM; \
	cp "$(STAGE0_BIN)" "$$tmp_stage0"; \
	chmod +x "$$tmp_stage0"; \
	if ! $(call cetta_exec,"$$tmp_stage0") --compile-stdlib $(ABT_DEFAULT_SIGNATURES_SRC) > "$$tmp_raw"; then \
		exit 1; \
	fi; \
	sed 's/STDLIB_BLOB/ABT_DEFAULT_SIGNATURES_BLOB/g' "$$tmp_raw" > "$$tmp_blob"; \
	if [ -f "$(ABT_DEFAULT_SIGNATURES_BLOB)" ] && cmp -s "$$tmp_blob" "$(ABT_DEFAULT_SIGNATURES_BLOB)"; then \
		rm -f "$$tmp_blob"; \
	else \
		mv "$$tmp_blob" "$(ABT_DEFAULT_SIGNATURES_BLOB)"; \
	fi; \
	touch "$@"
endif

# Stage 2: full binary with precompiled stdlib
$(BIN): $(OBJ) $(BRIDGE_DEPS) $(BIN_FORCE)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -Wl,--export-dynamic -o "$$tmp_out" \
		$(filter-out FORCE,$^) $(LDFLAGS); \
	mv "$$tmp_out" $@

$(FALLBACK_EVAL_TEST_BIN): $(FALLBACK_EVAL_TEST_OBJ) $(FALLBACK_EVAL_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-fallback-eval.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(FALLBACK_EVAL_TEST_OBJ): $(FALLBACK_EVAL_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(HE_COMPILED_READER_TEST_BIN): $(HE_COMPILED_READER_TEST_OBJ) $(HE_COMPILED_READER_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-he-compiled-reader.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(HE_COMPILED_READER_TEST_OBJ): $(HE_COMPILED_READER_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(HE_TYPING_DIRECT_TEST_BIN): $(HE_TYPING_DIRECT_TEST_OBJ) $(HE_TYPING_DIRECT_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-he-typing-direct.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(HE_TYPING_DIRECT_TEST_OBJ): $(HE_TYPING_DIRECT_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PETTA_COMPILED_READER_TEST_BIN): $(PETTA_COMPILED_READER_TEST_OBJ) $(PETTA_COMPILED_READER_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-petta-compiled-reader.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PETTA_COMPILED_READER_TEST_OBJ): $(PETTA_COMPILED_READER_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PETTA_SEARCH_MACHINE_TEST_BIN): $(PETTA_SEARCH_MACHINE_TEST_OBJ) $(PETTA_SEARCH_MACHINE_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-petta-search-machine.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PETTA_SEARCH_MACHINE_TEST_OBJ): $(PETTA_SEARCH_MACHINE_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(MATCH_DECISION_TEST_BIN): $(MATCH_DECISION_TEST_OBJ) $(MATCH_DECISION_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-match-decision.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(MATCH_DECISION_TEST_OBJ): $(MATCH_DECISION_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PETTA_SPECIALIZER_PREPARE_TEST_BIN): $(PETTA_SPECIALIZER_PREPARE_TEST_OBJ) $(PETTA_SPECIALIZER_PREPARE_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-petta-specializer-prepare.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PETTA_SPECIALIZER_PREPARE_TEST_OBJ): $(PETTA_SPECIALIZER_PREPARE_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

.PHONY: test-petta-specializer-prepare
test-petta-specializer-prepare: $(PETTA_SPECIALIZER_PREPARE_TEST_BIN)
	@CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER=1 ./$(PETTA_SPECIALIZER_PREPARE_TEST_BIN)

$(PRIME_COMPILED_READER_TEST_BIN): $(PRIME_COMPILED_READER_TEST_OBJ) $(PRIME_COMPILED_READER_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-prime-compiled-reader.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_COMPILED_READER_TEST_OBJ): $(PRIME_COMPILED_READER_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(HE_COMPILED_READER_BENCH_BIN): $(HE_COMPILED_READER_BENCH_OBJ) $(HE_COMPILED_READER_BENCH_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/bench-he-compiled-reader.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(HE_COMPILED_READER_BENCH_OBJ): $(HE_COMPILED_READER_BENCH_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PRIME_DELAYED_AMBIGUITY_TEST_BIN): $(PRIME_DELAYED_AMBIGUITY_TEST_OBJ) $(PRIME_DELAYED_AMBIGUITY_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-prime-delayed-ambiguity.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_DELAYED_AMBIGUITY_TEST_OBJ): $(PRIME_DELAYED_AMBIGUITY_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PRIME_PACKAGE_VALIDATION_TEST_BIN): $(PRIME_PACKAGE_VALIDATION_TEST_OBJ) $(PRIME_PACKAGE_VALIDATION_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-prime-package-validation.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_PACKAGE_VALIDATION_TEST_OBJ): $(PRIME_PACKAGE_VALIDATION_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RUNTIME_NAMED_VAR_TEST_BIN): $(RUNTIME_NAMED_VAR_TEST_OBJ) $(RUNTIME_NAMED_VAR_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-runtime-named-var.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(RUNTIME_NAMED_VAR_TEST_OBJ): $(RUNTIME_NAMED_VAR_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PRIME_BARE_DOLLAR_PARSER_TEST_BIN): $(PRIME_BARE_DOLLAR_PARSER_TEST_OBJ) $(PRIME_BARE_DOLLAR_PARSER_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-prime-bare-dollar-parser.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_BARE_DOLLAR_PARSER_TEST_OBJ): $(PRIME_BARE_DOLLAR_PARSER_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PRIME_BARE_DOLLAR_LITERAL_PARSER_OBJ): src/parser.c src/parser.h $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) \
		-DCETTA_BARE_DOLLAR_DEFAULT_MODE=PARSER_BARE_DOLLAR_SYMBOL \
		-MF $(@:.o=.d) -c -o $@ $<

$(PRIME_BARE_DOLLAR_SHARED_PARSER_OBJ): src/parser.c src/parser.h $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) \
		-DCETTA_BARE_DOLLAR_DEFAULT_MODE=PARSER_BARE_DOLLAR_SHARED_VARIABLE \
		-MF $(@:.o=.d) -c -o $@ $<

$(PRIME_BARE_DOLLAR_LITERAL_BIN): $(PRIME_BARE_DOLLAR_LITERAL_PARSER_OBJ) $(OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-bare-dollar-literal.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(filter-out src/parser.$(BUILD_OBJ_TAG).o src/parser.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		$(PRIME_BARE_DOLLAR_LITERAL_PARSER_OBJ) -o "$$tmp_out" $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_BARE_DOLLAR_SHARED_BIN): $(PRIME_BARE_DOLLAR_SHARED_PARSER_OBJ) $(OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-bare-dollar-shared.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(filter-out src/parser.$(BUILD_OBJ_TAG).o src/parser.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		$(PRIME_BARE_DOLLAR_SHARED_PARSER_OBJ) -o "$$tmp_out" $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_READER_AST_ORACLE_BIN): $(PRIME_READER_AST_ORACLE_OBJ) $(PRIME_READER_AST_ORACLE_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-reader-ast-oracle.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PRIME_READER_AST_ORACLE_OBJ): $(PRIME_READER_AST_ORACLE_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PRIME_SYNTAX_GSLT_ENGINE): tools/gslt2parse.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -Wall -Wextra -Werror -std=c11 -o $@ $<

$(PRIME_SYNTAX_GSLT_PRESENTATION): scripts/build_prime_syntax_gslt.py
	@mkdir -p $(dir $@)
	python3 $< $@

$(PRIME_SYNTAX_GSLT_MUTANT): scripts/build_prime_syntax_gslt.py
	@mkdir -p $(dir $@)
	python3 $< --without-universal-names $@

$(PRIME_SYNTAX_GSLT_DOLLAR_SYMBOL): scripts/build_prime_syntax_gslt.py
	@mkdir -p $(dir $@)
	python3 $< --bare-dollar symbol $@

$(PAYLOAD_MAP_CAPACITY_TEST_BIN): $(PAYLOAD_MAP_CAPACITY_TEST_OBJ) $(PAYLOAD_MAP_CAPACITY_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-rhometta-payload-map-capacity.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PAYLOAD_MAP_CAPACITY_TEST_OBJ): $(PAYLOAD_MAP_CAPACITY_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RHOCALC_ABT_SUBSTITUTION_TEST_BIN): $(RHOCALC_ABT_SUBSTITUTION_TEST_OBJ) $(RHOCALC_ABT_SUBSTITUTION_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-rhocalc-abt-substitution.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(RHOCALC_ABT_SUBSTITUTION_TEST_OBJ): $(RHOCALC_ABT_SUBSTITUTION_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LIB_PARSE_GLL_UTF8_FOREST_TEST_BIN): $(LIB_PARSE_GLL_UTF8_FOREST_TEST_OBJ) $(LIB_PARSE_GLL_UTF8_FOREST_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-lib-parse-gll-utf8-forest.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(LIB_PARSE_GLL_UTF8_FOREST_TEST_OBJ): $(LIB_PARSE_GLL_UTF8_FOREST_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LIB_PARSE_GLR_UTF8_FOREST_TEST_BIN): $(LIB_PARSE_GLR_UTF8_FOREST_TEST_OBJ) $(LIB_PARSE_GLR_UTF8_FOREST_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-lib-parse-glr-utf8-forest.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(LIB_PARSE_GLR_UTF8_FOREST_TEST_OBJ): $(LIB_PARSE_GLR_UTF8_FOREST_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LIB_PARSE_SLR_PREPARED_TEST_BIN): $(LIB_PARSE_SLR_PREPARED_TEST_OBJ) $(LIB_PARSE_SLR_PREPARED_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-lib-parse-slr-prepared.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(LIB_PARSE_SLR_PREPARED_TEST_OBJ): $(LIB_PARSE_SLR_PREPARED_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GLL_V1_TEST_BIN): $(PARSER_PACK_GLL_V1_TEST_OBJ) $(PARSER_PACK_GLL_V1_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-parser-pack-gll-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GLL_V1_TEST_OBJ): $(PARSER_PACK_GLL_V1_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GLR_V1_TEST_BIN): $(PARSER_PACK_GLR_V1_TEST_OBJ) $(PARSER_PACK_GLR_V1_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-parser-pack-glr-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GLR_V1_TEST_OBJ): $(PARSER_PACK_GLR_V1_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_LEXICAL_V1_TEST_BIN): $(PARSER_PACK_LEXICAL_V1_TEST_OBJ) $(PARSER_PACK_LEXICAL_V1_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-parser-pack-lexical-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_LEXICAL_V1_TEST_OBJ): $(PARSER_PACK_LEXICAL_V1_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_BIN): $(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-lexical-plan-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_OBJ): $(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_RELATION_V1_OBJ): $(PARSER_PACK_GUARD_RELATION_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_PLAN_V1_TEST_BIN): $(PARSER_PACK_GUARD_PLAN_V1_TEST_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-parser-pack-guard-plan-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GUARD_PLAN_V1_TEST_OBJ): $(PARSER_PACK_GUARD_PLAN_V1_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_PLAN_V1_OBJ): $(PARSER_PACK_GUARD_PLAN_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN): $(PARSER_PACK_GUARD_PLAN_V1_STREAM_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GUARD_PLAN_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-guard-plan-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GUARD_PLAN_V1_STREAM_OBJ): $(PARSER_PACK_GUARD_PLAN_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ): $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN): $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-guarded-lexical-plan-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_OBJ): $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARDED_LEXICAL_V1_OBJ): $(PARSER_PACK_GUARDED_LEXICAL_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN): $(PARSER_PACK_GUARD_REF_V1_STREAM_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GUARD_REF_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-guard-ref-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GUARD_REF_V1_STREAM_OBJ): $(PARSER_PACK_GUARD_REF_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_REF_V1_OBJ): $(PARSER_PACK_GUARD_REF_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARD_SCALAR_EXEC_V1_OBJ): $(PARSER_PACK_GUARD_SCALAR_EXEC_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN): $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-guarded-lexical-exec-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_OBJ): $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_OBJ): $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_SRC) $(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_HEADER) $(SEMANTIC_MASK_NFA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_CURSOR_C_EMITTER_V1_OBJ): $(PARSER_PACK_CURSOR_C_EMITTER_V1_SRC) experiments/gslt2parse_foundation/native/parser_pack_cursor_c_emitter_v1.h $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_OCCURRENCE_FOLD_V1_OBJ): $(PARSER_OCCURRENCE_FOLD_V1_SRC) $(PARSER_OCCURRENCE_FOLD_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_OCCURRENCE_SPAN_MASK_V1_OBJ): $(PARSER_OCCURRENCE_SPAN_MASK_V1_SRC) $(PARSER_OCCURRENCE_SPAN_MASK_V1_HEADER) $(PARSER_OCCURRENCE_FOLD_V1_HEADER) $(SEMANTIC_MASK_NFA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RELATIONAL_VALUE_LIST_V1_OBJ): $(RELATIONAL_VALUE_LIST_V1_SRC) $(RELATIONAL_VALUE_LIST_V1_HEADER) $(PARSER_OCCURRENCE_FOLD_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_DENSE_BITSET_V1_OBJ): \
		$(GSLT_DENSE_BITSET_V1_SRC) \
		$(GSLT_DENSE_BITSET_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ): \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_SRC) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_INDEXED_VALUE_TABLE_V1_OBJ): \
		$(GSLT_INDEXED_VALUE_TABLE_V1_SRC) \
		$(GSLT_INDEXED_VALUE_TABLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_LITERAL_HOLE_PROGRAM_V1_OBJ): \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_SRC) \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_U32_INDEX_V1_OBJ): \
		$(GSLT_U32_INDEX_V1_SRC) \
		$(GSLT_U32_INDEX_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_U32_SLICE_ARENA_V1_OBJ): \
		$(GSLT_U32_SLICE_ARENA_V1_SRC) \
		$(GSLT_U32_SLICE_ARENA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_EPOCH_SLOTS_V1_OBJ): \
		$(GSLT_EPOCH_SLOTS_V1_SRC) \
		$(GSLT_EPOCH_SLOTS_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(GSLT_GROUND_DENSE_TERM_V1_OBJ): \
		$(GSLT_GROUND_DENSE_TERM_V1_SRC) \
		$(GSLT_GROUND_DENSE_TERM_V1_HEADER) \
		$(GSLT_EPOCH_SLOTS_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RELATIONAL_STACK_PROOF_V1_OBJ): $(RELATIONAL_STACK_PROOF_V1_SRC) $(RELATIONAL_STACK_PROOF_V1_HEADER) $(RELATIONAL_STORE_V1_HEADER) $(RELATIONAL_VALUE_LIST_V1_HEADER) $(GSLT_DENSE_BITSET_V1_HEADER) $(GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER) $(GSLT_INDEXED_VALUE_TABLE_V1_HEADER) $(GSLT_LITERAL_HOLE_PROGRAM_V1_HEADER) $(GSLT_U32_INDEX_V1_HEADER) $(GSLT_U32_SLICE_ARENA_V1_HEADER) $(GSLT_EPOCH_SLOTS_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RELATIONAL_STATE_PROGRAM_V1_OBJ): $(RELATIONAL_STATE_PROGRAM_V1_SRC) $(RELATIONAL_STATE_PROGRAM_V1_HEADER) $(RELATIONAL_STACK_PROOF_V1_HEADER) $(RELATIONAL_STORE_V1_HEADER) $(RELATIONAL_VALUE_LIST_V1_HEADER) $(PARSER_OCCURRENCE_FOLD_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RELATIONAL_STATE_TRANSACTION_V1_TEST_OBJ): \
		$(RELATIONAL_STATE_TRANSACTION_V1_TEST_SRC) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER) \
		$(RELATIONAL_STORE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ $<

$(RELATIONAL_STATE_TRANSACTION_V1_TEST_BIN): \
		$(RELATIONAL_STATE_TRANSACTION_V1_TEST_OBJ) \
		$(RELATIONAL_STATE_TRANSACTION_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_OBJ): \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_HEADER) \
		$(OSLF_NATIVE_TYPE_VM_V1_HEADER) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER) \
		$(RELATIONAL_STORE_V1_HEADER) \
		$(RELATIONAL_VALUE_LIST_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ $<

$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_OBJ): \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER) \
		$(PROOF_GSLT_PLAN_V1_HEADER) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_OBJ): \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_HEADER) \
		$(RELATIONAL_STORE_V1_HEADER) \
		$(RELATIONAL_VALUE_LIST_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PROOF_GSLT_RELATIONAL_MACHINE_V1_OBJ): \
		$(PROOF_GSLT_RELATIONAL_MACHINE_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_MACHINE_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_HEADER) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER) \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_HEADER) \
		$(GSLT_U32_INDEX_V1_HEADER) \
		$(PROOF_STORAGE_PLAN_V1_HEADER) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER) \
		$(RELATIONAL_STORE_V1_HEADER) \
		$(RELATIONAL_VALUE_LIST_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_OBJ): $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_SRC) $(PARSER_OCCURRENCE_FOLD_V1_HEADER) $(PARSER_OCCURRENCE_SPAN_MASK_V1_HEADER) $(RELATIONAL_STATE_PROGRAM_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN): $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_OBJ) $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-cursor-compile-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -Wl,--gc-sections -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

.PHONY: build-parser-pack-cursor-generated-v1
GENERATED_CURSOR_HAS_OCCURRENCE_FOLD ?= 0
GENERATED_CURSOR_HAS_INDEXED_CHECKER_EFFECTS ?= 0
build-parser-pack-cursor-generated-v1: $(PARSER_PACK_CURSOR_GENERATED_V1_LINK_OBJ) $(BUILD_CONFIG_HEADER)
	@test -n "$(GENERATED_CURSOR_C)" -a -f "$(GENERATED_CURSOR_C)"
	@test -n "$(GENERATED_CURSOR_BIN)" -a -n "$(GENERATED_CURSOR_PREFIX)"
	@mkdir -p "$(dir $(GENERATED_CURSOR_BIN))"
	$(CC) $(CPPFLAGS) -Iexperiments/gslt2parse_foundation/native $(CFLAGS) \
		-Wl,--gc-sections \
		-DPP_CURSOR_GENERATED_PREFIX=$(GENERATED_CURSOR_PREFIX) \
		-DPP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD=$(GENERATED_CURSOR_HAS_OCCURRENCE_FOLD) \
		-DPP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS=$(GENERATED_CURSOR_HAS_INDEXED_CHECKER_EFFECTS) \
		-o "$(GENERATED_CURSOR_BIN)" \
		$(PARSER_PACK_CURSOR_GENERATED_V1_TEST_SRC) "$(GENERATED_CURSOR_C)" \
		$(PARSER_PACK_CURSOR_GENERATED_V1_LINK_OBJ) $(LDFLAGS)

$(PARSER_ATOM_PROJECTION_V1_OBJ): $(PARSER_ATOM_PROJECTION_V1_SRC) $(PARSER_ATOM_PROJECTION_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ): $(PARSER_ATOM_PROJECTION_EVENTS_V1_SRC) $(PARSER_ATOM_PROJECTION_EVENTS_V1_HEADER) $(PARSER_ATOM_PROJECTION_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_ATOM_PROJECTION_ACTION_V1_OBJ): $(PARSER_ATOM_PROJECTION_ACTION_V1_SRC) $(PARSER_ATOM_PROJECTION_ACTION_V1_HEADER) $(PARSER_ATOM_PROJECTION_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_ATOM_PROJECTION_DOMAIN_V1_OBJ): $(PARSER_ATOM_PROJECTION_DOMAIN_V1_SRC) $(PARSER_ATOM_PROJECTION_DOMAIN_V1_HEADER) $(PARSER_ATOM_PROJECTION_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_ATOM_PROJECTION_CLOSURE_V1_OBJ): $(PARSER_ATOM_PROJECTION_CLOSURE_V1_SRC) $(PARSER_ATOM_PROJECTION_CLOSURE_V1_HEADER) $(PARSER_ATOM_PROJECTION_DOMAIN_V1_HEADER) $(PARSER_ATOM_PROJECTION_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_BIN): $(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-atom-projection-closure-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_OBJ): $(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_SRC) $(PARSER_ATOM_PROJECTION_CLOSURE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(SEMANTIC_MASK_NFA_V1_OBJ): $(SEMANTIC_MASK_NFA_V1_SRC) $(SEMANTIC_MASK_NFA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(SEMANTIC_MASK_NFA_V1_STREAM_BIN): $(SEMANTIC_MASK_NFA_V1_STREAM_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(SEMANTIC_MASK_NFA_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/semantic-mask-nfa-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(SEMANTIC_MASK_NFA_V1_STREAM_OBJ): $(SEMANTIC_MASK_NFA_V1_STREAM_SRC) $(SEMANTIC_MASK_NFA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_OBJ): $(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_SRC) $(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_HEADER) $(SEMANTIC_MASK_NFA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(HE_DOCUMENT_PIPELINE_V1_STREAM_BIN): $(HE_DOCUMENT_PIPELINE_V1_STREAM_OBJ) $(HE_DOCUMENT_PIPELINE_V1_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(HE_DOCUMENT_PIPELINE_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/he-document-pipeline-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(HE_DOCUMENT_PIPELINE_V1_STREAM_OBJ): $(HE_DOCUMENT_PIPELINE_V1_STREAM_SRC) $(HE_DOCUMENT_PIPELINE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN): $(HE_DOCUMENT_PIPELINE_V1_BENCH_OBJ) $(HE_DOCUMENT_PIPELINE_V1_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(HE_DOCUMENT_PIPELINE_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/he-document-pipeline-v1-bench.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(HE_DOCUMENT_PIPELINE_V1_BENCH_OBJ): $(HE_DOCUMENT_PIPELINE_V1_BENCH_SRC) $(HE_DOCUMENT_PIPELINE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(HE_DOCUMENT_PIPELINE_V1_OBJ): $(HE_DOCUMENT_PIPELINE_V1_SRC) $(HE_DOCUMENT_PIPELINE_V1_HEADER) $(PARSER_ATOM_PROJECTION_V1_HEADER) $(PARSER_ATOM_PROJECTION_EVENTS_V1_HEADER) $(PARSER_ATOM_PROJECTION_ACTION_V1_HEADER) $(PARSER_PACK_SEMANTIC_MASK_BINDING_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PETTA_DOCUMENT_PIPELINE_V1_BIN): $(PETTA_DOCUMENT_PIPELINE_V1_OBJ) $(PARSER_PACK_NATIVE_API_V1_OBJ) $(PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PETTA_DOCUMENT_PIPELINE_V1_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/petta-document-pipeline-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PETTA_DOCUMENT_PIPELINE_V1_LIB): $(PETTA_DOCUMENT_PIPELINE_V1_OBJ) $(PETTA_DOCUMENT_PIPELINE_V1_SHARED_LINK_OBJ) $(PARSER_PACK_NATIVE_API_V1_LIB) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/libcetta-petta-document-pipeline-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) -shared -Wl,-soname,$(notdir $@) -Wl,-rpath,'$$ORIGIN' \
		-o "$$tmp_out" $(PETTA_DOCUMENT_PIPELINE_V1_OBJ) \
		$(PETTA_DOCUMENT_PIPELINE_V1_SHARED_LINK_OBJ) \
		$(PARSER_PACK_NATIVE_API_V1_LIB) $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PETTA_DOCUMENT_PIPELINE_V1_OBJ): $(PETTA_DOCUMENT_PIPELINE_V1_SRC) $(PETTA_DOCUMENT_PIPELINE_V1_HEADER) $(PARSER_PACK_NATIVE_API_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GLL_V1_STREAM_BIN): $(PARSER_PACK_GLL_V1_STREAM_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GLL_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-gll-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GLL_V1_STREAM_OBJ): $(PARSER_PACK_GLL_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GLL_V1_STREAM_READER_OBJ): $(PARSER_PACK_GLL_V1_STREAM_READER_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_SLR_SUMMARY_V1_STREAM_BIN): $(PARSER_PACK_SLR_SUMMARY_V1_STREAM_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_SLR_SUMMARY_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-slr-summary-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_SLR_SUMMARY_V1_STREAM_OBJ): $(PARSER_PACK_SLR_SUMMARY_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_GLR_V1_STREAM_BIN): $(PARSER_PACK_GLR_V1_STREAM_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_GLR_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-pack-glr-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_GLR_V1_STREAM_OBJ): $(PARSER_PACK_GLR_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_NATIVE_API_V1_LIB): $(PARSER_PACK_NATIVE_API_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_PACK_NATIVE_API_V1_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/libcetta-parser-pack-native-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) -shared -Wl,-soname,$(notdir $@) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_PACK_NATIVE_API_V1_OBJ): $(PARSER_PACK_NATIVE_API_V1_SRC) $(PARSER_PACK_NATIVE_API_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(REGULAR_SPAN_DFA_V1_TEST_BIN): $(REGULAR_SPAN_DFA_V1_TEST_OBJ) $(REGULAR_SPAN_DFA_V1_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-regular-span-dfa-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(REGULAR_SPAN_DFA_V1_TEST_OBJ): $(REGULAR_SPAN_DFA_V1_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(REGULAR_SPAN_NFA_V1_TEST_BIN): $(REGULAR_SPAN_NFA_V1_TEST_OBJ) $(REGULAR_SPAN_NFA_V1_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/test-regular-span-nfa-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(REGULAR_SPAN_NFA_V1_TEST_OBJ): $(REGULAR_SPAN_NFA_V1_TEST_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(REGULAR_SPAN_DFA_V1_STREAM_BIN): $(REGULAR_SPAN_DFA_V1_STREAM_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(REGULAR_SPAN_DFA_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/regular-span-dfa-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(REGULAR_SPAN_DFA_V1_STREAM_OBJ): $(REGULAR_SPAN_DFA_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(FINITE_HORN_ANSWER_STREAM_V1_OBJ): $(FINITE_HORN_ANSWER_STREAM_V1_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_OBJ): \
		$(PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_SRC) \
		$(PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_HEADER) \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(PARSER_ACTION_BYTECODE_V1_STREAM_BIN): $(PARSER_ACTION_BYTECODE_V1_STREAM_OBJ) $(FINITE_HORN_ANSWER_STREAM_V1_OBJ) $(PARSER_PACK_GLL_V1_STREAM_READER_OBJ) $(PARSER_ACTION_BYTECODE_V1_STREAM_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/parser-action-bytecode-v1-stream.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(PARSER_ACTION_BYTECODE_V1_STREAM_OBJ): $(PARSER_ACTION_BYTECODE_V1_STREAM_SRC) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

# stdlib objects depend on the generated blob header
src/cetta_stdlib.$(BUILD_OBJ_TAG).o: src/cetta_stdlib.c src/cetta_stdlib.h $(STDLIB_BLOB) $(STDLIB_BLOB_STAMP) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

src/cetta_stdlib.$(BUILD_OBJ_TAG).runtime-stats.o: src/cetta_stdlib.c src/cetta_stdlib.h $(STDLIB_BLOB) $(STDLIB_BLOB_STAMP) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

# Native ABT objects depend on the blob generated from the importable equation.
src/abt.$(BUILD_OBJ_TAG).o: src/abt.c src/abt.h src/atom_blob.h $(ABT_DEFAULT_SIGNATURES_BLOB) $(ABT_DEFAULT_SIGNATURES_BLOB_STAMP) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

src/abt.$(BUILD_OBJ_TAG).runtime-stats.o: src/abt.c src/abt.h src/atom_blob.h $(ABT_DEFAULT_SIGNATURES_BLOB) $(ABT_DEFAULT_SIGNATURES_BLOB_STAMP) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(RULE_MACHINE_PROGRAM_GENERATED_V1): \
		$(RULE_MACHINE_CORE_GSLT_V1) \
		$(RULE_MACHINE_PROGRAM_GSLT_V1) \
		$(RULE_MACHINE_PROGRAM_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py
	python3 $(RULE_MACHINE_PROGRAM_GENERATOR_V1) \
		--core $(RULE_MACHINE_CORE_GSLT_V1) \
		--program-gslt $(RULE_MACHINE_PROGRAM_GSLT_V1) \
		--out $@

src/rule_machine.$(BUILD_OBJ_TAG).stage0.o: $(RULE_MACHINE_PROGRAM_GENERATED_V1)
src/rule_machine.$(BUILD_OBJ_TAG).o: $(RULE_MACHINE_PROGRAM_GENERATED_V1)
src/rule_machine.$(BUILD_OBJ_TAG).runtime-stats.o: $(RULE_MACHINE_PROGRAM_GENERATED_V1)

$(MATCH_DECISION_POLICY_GENERATED_V1): \
		$(MATCH_DECISION_POLICY_GSLT_V1) \
		$(MATCH_DECISION_POLICY_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py
	python3 $(MATCH_DECISION_POLICY_GENERATOR_V1) \
		--policy $(MATCH_DECISION_POLICY_GSLT_V1) \
		--out $@

src/match_decision.$(BUILD_OBJ_TAG).stage0.o: $(MATCH_DECISION_POLICY_GENERATED_V1)
src/match_decision.$(BUILD_OBJ_TAG).o: $(MATCH_DECISION_POLICY_GENERATED_V1)
src/match_decision.$(BUILD_OBJ_TAG).runtime-stats.o: $(MATCH_DECISION_POLICY_GENERATED_V1)

$(LANGDEF_COMPILER_V1_OBJ): $(LANGDEF_COMPILER_V1_SRC) \
		native/langdef_module.h \
		native/langdef_metta_equation_compiler_v1.h \
		$(PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_HEADER) \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LANGDEF_FINITE_HORN_GSLT_V1_OBJ): \
		experiments/gslt2parse_foundation/native/finite_horn_gslt_v1.c \
		experiments/gslt2parse_foundation/native/finite_horn_gslt_v1.h \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LANGDEF_METTA_EQUATION_COMPILER_V1_OBJ): \
		native/langdef_metta_equation_compiler_v1.c \
		native/langdef_metta_equation_compiler_v1.h \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LANGDEF_ARTIFACT_V1_OBJ): native/langdef_module.c \
		native/langdef_module.h $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_LANGDEF_ARTIFACT_ONLY=1 \
		$(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LANGDEF_PARSER_V1_OBJ): src/parser.c src/parser.h $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		$(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(LANGDEF_COMPILER_V1_BIN): $(LANGDEF_COMPILER_V1_OBJ) \
		$(LANGDEF_FINITE_HORN_GSLT_V1_OBJ) \
		$(LANGDEF_METTA_EQUATION_COMPILER_V1_OBJ) \
		$(LANGDEF_COMPILER_V1_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/langdef-compile-v1.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -Wl,--gc-sections -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

$(GSLT_RULE_MUTATOR_V1_BIN): $(GSLT_RULE_MUTATOR_V1_SRC) \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(PROOF_GSLT_PROP_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_PROP_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_PROP_CANARY_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_EVEN_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_EVEN_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_EVEN_CANARY_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_BINDER_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_BINDER_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_BINDER_CANARY_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_SEQUENCE_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_SEQUENCE_EVIDENCE_ABI_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		--source $(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		--query '(proof-sequence-evidence-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_METAMATH_PLAN_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--query '(proof-sequence-evidence-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_SOURCE_FOLD_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(METAMATH_SOURCE_STATE_V1) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		--source $(METAMATH_SOURCE_FOLD_V1) \
		--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
		--source $(METAMATH_SOURCE_STATE_V1) \
		--source $(PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1) \
		--source $(PROOF_GSLT_RELATIONAL_ASSERTION_COMPILER_V1) \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1) \
		--query '(proof-sequence-relational-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1): \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_SOURCE_FOLD_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(METAMATH_SOURCE_STATE_V1) $(METAMATH_SOURCE_PROOF_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		--source $(METAMATH_SOURCE_FOLD_V1) \
		--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
		--source $(METAMATH_SOURCE_STATE_V1) \
		--source $(METAMATH_SOURCE_PROOF_V1) \
		--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		--source $(PROOF_GSLT_TRACE_COMPILER_V1) \
		--source $(PROOF_GSLT_TRACE_INPUT_V1) \
		--source $(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		--source $(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		--query '(proof-relational-projection-artifact-v1 ?record)' \
		--out $@

$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1): \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_SOURCE_FOLD_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(METAMATH_SOURCE_STATE_V1) $(METAMATH_SOURCE_PROOF_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		--source $(METAMATH_SOURCE_FOLD_V1) \
		--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
		--source $(METAMATH_SOURCE_STATE_V1) \
		--source $(METAMATH_SOURCE_PROOF_V1) \
		--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		--source $(PROOF_GSLT_TRACE_COMPILER_V1) \
		--source $(PROOF_GSLT_TRACE_INPUT_V1) \
		--source $(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		--source $(PROOF_GSLT_RELATIONAL_RUNTIME_COMPILER_V1) \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		--source $(METAMATH_PROOF_TRACE_CODEC_V1) \
		--query '(proof-relational-runtime-artifact-v1 ?record)' \
		--out $@

$(PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_SOURCE_V1): \
		$(PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1) \
		$(GSLT_RULE_MUTATOR_V1_BIN)
	@mkdir -p $(dir $@)
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_BRIDGE_V1) --out $@ \
		--rule mm-proof-relational-table-formula --mode delete

$(PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_SOURCE_FOLD_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(METAMATH_SOURCE_STATE_V1) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_SOURCE_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		--source $(METAMATH_SOURCE_FOLD_V1) \
		--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
		--source $(METAMATH_SOURCE_STATE_V1) \
		--source $(PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1) \
		--source $(PROOF_GSLT_RELATIONAL_ASSERTION_COMPILER_V1) \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_SOURCE_V1) \
		--query '(proof-sequence-relational-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_SOURCE_V1): \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) $(GSLT_RULE_MUTATOR_V1_BIN)
	@mkdir -p $(dir $@)
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) --out $@ \
		--rule proof-sequence-role-rule-instantiate-variable --mode delete

$(PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_SOURCE_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_SOURCE_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		--source $(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		--query '(proof-sequence-evidence-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_PROP_DELETE_MP_SOURCE_V1): \
		$(PROOF_GSLT_PROP_CANARY_V1) $(GSLT_RULE_MUTATOR_V1_BIN)
	@mkdir -p $(dir $@)
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_PROP_CANARY_V1) --out $@ \
		--rule prop-rule-modus-ponens --mode delete

$(PROOF_GSLT_PROP_DELETE_MP_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_PROP_DELETE_MP_SOURCE_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_PROP_DELETE_MP_SOURCE_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_SEQUENCE_DELETE_APART_SOURCE_V1): \
		$(PROOF_GSLT_SEQUENCE_CANARY_V1) $(GSLT_RULE_MUTATOR_V1_BIN)
	@mkdir -p $(dir $@)
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_SEQUENCE_CANARY_V1) --out $@ \
		--rule sequence-canary-fact-apart-a-c --mode delete

$(PROOF_GSLT_SEQUENCE_DELETE_APART_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_APART_SOURCE_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		--source $(PROOF_GSLT_SEQUENCE_DELETE_APART_SOURCE_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_SOURCE_V1): \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) $(GSLT_RULE_MUTATOR_V1_BIN)
	@mkdir -p $(dir $@)
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) --out $@ \
		--rule proof-sequence-assertion-rule-apply --mode delete

$(PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_ANSWERS_V1): \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_SOURCE_V1) \
		$(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		--source $(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		--source $(PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_SOURCE_V1) \
		--source $(PROOF_GSLT_SEQUENCE_CANARY_V1) \
		--query '(proof-artifact-v1 ?record)' --out $@

$(METAMATH_SOURCE_PACK_V1) $(METAMATH_SYNTAX_LOCK_V1) &: \
		$(METAMATH_SYNTAX_MANIFEST_V1) \
		$(METAMATH_SYNTAX_CORE_V1) \
		$(METAMATH_LOOKAHEAD_CORE_V1) \
		$(METAMATH_SYNTAX_SOURCE_V1) \
		$(LANGDEF_COMPILER_V1_BIN) \
		$(GSLT2PARSE_PARSER_PACK_COMPILER_SOURCES)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	$(LANGDEF_COMPILER_V1_BIN) parser-pack \
		--manifest $(METAMATH_SYNTAX_MANIFEST_V1) \
		--compiler-root "$(GSLT2PARSE_COMPILER_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations" \
		--pack-out $(METAMATH_SOURCE_PACK_V1) \
		--lock-out $(METAMATH_SYNTAX_LOCK_V1)

$(METAMATH_NORMALIZED_PACK_V1): $(METAMATH_SOURCE_PACK_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) transparent-inline \
		--abi $< --out $@

$(METAMATH_SOURCE_PACK_FACTS_V1): $(METAMATH_SOURCE_PACK_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) pack-facts --abi $< --out $@

$(METAMATH_NORMALIZED_PACK_FACTS_V1): \
		$(METAMATH_NORMALIZED_PACK_V1) $(METAMATH_SOURCE_PACK_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) pack-facts \
		--abi $(METAMATH_NORMALIZED_PACK_V1) \
		--source-abi $(METAMATH_SOURCE_PACK_V1) --out $@

$(METAMATH_LEXICAL_ROOTS_V1): \
		$(METAMATH_LEXICAL_PROJECTION_CORE_V1) \
		$(METAMATH_LEXICAL_PROJECTION_COMPILER_V1) \
		$(METAMATH_LEXICAL_PROJECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_LEXICAL_PROJECTION_CORE_V1) \
		--source $(METAMATH_LEXICAL_PROJECTION_COMPILER_V1) \
		--source $(METAMATH_LEXICAL_PROJECTION_V1) \
		--query '(compile-lexical-root ?root)' --out $@

$(METAMATH_LEXICAL_NFA_V1): $(METAMATH_REGULAR_SOURCES_V1) \
		$(METAMATH_LEXICAL_ROOTS_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) lexical-answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_REGULAR_SOURCES_V1),--source $(source)) \
		--roots $(METAMATH_LEXICAL_ROOTS_V1) --timeout 60 --out $@

$(METAMATH_EMPTY_GUARD_ANSWERS_V1): $(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) empty-answers --out $@

$(METAMATH_EMPTY_GUARD_EVIDENCE_V1): \
		$(METAMATH_NORMALIZED_PACK_V1) $(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) empty-guard-evidence \
		--abi $(METAMATH_NORMALIZED_PACK_V1) --out $@

$(METAMATH_ACTION_BYTECODE_V1): $(METAMATH_NORMALIZED_PACK_FACTS_V1) \
		$(METAMATH_ACTION_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_NORMALIZED_PACK_FACTS_V1) \
		--source $(METAMATH_ACTION_COMPILER_V1) \
		--query '(compile-pack-action-program ?owner ?label ?arity ?action ?code)' \
		--out $@

$(METAMATH_FOLD_PRODUCTION_PART_V1): $(METAMATH_FOLD_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_FOLD_SOURCES_V1),--source $(source)) \
		--query '(occurrence-fold-production-v1 ?label ?node ?operation ?contract)' \
		--out $@

$(METAMATH_FOLD_SHIFT_PART_V1): $(METAMATH_FOLD_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_FOLD_SOURCES_V1),--source $(source)) \
		--query '(occurrence-fold-shift-v1 ?role ?operation)' --out $@

$(METAMATH_FOLD_TOKEN_PART_V1): $(METAMATH_FOLD_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_FOLD_SOURCES_V1),--source $(source)) \
		--query '(occurrence-fold-token-v1 ?tag ?role)' --out $@

$(METAMATH_FOLD_VALUE_TOKEN_PART_V1): $(METAMATH_FOLD_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_FOLD_SOURCES_V1),--source $(source)) \
		--query '(occurrence-fold-token-v3 ?tag ?role ?node ?source-label ?normalized-label)' \
		--out $@

$(METAMATH_OCCURRENCE_FOLD_V1): \
		$(METAMATH_FOLD_PRODUCTION_PART_V1) \
		$(METAMATH_FOLD_SHIFT_PART_V1) \
		$(METAMATH_FOLD_TOKEN_PART_V1) \
		$(METAMATH_FOLD_VALUE_TOKEN_PART_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		$(foreach source,$(filter %.answers,$^),--source $(source)) --out $@

$(METAMATH_OCCURRENCE_SPAN_MASK_FOLD_FACTS_V1): \
		$(METAMATH_OCCURRENCE_FOLD_V1) $(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answer-facts \
		--source $(METAMATH_OCCURRENCE_FOLD_V1) --out $@

$(METAMATH_OCCURRENCE_SPAN_MASK_NFA_WRAPPER_V1): \
		$(METAMATH_OCCURRENCE_SPAN_MASK_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_OCCURRENCE_SPAN_MASK_SOURCES_V1),--source $(source)) \
		--query '(compile-occurrence-span-mask-nfa-v1 ?record)' \
		--timeout 120 --out $@

$(METAMATH_OCCURRENCE_SPAN_MASK_NFA_PART_V1): \
		$(METAMATH_OCCURRENCE_SPAN_MASK_NFA_WRAPPER_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head compile-occurrence-span-mask-nfa-v1 --arg 0 --out $@

$(METAMATH_OCCURRENCE_SPAN_MASK_LINK_WRAPPER_V1): \
		$(METAMATH_OCCURRENCE_SPAN_MASK_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_OCCURRENCE_SPAN_MASK_SOURCES_V1),--source $(source)) \
		--query '(compile-occurrence-span-mask-link-v1 ?record)' \
		--timeout 120 --out $@

$(METAMATH_OCCURRENCE_SPAN_MASK_LINK_PART_V1): \
		$(METAMATH_OCCURRENCE_SPAN_MASK_LINK_WRAPPER_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head compile-occurrence-span-mask-link-v1 --arg 0 --out $@

$(METAMATH_OCCURRENCE_SPAN_MASK_V1): \
		$(METAMATH_OCCURRENCE_SPAN_MASK_NFA_PART_V1) \
		$(METAMATH_OCCURRENCE_SPAN_MASK_LINK_PART_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		$(foreach source,$(filter %.answers,$^),--source $(source)) --out $@

$(METAMATH_STATE_TABLE_PART_V1): $(METAMATH_STATE_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_STATE_SOURCES_V1),--source $(source)) \
		--query '(state-table-v1 ?table ?arity ?key-arity ?lifetime)' \
		--out $@

$(METAMATH_STATE_ACTION_PART_V1): $(METAMATH_STATE_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_STATE_SOURCES_V1),--source $(source)) \
		--query '(state-action-v1 ?operation ?index ?action)' --out $@

$(METAMATH_STATE_PROOF_MACHINE_PART_V1): $(METAMATH_STATE_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_STATE_SOURCES_V1),--source $(source)) \
		--query '(state-proof-machine-v1 ?machine ?configuration)' --out $@

$(METAMATH_STATE_FINAL_PART_V1): $(METAMATH_STATE_SOURCES_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(METAMATH_STATE_SOURCES_V1),--source $(source)) \
		--query '(state-final-action-v1 ?index ?action)' --out $@

$(METAMATH_STATE_PROGRAM_V1): \
		$(METAMATH_STATE_TABLE_PART_V1) \
		$(METAMATH_STATE_PROOF_MACHINE_PART_V1) \
		$(METAMATH_STATE_ACTION_PART_V1) \
		$(METAMATH_STATE_FINAL_PART_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		$(foreach source,$(filter %.answers,$^),--source $(source)) --out $@

$(METAMATH_CURSOR_FOLD_GENERATED_C_V1): \
		$(METAMATH_NORMALIZED_PACK_V1) $(METAMATH_LEXICAL_NFA_V1) \
		$(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		$(METAMATH_EMPTY_GUARD_EVIDENCE_V1) \
		$(METAMATH_ACTION_BYTECODE_V1) $(METAMATH_OCCURRENCE_FOLD_V1) \
		$(METAMATH_OCCURRENCE_SPAN_MASK_V1) \
		$(METAMATH_STATE_PROGRAM_V1) \
		$(METAMATH_REGULAR_COMPILER_V1) \
		$(METAMATH_GUARDED_COMPILER_V1) $(METAMATH_ACTION_COMPILER_V1) \
		$(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN)
	$(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN) \
		--abi $(METAMATH_NORMALIZED_PACK_V1) \
		--lexical-answers $(METAMATH_LEXICAL_NFA_V1) \
		--guard-answers $(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		--guard-evidence $(METAMATH_EMPTY_GUARD_EVIDENCE_V1) \
		--guarded-answers $(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		--regular-compiler-digest $(METAMATH_REGULAR_COMPILER_DIGEST_V1) \
		--guarded-compiler-digest $(METAMATH_GUARDED_COMPILER_DIGEST_V1) \
		--action-answers $(METAMATH_ACTION_BYTECODE_V1) \
		--action-compiler-digest $(METAMATH_ACTION_COMPILER_DIGEST_V1) \
		--occurrence-fold-answers $(METAMATH_OCCURRENCE_FOLD_V1) \
		--occurrence-span-mask-answers $(METAMATH_OCCURRENCE_SPAN_MASK_V1) \
		--occurrence-span-mask-compiler-digest $(METAMATH_OCCURRENCE_SPAN_MASK_COMPILER_DIGEST_V1) \
		--state-answers $(METAMATH_STATE_PROGRAM_V1) \
		--out-c $@ --prefix $(METAMATH_CURSOR_FOLD_PREFIX_V1)

$(METAMATH_COMPILED_CURSOR_V1): \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		$(LANGDEF_COMPILED_CURSOR_EXPORT_V1) \
		$(LANGDEF_COMPILED_CURSOR_HEADER_V1) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/langdef-compiled-cursor.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -Wl,--build-id=none \
		-DCETTA_LANGDEF_COMPILED_PREFIX=$(METAMATH_CURSOR_FOLD_PREFIX_V1) \
		-DCETTA_LANGDEF_COMPILED_HAS_OCCURRENCE_SPAN_MASK=1 \
		-DCETTA_LANGDEF_COMPILED_HAS_STATE_PROGRAM=1 \
		-o "$$tmp_out" $(LANGDEF_COMPILED_CURSOR_EXPORT_V1) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1); \
	mv "$$tmp_out" $@

$(METAMATH_AUTHORING_NTT_V1): \
		$(METAMATH_AUTHORING_COMPOSITION_V1) \
		$(METAMATH_AUTHORING_COMPOSED_SOURCES_V1) \
		$(FINITE_HORN_REFLECTION_V1) $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) oslf-ntt \
		--composition $(METAMATH_AUTHORING_COMPOSITION_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--reflection $(FINITE_HORN_REFLECTION_V1) \
		--compiler $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--timeout 120 --out $@

$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1): \
		$(PROOF_GSLT_METAMATH_PLAN_V1) $(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answer-facts \
		--source $(PROOF_GSLT_METAMATH_PLAN_V1) --out $@

$(METAMATH_PROOF_SEMANTIC_OPERATOR_PART_V1): \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		$(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		--query '(source-operator ?owner ?name ?arity)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_SEMANTIC_RULE_PART_V1): \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		$(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		--query '(source-rule ?owner ?rule)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_SEMANTIC_REQUIRED_V1): \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		$(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		--query '(proof-semantic-required-v1 ?key)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_SEMANTIC_DENOTED_V1): \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		$(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		--query '(proof-semantic-denoted-v1 ?key)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_SEMANTIC_REQUIRED_KEYS_V1): \
		$(METAMATH_PROOF_SEMANTIC_REQUIRED_V1) $(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head proof-semantic-required-v1 --arg 0 --out $@

$(METAMATH_PROOF_SEMANTIC_DENOTED_KEYS_V1): \
		$(METAMATH_PROOF_SEMANTIC_DENOTED_V1) $(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head proof-semantic-denoted-v1 --arg 0 --out $@

$(METAMATH_PROOF_SEMANTIC_REFLECTION_V1): \
		$(METAMATH_PROOF_SEMANTIC_OPERATOR_PART_V1) \
		$(METAMATH_PROOF_SEMANTIC_RULE_PART_V1) \
		$(METAMATH_PROOF_SEMANTIC_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_SEMANTIC_DENOTED_KEYS_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	cmp $(METAMATH_PROOF_SEMANTIC_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_SEMANTIC_DENOTED_KEYS_V1)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		--source $(METAMATH_PROOF_SEMANTIC_OPERATOR_PART_V1) \
		--source $(METAMATH_PROOF_SEMANTIC_RULE_PART_V1) --out $@

$(METAMATH_PROOF_SEMANTIC_GSLT_V1): \
		$(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answer-facts \
		--source $(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) --out $@

$(METAMATH_PROOF_SEMANTIC_EXEC_V1): \
		$(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) semantic-gslt \
		--source $(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) --out $@

$(METAMATH_PROOF_SEMANTIC_NTT_V1): \
		$(METAMATH_PROOF_SEMANTIC_GSLT_V1) \
		$(PROOF_GSLT_FIRST_ORDER_SEMANTIC_INTERFACE_V1) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_GSLT_V1) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		--reflect-source $(PROOF_GSLT_FIRST_ORDER_SEMANTIC_INTERFACE_V1) \
		--source $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--query '(compile-oslf-native-type-v1 ?fact)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_MACHINE_NTT_V1): \
		$(METAMATH_PROOF_MACHINE_COMPOSITION_V1) \
		$(METAMATH_PROOF_MACHINE_NTT_V1_SOURCES) \
		$(FINITE_HORN_REFLECTION_V1) $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) oslf-ntt \
		--composition $(METAMATH_PROOF_MACHINE_COMPOSITION_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--reflection $(FINITE_HORN_REFLECTION_V1) \
		--compiler $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--timeout 120 --out $@

$(METAMATH_PROOF_STORAGE_COMBINED_V1): \
		$(METAMATH_STATE_PROGRAM_V1) \
		$(PROOF_GSLT_METAMATH_PLAN_V1) \
		$(PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		$(foreach source,$(filter %.answers,$^),--source $(source)) --out $@

$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1): \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answer-facts \
		--source $(METAMATH_PROOF_STORAGE_COMBINED_V1) --out $@

$(METAMATH_PROOF_STORAGE_REQUIRED_V1): \
		$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
		--query '(proof-storage-required-v1 ?key)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_STORAGE_DENOTED_V1): \
		$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
		--query '(proof-storage-denoted-v1 ?key)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_STORAGE_REQUIRED_KEYS_V1): \
		$(METAMATH_PROOF_STORAGE_REQUIRED_V1) $(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head proof-storage-required-v1 --arg 0 --out $@

$(METAMATH_PROOF_STORAGE_DENOTED_KEYS_V1): \
		$(METAMATH_PROOF_STORAGE_DENOTED_V1) $(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head proof-storage-denoted-v1 --arg 0 --out $@

$(METAMATH_PROOF_STORAGE_PLAN_V1): \
		$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(METAMATH_PROOF_STORAGE_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_STORAGE_DENOTED_KEYS_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	cmp $(METAMATH_PROOF_STORAGE_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_STORAGE_DENOTED_KEYS_V1)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
		--query '(compile-proof-storage-plan-v1 ?fact)' \
		--timeout 120 --out $@

$(METAMATH_LANGDEF_LOCK_V1): \
		$(METAMATH_LANGDEF_MANIFEST_V1) $(METAMATH_COMPILED_CURSOR_V1) \
		$(METAMATH_NORMALIZED_PACK_V1) $(METAMATH_REGULAR_SOURCES_V1) \
		$(METAMATH_LEXICAL_PROJECTION_CORE_V1) \
		$(METAMATH_LEXICAL_PROJECTION_COMPILER_V1) \
		$(METAMATH_LEXICAL_PROJECTION_V1) \
		$(METAMATH_ACTION_COMPILER_V1) \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_SOURCE_FOLD_V1) \
		$(METAMATH_OCCURRENCE_FOLD_COMPILER_V1) \
		$(METAMATH_OCCURRENCE_FOLD_PLAN_COMPILER_V1) \
		$(SEMANTIC_MASK_SPAN_COMPILER_V1) \
		$(PARSER_OCCURRENCE_SPAN_MASK_COMPILER_V1) \
		$(METAMATH_OCCURRENCE_SPAN_MASK_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(METAMATH_RELATIONAL_STATE_COMPILER_V1) \
		$(METAMATH_SOURCE_STATE_V1) $(METAMATH_SOURCE_PROOF_V1) \
		$(METAMATH_STATE_PROGRAM_V1) \
		$(PROOF_GSLT_METAMATH_PLAN_V1) \
		$(PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		$(METAMATH_AUTHORING_COMPOSITION_V1) \
		$(METAMATH_PROOF_MACHINE_COMPOSITION_V1) \
		$(PROOF_GSLT_ARTICLE_INTERFACE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_INTERFACE_V1) \
		$(FINITE_HORN_REFLECTION_V1) $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		$(METAMATH_PROOF_SEMANTIC_GSLT_V1) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_INTERFACE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_INTERFACE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		$(METAMATH_PROOF_STORAGE_PLAN_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest $(METAMATH_LANGDEF_MANIFEST_V1) \
		--pack $(METAMATH_NORMALIZED_PACK_V1) \
		--compiled-cursor $(METAMATH_COMPILED_CURSOR_V1) \
		--lock-out $@

.PHONY: generate-metamath-cogslt-syntax-v1
generate-metamath-cogslt-syntax-v1: \
		$(METAMATH_SOURCE_PACK_V1) $(METAMATH_SYNTAX_LOCK_V1) \
		$(METAMATH_NORMALIZED_PACK_V1) \
		$(METAMATH_SOURCE_PACK_FACTS_V1) \
		$(METAMATH_NORMALIZED_PACK_FACTS_V1) \
		$(METAMATH_LEXICAL_ROOTS_V1) $(METAMATH_LEXICAL_NFA_V1) \
		$(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		$(METAMATH_EMPTY_GUARD_EVIDENCE_V1) \
		$(METAMATH_ACTION_BYTECODE_V1) $(METAMATH_OCCURRENCE_FOLD_V1) \
		$(METAMATH_OCCURRENCE_SPAN_MASK_V1) \
		$(METAMATH_STATE_PROGRAM_V1) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1)

.PHONY: generate-metamath-cogslt-langdef-v1
generate-metamath-cogslt-langdef-v1: \
		generate-metamath-cogslt-syntax-v1 \
		$(METAMATH_COMPILED_CURSOR_V1) \
		$(PROOF_GSLT_METAMATH_PLAN_V1) \
		$(PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1) \
		$(METAMATH_PROOF_SEMANTIC_GSLT_V1) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1) \
		$(METAMATH_PROOF_STORAGE_PLAN_V1) \
		$(METAMATH_LANGDEF_LOCK_V1)

.PHONY: test-metamath-cogslt-regeneration-v1
test-metamath-cogslt-regeneration-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	regen_dir=$$(realpath "$$(mktemp -d runtime/metamath-cogslt-regeneration.XXXXXX)"); \
	trap 'rm -rf "$$regen_dir"' EXIT INT TERM; \
	mkdir -p "$$regen_dir/generated"; \
	$(MAKE) --no-print-directory -B \
		BUILD=$(BUILD) ENABLE_RUNTIME_STATS=0 ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		METAMATH_SOURCE_PACK_V1="$$regen_dir/generated/syntax_parser_pack_v1.abi" \
		METAMATH_SYNTAX_LOCK_V1="$$regen_dir/generated/syntax_lock_v1.metta" \
		METAMATH_NORMALIZED_PACK_V1="$$regen_dir/generated/normalized_parser_pack_v1.abi" \
		METAMATH_SOURCE_PACK_FACTS_V1="$$regen_dir/generated/source_parser_pack_facts_v1.metta" \
		METAMATH_NORMALIZED_PACK_FACTS_V1="$$regen_dir/generated/normalized_parser_pack_facts_v1.metta" \
		METAMATH_LEXICAL_ROOTS_V1="$$regen_dir/generated/lexical_roots_v1.answers" \
		METAMATH_LEXICAL_NFA_V1="$$regen_dir/generated/lexical_nfa_v1.answers" \
		METAMATH_EMPTY_GUARD_ANSWERS_V1="$$regen_dir/generated/empty_guard_v1.answers" \
		METAMATH_EMPTY_GUARD_EVIDENCE_V1="$$regen_dir/generated/empty_guard_v1.evidence" \
		METAMATH_ACTION_BYTECODE_V1="$$regen_dir/generated/action_bytecode_v1.answers" \
		METAMATH_OCCURRENCE_FOLD_V1="$$regen_dir/generated/occurrence_fold_v1.answers" \
		METAMATH_OCCURRENCE_SPAN_MASK_V1="$$regen_dir/generated/occurrence_span_mask_v1.answers" \
		METAMATH_STATE_PROGRAM_V1="$$regen_dir/generated/state_program_v1.answers" \
		METAMATH_CURSOR_FOLD_GENERATED_C_V1="$$regen_dir/generated/syntax_cursor_fold_v1.generated.c" \
		METAMATH_COMPILED_CURSOR_V1="$$regen_dir/generated/syntax_cursor_fold_v1.generated.so" \
		PROOF_GSLT_METAMATH_PLAN_V1="$$regen_dir/generated/proof_plan_v1.answers" \
		PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1="$$regen_dir/generated/proof_evidence_abi_v1.answers" \
		PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1="$$regen_dir/generated/proof_relational_abi_v1.answers" \
		PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1="$$regen_dir/generated/proof_relational_projection_v1.answers" \
		PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1="$$regen_dir/generated/proof_relational_runtime_v1.answers" \
		METAMATH_AUTHORING_NTT_V1="$$regen_dir/generated/authoring_ntt_v1.answers" \
		METAMATH_PROOF_SEMANTIC_PAYLOAD_V1="$$regen_dir/generated/proof_semantic_payload_v1.metta" \
		METAMATH_PROOF_SEMANTIC_GSLT_V1="$$regen_dir/generated/proof_semantic_gslt_v1.metta" \
		METAMATH_PROOF_SEMANTIC_EXEC_V1="$$regen_dir/generated/proof_semantic_exec_v1.metta" \
		METAMATH_PROOF_SEMANTIC_NTT_V1="$$regen_dir/generated/proof_semantic_ntt_v1.answers" \
		METAMATH_PROOF_MACHINE_NTT_V1="$$regen_dir/generated/proof_machine_ntt_v1.answers" \
		METAMATH_PROOF_SEMANTIC_STAGE_DIR_V1="$$regen_dir/proof-semantic-parts" \
		METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1="$$regen_dir/generated/proof_storage_analysis_payload_v1.metta" \
		METAMATH_PROOF_STORAGE_PLAN_V1="$$regen_dir/generated/proof_storage_plan_v1.answers" \
		METAMATH_PROOF_STORAGE_STAGE_DIR_V1="$$regen_dir/proof-storage-parts" \
		METAMATH_LANGDEF_LOCK_V1="$$regen_dir/generated/langdef_lock_v1.metta" \
		METAMATH_FOLD_PART_DIR_V1="$$regen_dir/fold-parts" \
		METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1="$$regen_dir/occurrence-span-mask-parts" \
		METAMATH_STATE_PART_DIR_V1="$$regen_dir/state-parts" \
		generate-metamath-cogslt-langdef-v1 >/dev/null; \
	for artifact in \
		syntax_parser_pack_v1.abi syntax_lock_v1.metta \
		normalized_parser_pack_v1.abi source_parser_pack_facts_v1.metta \
		normalized_parser_pack_facts_v1.metta lexical_roots_v1.answers \
		lexical_nfa_v1.answers empty_guard_v1.answers \
		empty_guard_v1.evidence action_bytecode_v1.answers \
		occurrence_fold_v1.answers occurrence_span_mask_v1.answers \
		state_program_v1.answers \
		proof_plan_v1.answers proof_evidence_abi_v1.answers \
		proof_relational_abi_v1.answers \
		proof_relational_projection_v1.answers \
		proof_relational_runtime_v1.answers \
		authoring_ntt_v1.answers \
		proof_semantic_payload_v1.metta \
		proof_semantic_gslt_v1.metta \
		proof_semantic_exec_v1.metta \
		proof_semantic_ntt_v1.answers \
		proof_machine_ntt_v1.answers \
		proof_storage_analysis_payload_v1.metta \
		proof_storage_plan_v1.answers \
		syntax_cursor_fold_v1.generated.c \
		syntax_cursor_fold_v1.generated.so langdef_lock_v1.metta; do \
		cmp "$$regen_dir/generated/$$artifact" \
			"$(METAMATH_LANGDEF_GENERATED_DIR_V1)/$$artifact"; \
	done; \
	printf '%s\n' '(MetamathCoGSLTRegenerationV1Summary 29 29 0)'

.PHONY: test-metamath-cogslt-authoring-ntt-v1
test-metamath-cogslt-authoring-ntt-v1: \
		$(METAMATH_AUTHORING_NTT_V1) $(LANGDEF_COMPILER_V1_BIN) \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) $(GSLT_RULE_MUTATOR_V1_BIN)
	@set -eu; \
	mutation_dir=$$(mktemp -d runtime/metamath-authoring-ntt.XXXXXX); \
	trap 'rm -rf "$$mutation_dir"' EXIT INT TERM; \
	$(LANGDEF_COMPILER_V1_BIN) oslf-ntt \
		--composition $(METAMATH_AUTHORING_COMPOSITION_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--reflection $(FINITE_HORN_REFLECTION_V1) \
		--compiler $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--timeout 120 --out "$$mutation_dir/regenerated.answers" \
		>/dev/null; \
	cmp "$$mutation_dir/regenerated.answers" $(METAMATH_AUTHORING_NTT_V1); \
	test "$$(wc -l <$(METAMATH_AUTHORING_NTT_V1))" -eq 1160; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-carrier-v1 ' \
		$(METAMATH_AUTHORING_NTT_V1))" -eq 1; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-native-former-v1 ' \
		$(METAMATH_AUTHORING_NTT_V1))" -eq 7; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-head-signature-v1 ' \
		$(METAMATH_AUTHORING_NTT_V1))" -eq 171; \
	rg -F -x -q '(compile-oslf-native-type-v1 (oslf-head-signature-v1 source-proof-conversion (q-succ (q-succ (q-succ q-zero)))))' \
		$(METAMATH_AUTHORING_NTT_V1); \
	rg -F -x -q '(compile-oslf-native-type-v1 (oslf-head-signature-v1 source-state-final-action (q-succ (q-succ q-zero))))' \
		$(METAMATH_AUTHORING_NTT_V1); \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-step-schema-v1 ' \
		$(METAMATH_AUTHORING_NTT_V1))" -eq 969; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-external-relation-v1 ' \
		$(METAMATH_AUTHORING_NTT_V1))" -eq 8; \
	for lane in delete falsify; do \
		mkdir -p "$$mutation_dir/$$lane"; \
		cp --parents $(METAMATH_AUTHORING_COMPOSED_SOURCES_V1) \
			$(METAMATH_AUTHORING_COMPOSITION_V1) \
			"$$mutation_dir/$$lane"; \
	done; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source "$$mutation_dir/delete/$(PROOF_GSLT_METAMATH_CALCULUS_V1)" \
		--out "$$mutation_dir/delete/proof-calculus.mutated" \
		--rule mm-proof-calculus-provable-judgment --mode delete; \
	mv "$$mutation_dir/delete/proof-calculus.mutated" \
		"$$mutation_dir/delete/$(PROOF_GSLT_METAMATH_CALCULUS_V1)"; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source "$$mutation_dir/falsify/$(PROOF_GSLT_METAMATH_CALCULUS_V1)" \
		--out "$$mutation_dir/falsify/proof-calculus.mutated" \
		--rule mm-proof-calculus-provable-judgment --mode falsify \
		--replacement \
		'(source-proof-judgment MetamathProofV1 MetamathProvableV1 proof-zero-v1)'; \
	mv "$$mutation_dir/falsify/proof-calculus.mutated" \
		"$$mutation_dir/falsify/$(PROOF_GSLT_METAMATH_CALCULUS_V1)"; \
	for lane in delete falsify; do \
		$(LANGDEF_COMPILER_V1_BIN) oslf-ntt \
			--composition \
			"$$mutation_dir/$$lane/$(METAMATH_AUTHORING_COMPOSITION_V1)" \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--reflection $(FINITE_HORN_REFLECTION_V1) \
			--compiler $(OSLF_NATIVE_TYPE_COMPILER_V1) \
			--timeout 120 \
			--out "$$mutation_dir/$$lane/authoring-ntt.answers" \
			>/dev/null; \
		if cmp -s $(METAMATH_AUTHORING_NTT_V1) \
			"$$mutation_dir/$$lane/authoring-ntt.answers"; then \
			exit 1; \
		fi; \
	done; \
	test "$$(wc -l <"$$mutation_dir/delete/authoring-ntt.answers")" \
		-eq 1159; \
	test "$$(wc -l <"$$mutation_dir/falsify/authoring-ntt.answers")" \
		-eq 1160; \
	test "$$(rg -c 'mm-proof-calculus-provable-judgment' \
		$(METAMATH_AUTHORING_NTT_V1))" -eq 1; \
	if rg -q 'mm-proof-calculus-provable-judgment' \
		"$$mutation_dir/delete/authoring-ntt.answers"; then exit 1; fi; \
	falsified=$$(rg 'mm-proof-calculus-provable-judgment' \
		"$$mutation_dir/falsify/authoring-ntt.answers"); \
	printf '%s\n' "$$falsified" | rg -q 'proof-zero-v1'; \
	if printf '%s\n' "$$falsified" | rg -q 'proof-succ-v1'; then exit 1; fi; \
	if $(LANGDEF_COMPILER_V1_BIN) oslf-ntt \
		--composition $(METAMATH_LANGDEF_MANIFEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--reflection $(FINITE_HORN_REFLECTION_V1) \
		--compiler $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--out "$$mutation_dir/malformed.answers" \
		>/dev/null 2>&1; then exit 1; fi; \
	printf '%s\n' '(MetamathAuthoringNTTV1Summary 9 9 0)'

.PHONY: test-metamath-cogslt-proof-semantic-ntt-v1
test-metamath-cogslt-proof-semantic-ntt-v1: \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		$(PROOF_GSLT_FIRST_ORDER_SEMANTIC_INTERFACE_V1) \
		$(METAMATH_PROOF_SEMANTIC_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_SEMANTIC_DENOTED_KEYS_V1) \
		$(PROOF_GSLT_BINDER_ANSWERS_V1) \
		$(GSLT_RULE_MUTATOR_V1_BIN)
	@set -eu; \
	work=$$(mktemp -d runtime/metamath-proof-semantic-ntt.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	cmp $(METAMATH_PROOF_SEMANTIC_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_SEMANTIC_DENOTED_KEYS_V1); \
	test "$$(rg -c '^    \(rule answer-fact-' \
		$(METAMATH_PROOF_SEMANTIC_PAYLOAD_V1))" -eq 48; \
	test "$$(rg -c '^    \(rule answer-fact-' \
		$(METAMATH_PROOF_SEMANTIC_GSLT_V1))" -eq 47; \
	test "$$(wc -l <$(METAMATH_PROOF_SEMANTIC_NTT_V1))" -eq 70; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-head-signature-v1 ' \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1))" -eq 26; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-step-schema-v1 ' \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1))" -eq 27; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-external-relation-v1 ' \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1))" -eq 5; \
	if $(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_PROOF_SEMANTIC_GSLT_V1) \
		--source $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--query '(compile-oslf-native-type-v1 ?fact)' \
		--out "$$work/missing-reflection.answers" \
		>"$$work/missing-reflection.out" \
		2>"$$work/missing-reflection.err"; then exit 1; fi; \
	rg -q 'compiler receipt:.*MalformedPresentation.*undeclared operator q-int/1' \
		"$$work/missing-reflection.err"; \
	test ! -e "$$work/missing-reflection.answers"; \
	rg -F -x -q \
		'(compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-append-nil-v1 (q-app (q-sym ProofSequenceAppendV1) (q-cons (q-app (q-sym ProofSequenceNilV1) q-nil) (q-cons (q-var q-zero) (q-cons (q-var q-zero) q-nil)))) q-nil))' \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1); \
	if $(LANGDEF_COMPILER_V1_BIN) project-answers \
		--source $(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		--head proof-semantic-required-v1 --arg 0 \
		--out "$$work/wrong-head.answers" >/dev/null 2>&1; then \
		exit 1; \
	fi; \
	mkdir -p "$$work/binder"; \
	$(LANGDEF_COMPILER_V1_BIN) answer-facts \
		--source $(PROOF_GSLT_BINDER_ANSWERS_V1) \
		--out "$$work/binder/payload.metta" >/dev/null; \
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source "$$work/binder/payload.metta" \
		--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		--query '(proof-semantic-required-v1 ?key)' \
		--out "$$work/binder/required.answers" >/dev/null; \
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source "$$work/binder/payload.metta" \
		--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
		--query '(proof-semantic-denoted-v1 ?key)' \
		--out "$$work/binder/denoted.answers" >/dev/null; \
	$(LANGDEF_COMPILER_V1_BIN) project-answers \
		--source "$$work/binder/required.answers" \
		--head proof-semantic-required-v1 --arg 0 \
		--out "$$work/binder/required-keys.answers" >/dev/null; \
	$(LANGDEF_COMPILER_V1_BIN) project-answers \
		--source "$$work/binder/denoted.answers" \
		--head proof-semantic-denoted-v1 --arg 0 \
		--out "$$work/binder/denoted-keys.answers" >/dev/null; \
	if cmp -s "$$work/binder/required-keys.answers" \
		"$$work/binder/denoted-keys.answers"; then exit 1; fi; \
	rg -F -x -q \
		'(proof-semantic-key-v1 proof-semantic-rule-v1 BinderProofV1 drop-unused)' \
		"$$work/binder/required-keys.answers"; \
	if rg -F -x -q \
		'(proof-semantic-key-v1 proof-semantic-rule-v1 BinderProofV1 drop-unused)' \
		"$$work/binder/denoted-keys.answers"; then exit 1; fi; \
	for lane in delete falsify; do mkdir -p "$$work/$$lane"; done; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--out "$$work/delete/proof-calculus.metta" \
		--rule mm-proof-calculus-provable-judgment --mode delete; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		--out "$$work/falsify/proof-calculus.metta" \
		--rule mm-proof-calculus-provable-judgment --mode falsify \
		--replacement \
		'(source-proof-judgment MetamathProofV1 MetamathProvableV1 proof-zero-v1)'; \
	for lane in delete falsify; do \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
			--source $(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
			--source $(PROOF_GSLT_ARTICLE_COMPILER_V1) \
			--source $(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
			--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
			--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
			--source "$$work/$$lane/proof-calculus.metta" \
			--query '(proof-artifact-v1 ?record)' \
			--out "$$work/$$lane/plan.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answer-facts \
			--source "$$work/$$lane/plan.answers" \
			--out "$$work/$$lane/payload.metta" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane/payload.metta" \
			--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
			--query '(source-operator ?owner ?name ?arity)' \
			--out "$$work/$$lane/operator.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane/payload.metta" \
			--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
			--query '(source-rule ?owner ?rule)' \
			--out "$$work/$$lane/rule.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane/payload.metta" \
			--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
			--query '(proof-semantic-required-v1 ?key)' \
			--out "$$work/$$lane/required.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane/payload.metta" \
			--source $(PROOF_GSLT_FIRST_ORDER_DENOTATION_V1) \
			--query '(proof-semantic-denoted-v1 ?key)' \
			--out "$$work/$$lane/denoted.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) project-answers \
			--source "$$work/$$lane/required.answers" \
			--head proof-semantic-required-v1 --arg 0 \
			--out "$$work/$$lane/required-keys.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) project-answers \
			--source "$$work/$$lane/denoted.answers" \
			--head proof-semantic-denoted-v1 --arg 0 \
			--out "$$work/$$lane/denoted-keys.answers" >/dev/null; \
		cmp "$$work/$$lane/required-keys.answers" \
			"$$work/$$lane/denoted-keys.answers"; \
		$(LANGDEF_COMPILER_V1_BIN) merge-answers \
			--source "$$work/$$lane/operator.answers" \
			--source "$$work/$$lane/rule.answers" \
			--out "$$work/$$lane/reflection.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answer-facts \
			--source "$$work/$$lane/reflection.answers" \
			--out "$$work/$$lane/semantic.metta" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane/semantic.metta" \
			--source $(FINITE_HORN_REFLECTION_V1) \
			--reflect-source $(PROOF_GSLT_FIRST_ORDER_SEMANTIC_INTERFACE_V1) \
			--source $(OSLF_NATIVE_TYPE_COMPILER_V1) \
			--query '(compile-oslf-native-type-v1 ?fact)' \
			--out "$$work/$$lane/ntt.answers" >/dev/null; \
		if cmp -s $(METAMATH_PROOF_SEMANTIC_NTT_V1) \
			"$$work/$$lane/ntt.answers"; then exit 1; fi; \
	done; \
	if rg -q 'oslf-head-signature-v1 MetamathProvableV1' \
		"$$work/delete/ntt.answers"; then exit 1; fi; \
	rg -F -x -q \
		'(compile-oslf-native-type-v1 (oslf-head-signature-v1 MetamathProvableV1 q-zero))' \
		"$$work/falsify/ntt.answers"; \
	printf '%s\n' '(MetamathProofSemanticNTTV1Summary 17 17 0)'

.PHONY: test-metamath-cogslt-proof-semantic-exec-v1
test-metamath-cogslt-proof-semantic-exec-v1: \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@set -eu; \
	work=$$(mktemp -d runtime/metamath-proof-semantic-exec.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	$(LANGDEF_COMPILER_V1_BIN) semantic-gslt \
		--source $(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) \
		--out "$$work/regenerated.metta" >/dev/null; \
	cmp "$$work/regenerated.metta" $(METAMATH_PROOF_SEMANTIC_EXEC_V1); \
	test "$$(rg -c '^    \(operator ' \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1))" -eq 22; \
	test "$$(rg -c '^    \(rule ' \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1))" -eq 22; \
	if rg -q '^    \(operator [^ ]+ 0\)' \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1); then exit 1; fi; \
	positive=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		--query-text \
		'(ProofSequenceAppendV1 ProofSequenceNilV1 ProofSequenceNilV1 ProofSequenceNilV1)' \
		--summary --cert-out "$$work/append.cert"); \
	printf '%s\n' "$$positive" | rg -F -q '"outcome":"Unique"'; \
	replay=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		--query-text \
		'(ProofSequenceAppendV1 ProofSequenceNilV1 ProofSequenceNilV1 ProofSequenceNilV1)' \
		--replay "$$work/append.cert"); \
	printf '%s\n' "$$replay" | rg -F -q '"replay":"ACCEPT"'; \
	negative=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		--query-text \
		'(ProofSequenceAppendV1 ProofSequenceNilV1 ProofSequenceNilV1 (ProofSequenceConsV1 ProofSequenceNilV1 ProofSequenceNilV1))' \
		--summary); \
	printf '%s\n' "$$negative" | rg -F -q '"outcome":"NoAnswer"'; \
	rg -v 'proof-sequence-append-nil-v1' \
		$(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) \
		>"$$work/deleted.answers"; \
	$(LANGDEF_COMPILER_V1_BIN) semantic-gslt \
		--source "$$work/deleted.answers" \
		--out "$$work/deleted.metta" >/dev/null; \
	if cmp -s "$$work/deleted.metta" \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1); then exit 1; fi; \
	deleted=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		"$$work/deleted.metta" \
		--query-text \
		'(ProofSequenceAppendV1 ProofSequenceNilV1 ProofSequenceNilV1 ProofSequenceNilV1)' \
		--summary); \
	printf '%s\n' "$$deleted" | rg -F -q '"outcome":"NoAnswer"'; \
	sed '1s/MetamathProofV1/OtherProofV1/' \
		$(METAMATH_PROOF_SEMANTIC_REFLECTION_V1) \
		>"$$work/multiple-owners.answers"; \
	if $(LANGDEF_COMPILER_V1_BIN) semantic-gslt \
		--source "$$work/multiple-owners.answers" \
		--out "$$work/multiple-owners.metta" >/dev/null 2>&1; then \
		exit 1; \
	fi; \
	test ! -e "$$work/multiple-owners.metta"; \
	{ cat $(METAMATH_PROOF_SEMANTIC_REFLECTION_V1); \
		head -n 1 $(METAMATH_PROOF_SEMANTIC_REFLECTION_V1); } \
		>"$$work/duplicate.answers"; \
	if $(LANGDEF_COMPILER_V1_BIN) semantic-gslt \
		--source "$$work/duplicate.answers" \
		--out "$$work/duplicate.metta" >/dev/null 2>&1; then \
		exit 1; \
	fi; \
	test ! -e "$$work/duplicate.metta"; \
	printf '%s\n' '(MetamathProofSemanticExecV1Summary 10 10 0)'

.PHONY: test-metamath-cogslt-proof-relational-projection-v1
test-metamath-cogslt-proof-relational-projection-v1: \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_SOURCE_FOLD_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(METAMATH_SOURCE_STATE_V1) $(METAMATH_SOURCE_PROOF_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
		$(PROOF_GSLT_METAMATH_CALCULUS_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CONTEXT_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_FRAME_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_FILTERED_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_ASSERTION_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_PUSH_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NO_APARTNESS_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(GSLT_RULE_MUTATOR_V1_BIN)
	@set -eu; \
	work=$$(mktemp -d runtime/metamath-proof-relational-projection.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	run_projection_artifact() { \
		guest="$$1"; out="$$2"; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source $(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
			--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
			--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
			--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
			--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
			--source $(METAMATH_SOURCE_FOLD_V1) \
			--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
			--source $(METAMATH_SOURCE_STATE_V1) \
			--source $(METAMATH_SOURCE_PROOF_V1) \
			--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
			--source $(PROOF_GSLT_TRACE_COMPILER_V1) \
			--source $(PROOF_GSLT_TRACE_INPUT_V1) \
			--source $(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
			--source $(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
			--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
			--source "$$guest" \
			--query '(proof-relational-projection-artifact-v1 ?record)' \
			--out "$$out" >/dev/null; \
	}; \
	run_projection_query() { \
		projection="$$1"; guest="$$2"; query="$$3"; \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
			$(PROOF_GSLT_ARTICLE_CORE_V1) \
			$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
			$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
			$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
			$(METAMATH_SOURCE_FOLD_V1) \
			$(METAMATH_RELATIONAL_STATE_CORE_V1) \
			$(METAMATH_SOURCE_STATE_V1) \
			$(METAMATH_SOURCE_PROOF_V1) \
			$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
			$(PROOF_GSLT_TRACE_COMPILER_V1) \
			$(PROOF_GSLT_TRACE_INPUT_V1) \
			"$$projection" $(PROOF_GSLT_METAMATH_CALCULUS_V1) "$$guest" \
			--query-file "$$query" --summary; \
	}; \
	run_projection_artifact $(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		"$$work/metamath.answers"; \
	cmp "$$work/metamath.answers" \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1); \
	test "$$(wc -l <$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1))" -eq 16; \
	test "$$(rg -c 'proof-relational-projection-identity-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1))" -eq 1; \
	test "$$(rg -c 'proof-relational-projection-table-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1))" -eq 9; \
	test "$$(rg -c 'proof-relational-projection-selector-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1))" -eq 6; \
	run_projection_artifact $(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		"$$work/canary.answers"; \
	test "$$(wc -l <"$$work/canary.answers")" -eq 13; \
	test "$$(rg -c 'proof-relational-projection-table-v1' \
		"$$work/canary.answers")" -eq 7; \
	test "$$(rg -c 'proof-relational-projection-selector-v1' \
		"$$work/canary.answers")" -eq 5; \
	if rg -q 'apartness-v1' "$$work/canary.answers"; then exit 1; fi; \
	tail -n +2 "$$work/canary.answers" \
		>"$$work/missing-identity.answers"; \
	sed '0,/RelationalProjectionCanaryProofV1/s//MixedProjectionOwnerV1/' \
		"$$work/canary.answers" >"$$work/mixed-owner.answers"; \
	$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_BIN) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1) \
		"$$work/canary.answers" \
		"$$work/missing-identity.answers" \
		"$$work/mixed-owner.answers"; \
	context=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CONTEXT_V1)); \
	printf '%s\n' "$$context" | rg -F -q '"outcome":"Unique"'; \
	frame=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_FRAME_V1)); \
	printf '%s\n' "$$frame" | rg -F -q '"outcome":"Unique"'; \
	filtered=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_FILTERED_V1)); \
	printf '%s\n' "$$filtered" | rg -F -q '"outcome":"NoAnswer"'; \
	assertion=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_ASSERTION_V1)); \
	printf '%s\n' "$$assertion" | rg -F -q '"outcome":"Unique"'; \
	push=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_PUSH_V1)); \
	printf '%s\n' "$$push" | rg -F -q '"outcome":"Unique"'; \
	no_apartness=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NO_APARTNESS_V1)); \
	printf '%s\n' "$$no_apartness" | rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		--out "$$work/deleted-frame.metta" \
		--rule proof-relational-projection-frame-binding-v1 --mode delete; \
	deleted=$$(run_projection_query "$$work/deleted-frame.metta" \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_FRAME_V1)); \
	printf '%s\n' "$$deleted" | rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_RELATIONAL_PROJECTION_CANARY_V1) \
		--out "$$work/falsified-frame.metta" \
		--rule relational-projection-canary-frame-identity-x-v1 \
		--mode falsify --replacement \
		'(source-proof-relational-row3-v1 projection-canary-t6-v1 projection-canary-identity-v1 projection-canary-position-0-v1 projection-canary-hy-v1)'; \
	falsified=$$(run_projection_query \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		"$$work/falsified-frame.metta" \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_FRAME_V1)); \
	printf '%s\n' "$$falsified" | rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		--out "$$work/deleted-metamath-role.metta" \
		--rule mm-proof-relational-projection-formula-v1 --mode delete; \
	run_projection_artifact "$$work/deleted-metamath-role.metta" \
		"$$work/deleted-metamath-role.answers"; \
	if cmp -s "$$work/deleted-metamath-role.answers" \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1); then exit 1; fi; \
	test "$$(wc -l <"$$work/deleted-metamath-role.answers")" -eq 15; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
		--out "$$work/deleted-metamath-frame-role.metta" \
		--rule mm-proof-relational-projection-ordered-frame-v1 --mode delete; \
	run_projection_artifact "$$work/deleted-metamath-frame-role.metta" \
		"$$work/deleted-metamath-frame-role.answers"; \
	if cmp -s "$$work/deleted-metamath-frame-role.answers" \
		$(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_ABI_V1); then exit 1; fi; \
	test "$$(wc -l <"$$work/deleted-metamath-frame-role.answers")" -eq 15; \
	if rg -q 'proof-relational-ordered-frame-v1' \
		"$$work/deleted-metamath-frame-role.answers"; then exit 1; fi; \
	if rg -ni 'metamath|megalodon|tptp|set[.]mm|mm-state|mm-symbol|mm-label|[$$][acdefpv]' \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_COMPILER_V1) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_HEADER); then exit 1; fi; \
	printf '%s\n' '(MetamathProofRelationalProjectionV1Summary 23 23 0)'

.PHONY: test-metamath-cogslt-proof-trace-semantics-v1
test-metamath-cogslt-proof-trace-semantics-v1: \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		$(PROOF_GSLT_TRACE_ORDER_POSITIVE_V1) \
		$(PROOF_GSLT_TRACE_ORDER_SWAPPED_V1) \
		$(PROOF_GSLT_TRACE_ORDER_MISSING_CONTEXT_V1) \
		$(PROOF_GSLT_TRACE_SAVED_POSITIVE_V1) \
		$(PROOF_GSLT_TRACE_SAVED_RANGE_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		$(PROOF_GSLT_TRACE_COMPILE_NORMAL_V1) \
		$(PROOF_GSLT_TRACE_COMPILE_COMPRESSED_V1) \
		$(PROOF_GSLT_TRACE_COMPILE_CONTINUATION_V1) \
		$(PROOF_GSLT_TRACE_COMPILE_OPEN_SAVE_V1) \
		$(PROOF_GSLT_TRACE_COMPILE_UNKNOWN_V1) \
		$(PROOF_GSLT_TRACE_COMPILE_ACCEPTED_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CONTEXT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_FRAME_V1) \
		$(PROOF_GSLT_TRACE_INPUT_NORMAL_V1) \
		$(PROOF_GSLT_TRACE_INPUT_COMPRESSED_V1) \
		$(PROOF_GSLT_TRACE_INPUT_INCOMPLETE_NORMAL_V1) \
		$(PROOF_GSLT_TRACE_INPUT_INCOMPLETE_COMPRESSED_V1) \
		$(PROOF_GSLT_TRACE_INPUT_UNKNOWN_NOT_VERIFIED_V1) \
		$(PROOF_GSLT_TRACE_INPUT_WRONG_TARGET_V1) \
		$(PROOF_GSLT_TRACE_INPUT_BAD_COUNT_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_POSITIVE_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_RANGE_V1) \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) $(GSLT_RULE_MUTATOR_V1_BIN)
	@set -eu; \
	work=$$(mktemp -d runtime/metamath-proof-trace.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	positive=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_ORDER_POSITIVE_V1) \
		--summary --cert-out "$$work/trace.cert"); \
	printf '%s\n' "$$positive" | rg -F -q '"outcome":"Unique"'; \
	replay=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_ORDER_POSITIVE_V1) \
		--replay "$$work/trace.cert"); \
	printf '%s\n' "$$replay" | rg -F -q '"replay":"ACCEPT"'; \
	swapped=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_ORDER_SWAPPED_V1) --summary); \
	printf '%s\n' "$$swapped" | rg -F -q '"outcome":"NoAnswer"'; \
	missing=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_ORDER_MISSING_CONTEXT_V1) \
		--summary); \
	printf '%s\n' "$$missing" | rg -F -q '"outcome":"NoAnswer"'; \
	saved=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_SAVED_POSITIVE_V1) --summary); \
	printf '%s\n' "$$saved" | rg -F -q '"outcome":"Unique"'; \
	range=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_SAVED_RANGE_V1) --summary); \
	printf '%s\n' "$$range" | rg -F -q '"outcome":"NoAnswer"'; \
	normal=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_NORMAL_V1) --summary); \
	printf '%s\n' "$$normal" | rg -F -q '"outcome":"Unique"'; \
	compressed=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_COMPRESSED_V1) --summary); \
	printf '%s\n' "$$compressed" | rg -F -q '"outcome":"Unique"'; \
	continuation=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_CONTINUATION_V1) \
		--summary); \
	printf '%s\n' "$$continuation" | rg -F -q '"outcome":"Unique"'; \
	open_save=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_OPEN_SAVE_V1) --summary); \
	printf '%s\n' "$$open_save" | rg -F -q '"outcome":"NoAnswer"'; \
	unknown=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_UNKNOWN_V1) --summary); \
	printf '%s\n' "$$unknown" | rg -F -q '"outcome":"NoAnswer"'; \
	accepted=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_ACCEPTED_V1) --summary); \
	printf '%s\n' "$$accepted" | rg -F -q '"outcome":"Unique"'; \
	input_context=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_CONTEXT_V1) --summary); \
	printf '%s\n' "$$input_context" | rg -F -q '"outcome":"Unique"'; \
	input_frame=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_FRAME_V1) --summary); \
	printf '%s\n' "$$input_frame" | rg -F -q '"outcome":"Unique"'; \
	input_normal=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_NORMAL_V1) --summary); \
	printf '%s\n' "$$input_normal" | rg -F -q '"outcome":"Unique"'; \
	input_compressed=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_COMPRESSED_V1) --summary); \
	printf '%s\n' "$$input_compressed" | rg -F -q '"outcome":"Unique"'; \
	input_incomplete_normal=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_INCOMPLETE_NORMAL_V1) \
		--summary); \
	printf '%s\n' "$$input_incomplete_normal" | \
		rg -F -q '"outcome":"Unique"'; \
	input_incomplete_compressed=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_INCOMPLETE_COMPRESSED_V1) \
		--summary); \
	printf '%s\n' "$$input_incomplete_compressed" | \
		rg -F -q '"outcome":"Unique"'; \
	input_unknown_not_verified=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_UNKNOWN_NOT_VERIFIED_V1) \
		--summary); \
	printf '%s\n' "$$input_unknown_not_verified" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	composition_positive=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1) \
		--query-file \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_POSITIVE_V1) \
		--summary); \
	printf '%s\n' "$$composition_positive" | \
		rg -F -q '"outcome":"Unique"'; \
	composition_range=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1) \
		--query-file \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_RANGE_V1) \
		--summary); \
	printf '%s\n' "$$composition_range" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	input_wrong_target=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_WRONG_TARGET_V1) \
		--summary); \
	printf '%s\n' "$$input_wrong_target" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	input_bad_count=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_BAD_COUNT_V1) --summary); \
	printf '%s\n' "$$input_bad_count" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		--out "$$work/deleted.metta" \
		--rule proof-trace-frame-apply-variable-v1 --mode delete; \
	deleted=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) "$$work/deleted.metta" \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_ORDER_POSITIVE_V1) --summary); \
	printf '%s\n' "$$deleted" | rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		--out "$$work/deleted-save.metta" \
		--rule proof-trace-exec-state-save-v1 --mode delete; \
	deleted_save=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		"$$work/deleted-save.metta" \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_SAVED_POSITIVE_V1) --summary); \
	printf '%s\n' "$$deleted_save" | rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		--out "$$work/deleted-incomplete-step.metta" \
		--rule proof-trace-exec-state-incomplete-v1 --mode delete; \
	deleted_incomplete=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		"$$work/deleted-incomplete-step.metta" \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_INCOMPLETE_NORMAL_V1) \
		--summary); \
	printf '%s\n' "$$deleted_incomplete" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		--out "$$work/deleted-saved-lookup-zero.metta" \
		--rule proof-trace-saved-lookup-zero-v1 --mode delete; \
	deleted_saved_lookup=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		"$$work/deleted-saved-lookup-zero.metta" \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1) \
		--query-file \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_POSITIVE_V1) \
		--summary); \
	printf '%s\n' "$$deleted_saved_lookup" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_COMPILER_V1) \
		--out "$$work/deleted-continuation.metta" \
		--rule proof-trace-decode-status-continuation-v1 --mode delete; \
	deleted_continuation=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		"$$work/deleted-continuation.metta" \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_CONTINUATION_V1) \
		--summary); \
	printf '%s\n' "$$deleted_continuation" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(METAMATH_PROOF_TRACE_CODEC_V1) \
		--out "$$work/deleted-terminal.metta" \
		--rule mm-proof-trace-terminal-a-v1 --mode delete; \
	deleted_terminal=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		"$$work/deleted-terminal.metta" \
		$(PROOF_GSLT_TRACE_CODEC_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_COMPILE_ACCEPTED_V1) --summary); \
	printf '%s\n' "$$deleted_terminal" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_INPUT_V1) \
		--out "$$work/deleted-input-assertion.metta" \
		--rule proof-trace-input-assertion-v1 --mode delete; \
	deleted_input=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		"$$work/deleted-input-assertion.metta" \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query-file $(PROOF_GSLT_TRACE_INPUT_NORMAL_V1) --summary); \
	printf '%s\n' "$$deleted_input" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source $(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--out "$$work/falsified-input-count.metta" \
		--rule proof-trace-input-canary-context-count-v1 \
		--mode falsify --replacement \
		'(source-proof-trace-input-context-count-v1 input-request-v1 (ProofTraceNatSuccV1 (ProofTraceNatSuccV1 (ProofTraceNatSuccV1 ProofTraceNatZeroV1))))'; \
	falsified_input=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1) \
		$(METAMATH_PROOF_TRACE_CODEC_V1) \
		"$$work/falsified-input-count.metta" \
		--query-file $(PROOF_GSLT_TRACE_INPUT_NORMAL_V1) --summary); \
	printf '%s\n' "$$falsified_input" | \
		rg -F -q '"outcome":"NoAnswer"'; \
	if rg -ni 'metamath|megalodon|tptp|set\.mm|\$[acdefpv]' \
		$(PROOF_GSLT_TRACE_SEMANTICS_V1) \
		$(PROOF_GSLT_TRACE_COMPILER_V1) \
		$(PROOF_GSLT_TRACE_INPUT_V1); then exit 1; fi; \
	if rg -q 'ProofTokenApartV1|ProofAssertionDisjointV1' \
		$(PROOF_GSLT_TRACE_ORDER_CANARY_V1) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1); then exit 1; fi; \
	test "$$(wc -l <$(METAMATH_PROOF_MACHINE_NTT_V1))" -eq 911; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-head-signature-v1 ' \
		$(METAMATH_PROOF_MACHINE_NTT_V1))" -eq 246; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-step-schema-v1 ' \
		$(METAMATH_PROOF_MACHINE_NTT_V1))" -eq 629; \
	test "$$(rg -c '^\(compile-oslf-native-type-v1 \(oslf-external-relation-v1 ' \
		$(METAMATH_PROOF_MACHINE_NTT_V1))" -eq 24; \
	rg -F -q \
		'(oslf-head-signature-v1 ProofTraceAcceptedV1 (q-succ (q-succ (q-succ q-zero))))' \
		$(METAMATH_PROOF_MACHINE_NTT_V1); \
	rg -F -q \
		'(oslf-head-signature-v1 ProofTraceInputVerifyCompressedV1 (q-succ (q-succ (q-succ (q-succ q-zero)))))' \
		$(METAMATH_PROOF_MACHINE_NTT_V1); \
	rg -F -q \
		'(oslf-head-signature-v1 proof-relational-projection-artifact-v1 (q-succ q-zero))' \
		$(METAMATH_PROOF_MACHINE_NTT_V1); \
	rg -F -q 'proof-relational-projection-frame-binding-v1' \
		$(METAMATH_PROOF_MACHINE_NTT_V1); \
	printf '%s\n' '(MetamathProofTraceSemanticsV1Summary 41 41 0)'

.PHONY: test-metamath-cogslt-proof-storage-plan-v1
test-metamath-cogslt-proof-storage-plan-v1: \
		$(METAMATH_PROOF_STORAGE_PLAN_V1) \
		$(METAMATH_PROOF_STORAGE_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_STORAGE_DENOTED_KEYS_V1)
	@set -eu; \
	work=$$(mktemp -d runtime/metamath-proof-storage-plan.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	cmp $(METAMATH_PROOF_STORAGE_REQUIRED_KEYS_V1) \
		$(METAMATH_PROOF_STORAGE_DENOTED_KEYS_V1); \
	test "$$(rg -c '^    \(rule answer-fact-' \
		$(METAMATH_PROOF_STORAGE_ANALYSIS_PAYLOAD_V1))" -eq 332; \
	test "$$(wc -l <$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 36; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(state-table-storage-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 19; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(proof-machine-table-read-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 9; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(proof-call-region-plan-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 2; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(proof-finite-support-plan-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 1; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(proof-indexed-value-plan-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 1; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(proof-frame-index-plan-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 1; \
	test "$$(rg -c '^\(compile-proof-storage-plan-v1 \(proof-literal-hole-plan-v1 ' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1))" -eq 1; \
	rg -F -x -q \
		'(compile-proof-storage-plan-v1 (proof-sequence-layout-v1 MetamathProofV1 MetamathLanguageV1 MetamathProvableV1 ProofSequenceConsV1 ProofSequenceNilV1 flat-symbol-id-vector-v1 proof-call-region-v1))' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1); \
	rg -F -x -q \
		'(compile-proof-storage-plan-v1 (proof-call-region-plan-v1 mm-check-theorem-normal 14 mm-stack-proof-machine-v1 MetamathProofV1 MetamathProvableV1 proof-call-region-v1 flat-symbol-id-vector-v1 proof-verdict-only-v1))' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1); \
	rg -F -x -q \
		'(compile-proof-storage-plan-v1 (proof-finite-support-plan-v1 MetamathProofV1 ProofSequenceConsV1 ProofSequenceNilV1 ProofSequenceSupportApartV1 ProofTokenAgainstSequenceV1 ProofTokenPairAllowedV1 ProofTokenApartV1 ProofTokenLiteralV1 ProofTokenVariableV1 finite-dense-bitset-v1 finite-apartness-matrix-v1))' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1); \
	rg -F -x -q \
		'(compile-proof-storage-plan-v1 (proof-indexed-value-plan-v1 mm-check-theorem-compressed 14 mm-stack-proof-machine-v1 mm-proof-step mm-compressed-word prepared-indexed-value-table-v1 proof-call-region-v1))' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1); \
	rg -F -x -q \
		'(compile-proof-storage-plan-v1 (proof-frame-index-plan-v1 mm-check-theorem-compressed 14 mm-stack-proof-machine-v1 u32-open-addressed-index-v1 duplicate-reject-v1 proof-call-region-v1))' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1); \
	rg -F -x -q \
		'(compile-proof-storage-plan-v1 (proof-literal-hole-plan-v1 mm-stack-proof-machine-v1 MetamathProofV1 ProofSequenceConsV1 ProofSequenceNilV1 literal-hole-run-program-v1 state-run-region-v1 proof-call-region-v1))' \
		$(METAMATH_PROOF_STORAGE_PLAN_V1); \
	sed \
		'/^(compile-oslf-native-type-v1 (oslf-head-signature-v1 MetamathProvableV1 (q-succ q-zero)))$$/d' \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		>"$$work/delete.answers"; \
	sed \
		's/(oslf-head-signature-v1 ProofSequenceConsV1 (q-succ (q-succ q-zero)))/(oslf-head-signature-v1 ProofSequenceConsV1 (q-succ q-zero))/' \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		>"$$work/falsify.answers"; \
	sed \
		'/oslf-step-schema-v1 MetamathProofV1 proof-sequence-support-apart-cons-v1/d' \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		>"$$work/support-delete.answers"; \
	sed \
		's/state-proof-decoder-v1/state-proof-decoder-disabled-v1/' \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		>"$$work/indexed-disable.answers"; \
	sed \
		's/(state-table-v1 mm-state-assertion-mandatory-variable 2 2 state-persistent-v1)/(state-table-v1 mm-state-assertion-mandatory-variable 2 1 state-persistent-v1)/' \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		>"$$work/frame-key-mutation.answers"; \
	sed \
		's/(state-table-v1 mm-state-formula 2 1 state-persistent-v1)/(state-table-v1 mm-state-formula 2 1 state-scoped-v1)/' \
		$(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		>"$$work/literal-scoped.answers"; \
	if cmp -s $(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		"$$work/delete.answers"; then exit 1; fi; \
	if cmp -s $(METAMATH_PROOF_STORAGE_COMBINED_V1) \
		"$$work/falsify.answers"; then exit 1; fi; \
	for lane in delete falsify support-delete indexed-disable frame-key-mutation literal-scoped; do \
		$(LANGDEF_COMPILER_V1_BIN) answer-facts \
			--source "$$work/$$lane.answers" \
			--out "$$work/$$lane.metta" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane.metta" \
			--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
			--query '(proof-storage-required-v1 ?key)' \
			--out "$$work/$$lane-required.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane.metta" \
			--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
			--query '(proof-storage-denoted-v1 ?key)' \
			--out "$$work/$$lane-denoted.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) project-answers \
			--source "$$work/$$lane-required.answers" \
			--head proof-storage-required-v1 --arg 0 \
			--out "$$work/$$lane-required-keys.answers" >/dev/null; \
		$(LANGDEF_COMPILER_V1_BIN) project-answers \
			--source "$$work/$$lane-denoted.answers" \
			--head proof-storage-denoted-v1 --arg 0 \
			--out "$$work/$$lane-denoted-keys.answers" >/dev/null; \
		if [[ "$$lane" == support-delete || \
			"$$lane" == indexed-disable || \
			"$$lane" == frame-key-mutation || \
			"$$lane" == literal-scoped ]]; then \
			cmp "$$work/$$lane-required-keys.answers" \
				"$$work/$$lane-denoted-keys.answers"; \
		elif cmp -s "$$work/$$lane-required-keys.answers" \
			"$$work/$$lane-denoted-keys.answers"; then exit 1; fi; \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source "$$work/$$lane.metta" \
			--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
			--query '(compile-proof-storage-plan-v1 ?fact)' \
			--out "$$work/$$lane-plan.answers" >/dev/null; \
		if cmp -s $(METAMATH_PROOF_STORAGE_PLAN_V1) \
			"$$work/$$lane-plan.answers"; then exit 1; fi; \
	done; \
	rg -F -x -q \
		'(proof-storage-key-v1 proof-sequence-v1 (proof-sequence-id-v1 MetamathProofV1 MetamathProvableV1))' \
		"$$work/delete-required-keys.answers"; \
	if rg -q 'proof-sequence-id-v1 MetamathProofV1 MetamathProvableV1' \
		"$$work/delete-denoted-keys.answers"; then exit 1; fi; \
	if rg -q 'proof-call-region-plan-v1' \
		"$$work/falsify-plan.answers"; then exit 1; fi; \
	if rg -q 'proof-finite-support-plan-v1' \
		"$$work/support-delete-plan.answers"; then exit 1; fi; \
	rg -q 'proof-sequence-layout-v1' \
		"$$work/support-delete-plan.answers"; \
	if rg -q 'proof-indexed-value-plan-v1' \
		"$$work/indexed-disable-plan.answers"; then exit 1; fi; \
	rg -q 'proof-call-region-plan-v1.*mm-check-theorem-compressed' \
		"$$work/indexed-disable-plan.answers"; \
	if rg -q 'proof-frame-index-plan-v1' \
		"$$work/frame-key-mutation-plan.answers"; then exit 1; fi; \
	rg -q 'proof-indexed-value-plan-v1.*mm-check-theorem-compressed' \
		"$$work/frame-key-mutation-plan.answers"; \
	if rg -q 'proof-literal-hole-plan-v1' \
		"$$work/literal-scoped-plan.answers"; then exit 1; fi; \
	rg -q 'proof-call-region-plan-v1.*mm-check-theorem-normal' \
		"$$work/literal-scoped-plan.answers"; \
	printf '%s\n' '(MetamathProofStoragePlanV1Summary 24 24 0)'

.PHONY: test-metamath-cogslt-load-bearing-mutations-v1
test-metamath-cogslt-load-bearing-mutations-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
		$(BIN) >/dev/null
	@if [[ ! -x "$(METAMATH_CURSOR_FOLD_TEST_BIN_V1)" ]]; then \
		$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			"$(METAMATH_CURSOR_FOLD_TEST_BIN_V1)"; \
	fi
	@mutation_dir=$$(mktemp -d langdef/metamath-cogslt-mutation.XXXXXX); \
	trap 'rm -rf "$$mutation_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$mutation_dir/"; \
	sed '/(rule def-kw-constant-lexeme /s/(cp 99)/(cp 120)/' \
		"$$mutation_dir/syntax_v1.metta" \
		>"$$mutation_dir/syntax_v1.mutated"; \
	mv "$$mutation_dir/syntax_v1.mutated" \
		"$$mutation_dir/syntax_v1.metta"; \
	rg -q -U 'def-kw-constant-lexeme(.|\n)*(cp 120)' \
		"$$mutation_dir/syntax_v1.metta"; \
	$(MAKE) --no-print-directory -B \
		BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		METAMATH_SYNTAX_MANIFEST_V1="$$mutation_dir/syntax_pack_v1.metta" \
		METAMATH_SYNTAX_SOURCE_V1="$$mutation_dir/syntax_v1.metta" \
		METAMATH_SOURCE_PACK_V1="$$mutation_dir/generated/syntax_parser_pack_v1.abi" \
		METAMATH_SYNTAX_LOCK_V1="$$mutation_dir/generated/syntax_lock_v1.metta" \
		METAMATH_NORMALIZED_PACK_V1="$$mutation_dir/generated/normalized_parser_pack_v1.abi" \
		METAMATH_SOURCE_PACK_FACTS_V1="$$mutation_dir/generated/source_parser_pack_facts_v1.metta" \
		METAMATH_NORMALIZED_PACK_FACTS_V1="$$mutation_dir/generated/normalized_parser_pack_facts_v1.metta" \
		METAMATH_LEXICAL_ROOTS_V1="$$mutation_dir/generated/lexical_roots_v1.answers" \
		METAMATH_LEXICAL_NFA_V1="$$mutation_dir/generated/lexical_nfa_v1.answers" \
		METAMATH_EMPTY_GUARD_ANSWERS_V1="$$mutation_dir/generated/empty_guard_v1.answers" \
		METAMATH_EMPTY_GUARD_EVIDENCE_V1="$$mutation_dir/generated/empty_guard_v1.evidence" \
		METAMATH_ACTION_BYTECODE_V1="$$mutation_dir/generated/action_bytecode_v1.answers" \
		METAMATH_OCCURRENCE_FOLD_V1="$$mutation_dir/generated/occurrence_fold_v1.answers" \
		METAMATH_OCCURRENCE_SPAN_MASK_V1="$$mutation_dir/generated/occurrence_span_mask_v1.answers" \
		METAMATH_STATE_PROGRAM_V1="$$mutation_dir/generated/state_program_v1.answers" \
		METAMATH_CURSOR_FOLD_GENERATED_C_V1="$$mutation_dir/generated/syntax_cursor_fold_v1.generated.c" \
		METAMATH_COMPILED_CURSOR_V1="$$mutation_dir/generated/syntax_cursor_fold_v1.generated.so" \
		METAMATH_LANGDEF_MANIFEST_V1="$$mutation_dir/langdef.metta" \
		METAMATH_LANGDEF_LOCK_V1="$$mutation_dir/generated/langdef_lock_v1.metta" \
		METAMATH_FOLD_PART_DIR_V1="$$mutation_dir/fold-parts" \
		METAMATH_OCCURRENCE_SPAN_MASK_STAGE_DIR_V1="$$mutation_dir/occurrence-span-mask-parts" \
		METAMATH_STATE_PART_DIR_V1="$$mutation_dir/state-parts" \
		generate-metamath-cogslt-langdef-v1 >/dev/null; \
	$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
		build-parser-pack-cursor-generated-v1 \
		GENERATED_CURSOR_C="$$mutation_dir/generated/syntax_cursor_fold_v1.generated.c" \
		GENERATED_CURSOR_BIN="$$mutation_dir/mutated-cursor" \
		GENERATED_CURSOR_PREFIX=$(METAMATH_CURSOR_FOLD_PREFIX_V1) \
		GENERATED_CURSOR_HAS_OCCURRENCE_FOLD=1 >/dev/null; \
	if "$$mutation_dir/mutated-cursor" \
		tests/langdef/metamath/positive_constants.mm \
		--recognition-verdict >/dev/null 2>&1; then exit 1; fi; \
	sed 's/^[$$]c/$$x/' tests/langdef/metamath/positive_constants.mm \
		>"$$mutation_dir/mutated-positive.mm"; \
	"$$mutation_dir/mutated-cursor" \
		"$$mutation_dir/mutated-positive.mm" \
		--recognition-verdict >/dev/null; \
	loader_output=$$($(CETTA_BIN_INVOKE) --lang prime \
		-e '!(import! &self langdef)' \
		-e "!(bind! &mutated-langdef (langdef:load \"$$mutation_dir/langdef.metta\"))" \
		-e "!(println! (langdef:parse-file &mutated-langdef \"$$mutation_dir/mutated-positive.mm\"))"); \
	printf '%s\n' "$$loader_output" | \
		rg -Fq '(mm-declare-constants (mm-symbol "wff") (mm-symbol "|-"))'; \
	if $(METAMATH_CURSOR_FOLD_TEST_BIN_V1) \
		"$$mutation_dir/mutated-positive.mm" \
		--recognition-verdict >/dev/null 2>&1; then exit 1; fi; \
	cp $(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		"$$mutation_dir/preserved.generated.c"; \
	if $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN) \
		--abi $(METAMATH_NORMALIZED_PACK_V1) \
		--lexical-answers $(METAMATH_LEXICAL_NFA_V1) \
		--guard-answers $(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		--guard-evidence $(METAMATH_EMPTY_GUARD_EVIDENCE_V1) \
		--guarded-answers $(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		--regular-compiler-digest $(METAMATH_REGULAR_COMPILER_DIGEST_V1) \
		--guarded-compiler-digest $(METAMATH_GUARDED_COMPILER_DIGEST_V1) \
		--action-answers $(METAMATH_EMPTY_GUARD_ANSWERS_V1) \
		--action-compiler-digest $(METAMATH_ACTION_COMPILER_DIGEST_V1) \
		--occurrence-fold-answers $(METAMATH_OCCURRENCE_FOLD_V1) \
		--occurrence-span-mask-answers $(METAMATH_OCCURRENCE_SPAN_MASK_V1) \
		--occurrence-span-mask-compiler-digest $(METAMATH_OCCURRENCE_SPAN_MASK_COMPILER_DIGEST_V1) \
		--out-c "$$mutation_dir/preserved.generated.c" \
		--prefix $(METAMATH_CURSOR_FOLD_PREFIX_V1) \
		>/dev/null 2>&1; then exit 1; fi; \
	cmp $(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		"$$mutation_dir/preserved.generated.c"; \
	printf '%s\n' '(MetamathCoGSLTLoadBearingMutationV1Summary 5 5 0)'

$(METAMATH_CURSOR_FOLD_TEST_BIN_V1): \
		$(PARSER_PACK_CURSOR_GENERATED_V1_TEST_SRC) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		$(PARSER_PACK_CURSOR_GENERATED_V1_LINK_OBJ) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Iexperiments/gslt2parse_foundation/native $(CFLAGS) \
		-Wl,--gc-sections \
		-DPP_CURSOR_GENERATED_PREFIX=$(METAMATH_CURSOR_FOLD_PREFIX_V1) \
		-DPP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD=1 \
		-DPP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK=1 \
		-DPP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS=0 \
		-o $@ $(PARSER_PACK_CURSOR_GENERATED_V1_TEST_SRC) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		$(PARSER_PACK_CURSOR_GENERATED_V1_LINK_OBJ) $(LDFLAGS)

.PHONY: test-metamath-cogslt-syntax-v1
test-metamath-cogslt-syntax-v1: $(METAMATH_CURSOR_FOLD_TEST_BIN_V1)
	@$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		test-metamath-cogslt-load-bearing-mutations-v1
	@output=$$($(METAMATH_CURSOR_FOLD_TEST_BIN_V1) \
		tests/support/metamath_mini_thm_compressed_v0.mm \
		--occurrence-fold); \
	printf '%s\n' "$$output" | \
		rg -q '^occurrence-fold-committed[[:space:]]+1$$'; \
	printf '%s\n' "$$output" | \
		rg -q '^semantic-occurrence-projections[[:space:]]+[1-9][0-9]*$$'; \
	printf '%s\n' "$$output" | \
		rg -q '^occurrence-span-mask-mutation-killed[[:space:]]+1$$'
	@if $(METAMATH_CURSOR_FOLD_TEST_BIN_V1) \
		tests/langdef/metamath/invalid_missing_terminator.mm \
		--recognition-verdict >/dev/null 2>&1; then exit 1; fi
	@if $(METAMATH_CURSOR_FOLD_TEST_BIN_V1) \
		tests/langdef/metamath/invalid_unclosed_comment.mm \
		--recognition-verdict >/dev/null 2>&1; then exit 1; fi
	@if ldd $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN) | \
		rg -qi 'python|libswipl'; then exit 1; fi
	@if ldd $(LANGDEF_COMPILER_V1_BIN) | rg -qi 'python|libswipl'; \
		then exit 1; fi
	@if $(MAKE) -B -n BUILD=core ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		generate-metamath-cogslt-syntax-v1 | rg -q \
		'(^|[[:space:]/])python(3)?([[:space:]]|$$)|[[:alnum:]_/.-]+[.]py([[:space:]]|$$)'; \
		then exit 1; fi
	@if strings $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN) | \
		rg -qi 'metamath|indexed.checker|indexed.inference|[.]mm([^[:alnum:]_]|$$)'; \
		then exit 1; fi
	@if rg -ni 'metamath|megalodon|tptp|indexed.checker|indexed.inference' \
		experiments/gslt2parse_foundation/native/parser_pack_cursor_compile_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_transparent_inline_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_transparent_inline_native_v1.h \
		experiments/gslt2parse_foundation/native/parser_occurrence_fold_v1.c \
		experiments/gslt2parse_foundation/native/parser_occurrence_fold_v1.h \
		experiments/gslt2parse_foundation/native/parser_occurrence_file_resolver_v1.c \
		experiments/gslt2parse_foundation/native/parser_occurrence_file_resolver_v1.h \
		experiments/gslt2parse_foundation/native/parser_occurrence_source_resolver_v1.h \
		experiments/gslt2parse_foundation/native/relational_stack_proof_v1.c \
		experiments/gslt2parse_foundation/native/relational_stack_proof_v1.h \
		$(RELATIONAL_STORE_V1_HEADER) \
		experiments/gslt2parse_foundation/native/relational_state_program_v1.c \
		experiments/gslt2parse_foundation/native/relational_state_program_v1.h \
		experiments/gslt2parse_foundation/native/relational_value_list_v1.c \
		experiments/gslt2parse_foundation/native/relational_value_list_v1.h \
		tools/langdef_compile_v1.c; then exit 1; fi
	@$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		test-metamath-cogslt-regeneration-v1
	@echo '(MetamathCoGSLTSyntaxV1Summary 3 3 0)'

.PHONY: test-metamath-cogslt-diagnostic-binary-v1
test-metamath-cogslt-diagnostic-binary-v1:
	@$(MAKE) --no-print-directory BUILD=$(BUILD) \
		ENABLE_LIB_PROLOG=$(ENABLE_LIB_PROLOG) \
		ENABLE_LANGDEF_DIAGNOSTIC_BACKENDS=1 \
		BIN=$(METAMATH_COGSLT_DIAGNOSTIC_BIN_V1) \
		$(METAMATH_COGSLT_DIAGNOSTIC_BIN_V1) >/dev/null

.PHONY: test-metamath-cogslt-langdef-v1
test-metamath-cogslt-langdef-v1: $(BIN) \
		$(METAMATH_COMPILED_CURSOR_V1) $(METAMATH_LANGDEF_LOCK_V1) \
		test-metamath-cogslt-proof-semantic-exec-v1 \
		test-metamath-cogslt-proof-relational-projection-v1 \
		test-metamath-cogslt-proof-trace-semantics-v1 \
		test-metamath-cogslt-proof-trace-compiled-v1 \
		test-cogslt-oslf-native-type-vm-v1 \
		test-cogslt-relational-state-transaction-v1 \
		test-cogslt-first-order-frame-decoder-v1 \
		test-cogslt-relational-stack-proof-cache-v1 \
		test-metamath-cogslt-diagnostic-binary-v1
	@test_output=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/langdef/metamath/langdef_parse.metta); \
	if printf '%s\n' "$$test_output" | rg -q '\(Error '; then \
		printf '%s\n' "$$test_output" >&2; exit 1; \
	fi
	@echo '(MetamathCoGSLTStateV1Summary 100 100 0)'
	@$(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest $(METAMATH_LANGDEF_MANIFEST_V1)
	@if rg -ni 'metamath|megalodon|tptp|indexed.checker|indexed.inference' \
		native/langdef_module.c native/langdef_module.h \
		native/langdef_compiled_cursor_v1.h \
		native/langdef_compiled_cursor_export_v1.c; then exit 1; fi
	@lock_dir=$$(mktemp -d langdef/metamath-compiled-lock.XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	printf '\001' >> \
		"$$lock_dir/generated/syntax_cursor_fold_v1.generated.so"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$lock_dir/langdef.metta" >/dev/null 2>&1; \
		then exit 1; fi
	@lock_dir=$$(mktemp -d langdef/metamath-proof-lock.XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	printf '\001' >> "$$lock_dir/generated/proof_plan_v1.answers"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$lock_dir/langdef.metta" >/dev/null 2>&1; \
		then exit 1; fi
	@lock_dir=$$(mktemp -d langdef/metamath-proof-projection-lock.XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	printf '\001' >> \
		"$$lock_dir/generated/proof_relational_projection_v1.answers"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$lock_dir/langdef.metta" >/dev/null 2>&1; \
		then exit 1; fi
	@lock_dir=$$(mktemp -d langdef/metamath-proof-source-lock.XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	printf '\n' >> "$$lock_dir/proof_calculus_v1.metta"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$lock_dir/langdef.metta" >/dev/null 2>&1; \
		then exit 1; fi
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/native-type-admission-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed 's/(oslf-head-signature-v1 MetamathProvableV1 (q-succ q-zero))/(oslf-head-signature-v1 MetamathProvableV1 q-zero)/' \
		"$$lock_dir/generated/proof_semantic_ntt_v1.answers" \
		>"$$lock_dir/generated/proof_semantic_ntt_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_semantic_ntt_v1.answers.mutated" \
		"$$lock_dir/generated/proof_semantic_ntt_v1.answers"; \
	rg -Fq '(oslf-head-signature-v1 MetamathProvableV1 q-zero)' \
		"$$lock_dir/generated/proof_semantic_ntt_v1.answers"; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	if authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		2>&1); then \
		printf '%s\n' "$$authority_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$authority_output" | \
		rg -q 'OSLF step application violates its head signature'; \
	if diagnostic_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(METAMATH_COGSLT_DIAGNOSTIC_BIN_INVOKE) --lang "$$selector" \
		--langdef-proof-backend frame-cache-diagnostic-v1 \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		2>&1); then \
		printf '%s\n' "$$diagnostic_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$diagnostic_output" | \
		rg -q 'OSLF step application violates its head signature'
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/storage-plan-admission-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed 's/(state-table-storage-v1 mm-state-formula 2 1 /(state-table-storage-v1 mm-state-formula 3 1 /' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers" \
		>"$$lock_dir/generated/proof_storage_plan_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_storage_plan_v1.answers.mutated" \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	rg -Fq '(state-table-storage-v1 mm-state-formula 3 1 ' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm); \
	printf '%s\n' "$$authority_output" | \
		rg -q '^\(LangDef:RunAccepted MetamathV1 '; \
	diagnostic_status=0; \
	diagnostic_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(METAMATH_COGSLT_DIAGNOSTIC_BIN_INVOKE) --lang "$$selector" \
		--langdef-proof-backend frame-cache-diagnostic-v1 \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		2>&1) || diagnostic_status=$$?; \
	if [[ "$$diagnostic_status" -eq 0 ]]; then \
		printf '%s\n' "$$diagnostic_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$diagnostic_output" | \
		rg -q 'generated state and storage table shapes disagree'
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/finite-support-admission-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed 's/finite-dense-bitset-v1/unsupported-support-carrier-v1/' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers" \
		>"$$lock_dir/generated/proof_storage_plan_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_storage_plan_v1.answers.mutated" \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	rg -Fq 'unsupported-support-carrier-v1' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm); \
	printf '%s\n' "$$authority_output" | \
		rg -q '^\(LangDef:RunAccepted MetamathV1 '; \
	diagnostic_status=0; \
	diagnostic_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(METAMATH_COGSLT_DIAGNOSTIC_BIN_INVOKE) --lang "$$selector" \
		--langdef-proof-backend frame-cache-diagnostic-v1 \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		2>&1) || diagnostic_status=$$?; \
	if [[ "$$diagnostic_status" -eq 0 ]]; then \
		printf '%s\n' "$$diagnostic_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$diagnostic_output" | \
		rg -q 'finite support plan is unsupported by the frame backend'
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/indexed-value-admission-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed '/proof-indexed-value-plan-v1/d' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers" \
		>"$$lock_dir/generated/proof_storage_plan_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_storage_plan_v1.answers.mutated" \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	if rg -q 'proof-indexed-value-plan-v1' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; then exit 1; fi; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	authority_status=0; \
	authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_compressed_reuse.mm \
		2>&1) || authority_status=$$?; \
	if [[ "$$authority_status" -eq 0 ]]; then \
		printf '%s\n' "$$authority_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$authority_output" | \
		rg -q 'compressed proof lacks generated indexed-value admission'; \
	diagnostic_status=0; \
	diagnostic_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(METAMATH_COGSLT_DIAGNOSTIC_BIN_INVOKE) --lang "$$selector" \
		--langdef-proof-backend frame-cache-diagnostic-v1 \
		tests/langdef/metamath/positive_theorem_compressed_reuse.mm \
		2>&1) || diagnostic_status=$$?; \
	if [[ "$$diagnostic_status" -eq 0 ]]; then \
		printf '%s\n' "$$diagnostic_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$diagnostic_output" | \
		rg -q 'prepared indexed-value table is not admitted'
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/frame-index-admission-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed '/proof-frame-index-plan-v1/d' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers" \
		>"$$lock_dir/generated/proof_storage_plan_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_storage_plan_v1.answers.mutated" \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	if rg -q 'proof-frame-index-plan-v1' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; then exit 1; fi; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	authority_status=0; \
	authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_compressed_reuse.mm \
		2>&1) || authority_status=$$?; \
	if [[ "$$authority_status" -eq 0 ]]; then \
		printf '%s\n' "$$authority_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$authority_output" | \
		rg -q 'compressed proof lacks generated frame-index admission'
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/frame-index-carrier-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed 's/u32-open-addressed-index-v1/unsupported-frame-index-v1/' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers" \
		>"$$lock_dir/generated/proof_storage_plan_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_storage_plan_v1.answers.mutated" \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	rg -Fq 'unsupported-frame-index-v1' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	authority_status=0; \
	authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_compressed_reuse.mm \
		2>&1) || authority_status=$$?; \
	if [[ "$$authority_status" -eq 0 ]]; then \
		printf '%s\n' "$$authority_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$authority_output" | \
		rg -q 'generated frame-index plan does not admit the compressed backend'
	@set -euo pipefail; \
	lock_dir=$$(mktemp -d langdef/indexed-value-call-site-XXXXXX); \
	trap 'rm -rf "$$lock_dir"' EXIT INT TERM; \
	cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$lock_dir/"; \
	sed \
		-e 's/(proof-call-region-plan-v1 mm-check-theorem-compressed 14 /(proof-call-region-plan-v1 mm-check-theorem-compressed 13 /' \
		-e 's/(proof-indexed-value-plan-v1 mm-check-theorem-compressed 14 /(proof-indexed-value-plan-v1 mm-check-theorem-compressed 13 /' \
		-e 's/(proof-frame-index-plan-v1 mm-check-theorem-compressed 14 /(proof-frame-index-plan-v1 mm-check-theorem-compressed 13 /' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers" \
		>"$$lock_dir/generated/proof_storage_plan_v1.answers.mutated"; \
	mv "$$lock_dir/generated/proof_storage_plan_v1.answers.mutated" \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	rg -Fq '(proof-indexed-value-plan-v1 mm-check-theorem-compressed 13 ' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	rg -Fq '(proof-frame-index-plan-v1 mm-check-theorem-compressed 13 ' \
		"$$lock_dir/generated/proof_storage_plan_v1.answers"; \
	$(LANGDEF_COMPILER_V1_BIN) seal \
		--manifest "$$lock_dir/langdef.metta" \
		--pack "$$lock_dir/generated/normalized_parser_pack_v1.abi" \
		--compiled-cursor "$$lock_dir/generated/syntax_cursor_fold_v1.generated.so" \
		--lock-out "$$lock_dir/generated/langdef_lock_v1.metta" >/dev/null; \
	selector=$$(basename "$$lock_dir"); \
	registry_root=$$(dirname "$$lock_dir"); \
	authority_status=0; \
	authority_output=$$(CETTA_LANGDEF_ROOT="$$registry_root" \
		$(CETTA_BIN_INVOKE) --lang "$$selector" \
		tests/langdef/metamath/positive_theorem_compressed_reuse.mm \
		2>&1) || authority_status=$$?; \
	if [[ "$$authority_status" -eq 0 ]]; then \
		printf '%s\n' "$$authority_output" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$authority_output" | \
		rg -q 'indexed-value admission does not match its generated call site'
	@set -euo pipefail; \
	status=0; \
	output=$$($(CETTA_BIN_INVOKE) --lang metamath \
		--langdef-proof-backend frame-cache-diagnostic-v1 \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		2>&1) || status=$$?; \
	test "$$status" -ne 0; \
	printf '%s\n' "$$output" | \
		rg -q "unknown langdef proof backend 'frame-cache-diagnostic-v1'"
	@if nm -g $(BIN) | rg -q 'ppfirst_order_frame_decoder_v1_'; then \
		echo 'public LanguageDef binary links the diagnostic frame decoder'; \
		exit 1; \
	fi
	@nm -g $(METAMATH_COGSLT_DIAGNOSTIC_BIN_V1) | \
		rg -q 'ppfirst_order_frame_decoder_v1_admit'
	@echo '(MetamathCoGSLTLangDefV1Summary 14 14 0)'

.PHONY: test-metamath-cogslt-cli-v1 \
		test-metamath-cogslt-cli-core-v1
test-metamath-cogslt-cli-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		test-metamath-cogslt-cli-core-v1 \
		test-metamath-cogslt-lowered-artifact-mutations-core-v1

test-metamath-cogslt-cli-core-v1: $(BIN) \
		$(METAMATH_COMPILED_CURSOR_V1) $(METAMATH_LANGDEF_LOCK_V1) \
		test-metamath-cogslt-diagnostic-binary-v1 \
		tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
		tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
		tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm \
		tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'the compiled LanguageDef CLI gate requires the C-only core build'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	positive=$$($(CETTA_BIN_INVOKE) --lang metamath \
		tests/langdef/metamath/positive_constants.mm); \
	printf '%s\n' "$$positive" | rg -q \
		'^\(LangDef:RunAccepted MetamathV1 "[0-9a-f]{64}" 1\)$$'; \
	status=0; \
	malformed=$$($(CETTA_BIN_INVOKE) --lang metamath \
		tests/langdef/metamath/invalid_missing_terminator.mm 2>&1) || status=$$?; \
	if [[ $$status -ne 2 ]] || ! printf '%s\n' "$$malformed" | \
		rg -q '^\(LangDef:ParseRejected MetamathV1 '; then \
		printf '%s\n' "$$malformed" >&2; exit 1; \
	fi; \
	status=0; \
	bad_proof=$$($(CETTA_BIN_INVOKE) --lang metamath \
		tests/langdef/metamath/invalid_theorem_wrong_claim.mm 2>&1) || status=$$?; \
	if [[ $$status -ne 2 ]] || ! printf '%s\n' "$$bad_proof" | \
		rg -q '^\(LangDef:StageRejected MetamathV1 '; then \
		printf '%s\n' "$$bad_proof" >&2; exit 1; \
	fi; \
	incomplete_normal=$$($(CETTA_BIN_INVOKE) --lang metamath \
		tests/langdef/metamath/positive_theorem_normal_unknown.mm); \
	printf '%s\n' "$$incomplete_normal" | rg -q \
		'^\(LangDef:StageAcceptedIncomplete MetamathV1 '; \
	incomplete_compressed=$$($(CETTA_BIN_INVOKE) --lang metamath \
		tests/langdef/metamath/positive_theorem_compressed_unknown.mm); \
	printf '%s\n' "$$incomplete_compressed" | rg -q \
		'^\(LangDef:StageAcceptedIncomplete MetamathV1 '; \
	incomplete_after_continuation=$$($(CETTA_BIN_INVOKE) --lang metamath \
		tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm); \
	printf '%s\n' "$$incomplete_after_continuation" | rg -q \
		'^\(LangDef:StageAcceptedIncomplete MetamathV1 '; \
	for fixture in \
		tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
		tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
		tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm; do \
		status=0; \
		invalid=$$($(CETTA_BIN_INVOKE) --lang metamath "$$fixture" 2>&1) || \
			status=$$?; \
		if [[ $$status -ne 2 ]] || ! printf '%s\n' "$$invalid" | \
			rg -q '^\(LangDef:StageRejected MetamathV1 '; then \
			printf '%s\n' "$$invalid" >&2; exit 1; \
		fi; \
	done; \
	status=0; \
	diagnostic_open_save=$$($(METAMATH_COGSLT_DIAGNOSTIC_BIN_INVOKE) --lang metamath \
		--langdef-proof-backend frame-cache-diagnostic-v1 \
		tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
		2>&1) || status=$$?; \
	if [[ $$status -ne 2 ]] || ! printf '%s\n' "$$diagnostic_open_save" | \
		rg -q '^\(LangDef:StageRejected MetamathV1 '; then \
		printf '%s\n' "$$diagnostic_open_save" >&2; exit 1; \
	fi; \
	inline=$$($(CETTA_BIN_INVOKE) --lang metamath \
		-e '$$c wff |- $$. '); \
	printf '%s\n' "$$inline" | rg -q \
		'^\(LangDef:RunAccepted MetamathV1 "[0-9a-f]{64}" 1\)$$'; \
	canary_root=$$(mktemp -d runtime/langdef-cli-canary-v1.XXXXXX); \
	trap 'rm -rf "$$canary_root"' EXIT INT TERM; \
	ln -s "$$(realpath $(METAMATH_LANGDEF_DIR_V1))" \
		"$$canary_root/neutral-cogslt-v1"; \
	canary=$$(CETTA_LANGDEF_ROOT="$$canary_root" \
		$(CETTA_BIN_INVOKE) --lang neutral-cogslt-v1 \
		tests/langdef/metamath/positive_constants.mm); \
	printf '%s\n' "$$canary" | rg -q \
		'^\(LangDef:RunAccepted MetamathV1 "[0-9a-f]{64}" 1\)$$'; \
	if ldd $(BIN) | rg -qi 'python|libswipl'; then exit 1; fi; \
	if strings $(BIN) | \
		rg -qi 'metamath|[.]mm([^[:alnum:]_]|$$)|\$$[cvfeap]([^[:alnum:]_]|$$)'; \
		then exit 1; fi; \
	if rg -ni 'metamath|[.]mm([^[:alnum:]_]|$$)|\$$[cvfeap]([^[:alnum:]_]|$$)' \
		src/main.c src/library.c src/library.h \
		native/langdef_module.c native/langdef_module.h; then exit 1; fi; \
	printf '%s\n' '(MetamathCoGSLTCliV1Summary 16 16 0 renamed-selector 1)'

.PHONY: test-metamath-cogslt-lowered-artifact-mutations-v1 \
		test-metamath-cogslt-lowered-artifact-mutations-core-v1
test-metamath-cogslt-lowered-artifact-mutations-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		test-metamath-cogslt-lowered-artifact-mutations-core-v1

test-metamath-cogslt-lowered-artifact-mutations-core-v1: $(BIN) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		$(METAMATH_COMPILED_CURSOR_V1) $(METAMATH_LANGDEF_LOCK_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'lowered-artifact mutations require the C-only core build' >&2; \
		exit 1; \
	fi
	@set -euo pipefail; \
	mutation_dir=$$(mktemp -d runtime/metamath-lowered-v1-XXXXXX); \
	trap 'rm -rf "$$mutation_dir"' EXIT INT TERM; \
	mkdir -p "$$mutation_dir/generated"; \
	cp $(METAMATH_LANGDEF_DIR_V1)/*.metta "$$mutation_dir/"; \
	cp $(METAMATH_LANGDEF_GENERATED_DIR_V1)/* \
		"$$mutation_dir/generated/"; \
	selector=$$(basename "$$mutation_dir"); \
	registry_root=$$(dirname "$$mutation_dir"); \
	mutated_c="$$mutation_dir/generated/syntax_cursor_fold_v1.generated.c"; \
	canonical_c=$(METAMATH_CURSOR_FOLD_GENERATED_C_V1); \
	mutated_so="$$mutation_dir/generated/syntax_cursor_fold_v1.generated.so"; \
	mutated_lock="$$mutation_dir/generated/langdef_lock_v1.metta"; \
	capture() { \
		local selector_name="$$1"; \
		local witness="$$2"; \
		local output_file="$$3"; \
		local status_file="$$4"; \
		local status=0; \
		if [[ "$$selector_name" == metamath ]]; then \
			$(CETTA_BIN_INVOKE) --lang metamath "$$witness" \
				>"$$output_file" 2>&1 || status=$$?; \
		else \
			CETTA_LANGDEF_ROOT="$$registry_root" \
				$(CETTA_BIN_INVOKE) --lang "$$selector_name" "$$witness" \
				>"$$output_file" 2>&1 || status=$$?; \
		fi; \
		printf '%d\n' "$$status" >"$$status_file"; \
	}; \
	compile_and_seal() { \
		local mutation_name="$$1"; \
		local candidate_so="$$mutation_dir/generated/$$mutation_name.so"; \
		local candidate_lock="$$mutation_dir/generated/$$mutation_name.lock"; \
		local compile_log="$$mutation_dir/$$mutation_name.compile.log"; \
		timeout 180 $(CC) $(CPPFLAGS) \
			-Iexperiments/gslt2parse_foundation/native $(CFLAGS) \
			-fPIC -shared -Wl,--build-id=none \
			-DCETTA_LANGDEF_COMPILED_PREFIX=$(METAMATH_CURSOR_FOLD_PREFIX_V1) \
			-DCETTA_LANGDEF_COMPILED_HAS_OCCURRENCE_SPAN_MASK=1 \
			-DCETTA_LANGDEF_COMPILED_HAS_STATE_PROGRAM=1 \
			-o "$$candidate_so" $(LANGDEF_COMPILED_CURSOR_EXPORT_V1) \
			"$$mutated_c" >"$$compile_log" 2>&1; \
		mv "$$candidate_so" "$$mutated_so"; \
		timeout 60 $(LANGDEF_COMPILER_V1_BIN) seal \
			--manifest "$$mutation_dir/langdef.metta" \
			--pack "$$mutation_dir/generated/normalized_parser_pack_v1.abi" \
			--compiled-cursor "$$mutated_so" \
			--lock-out "$$candidate_lock" >>"$$compile_log" 2>&1; \
		mv "$$candidate_lock" "$$mutated_lock"; \
	}; \
	total=0; killed=0; inert=0; rebuilt=0; resealed=0; \
	for mutation in parser fold proof-compressed proof-normal include; do \
		case "$$mutation" in \
		parser) witness=tests/langdef/metamath/positive_constants.mm ;; \
		fold) witness=tests/langdef/metamath/positive_constants.mm ;; \
		proof-compressed) \
			witness=tests/langdef/metamath/invalid_theorem_compressed_out_of_range.mm ;; \
		proof-normal) \
			witness=tests/langdef/metamath/invalid_theorem_wrong_claim.mm ;; \
		include) witness=tests/langdef/metamath/include/basic_root.mm ;; \
		esac; \
		capture metamath "$$witness" \
			"$$mutation_dir/$$mutation.baseline.out" \
			"$$mutation_dir/$$mutation.baseline.status"; \
		cp "$$canonical_c" "$$mutated_c"; \
		candidate_c="$$mutation_dir/generated/$$mutation.c"; \
		case "$$mutation" in \
		parser) \
			sed '0,/result[.]final_slr[.]start_nonterminal = UINT32_C([0-9][0-9]*);/s//result.final_slr.start_nonterminal = UINT32_C(4294967295);/' \
				"$$mutated_c" >"$$candidate_c" ;; \
		fold) \
			sed '0,/{ UINT32_C(49), UINT32_C(49), UINT32_C(2), UINT32_C(5), UINT32_C(3) }/s//{ UINT32_C(49), UINT32_C(49), UINT32_C(2), UINT32_C(9), UINT32_C(3) }/' \
				"$$mutated_c" >"$$candidate_c" ;; \
		proof-compressed) \
			sed '0,/[.]kind = (PPRelationalStateActionKindV1)14,/s//.kind = (PPRelationalStateActionKindV1)4,/' \
				"$$mutated_c" >"$$candidate_c" ;; \
		proof-normal) \
			sed '0,/[.]kind = (PPRelationalStateActionKindV1)13,/s//.kind = (PPRelationalStateActionKindV1)4,/' \
				"$$mutated_c" >"$$candidate_c" ;; \
		include) \
			sed '0,/[.]kind = (PPRelationalStateActionKindV1)15,/s//.kind = (PPRelationalStateActionKindV1)4,/' \
				"$$mutated_c" >"$$candidate_c" ;; \
		esac; \
		mv "$$candidate_c" "$$mutated_c"; \
		if cmp -s "$$canonical_c" "$$mutated_c"; then \
			echo "lowered-artifact mutation was not constructed: $$mutation" >&2; \
			exit 1; \
		fi; \
		total=$$((total + 1)); \
		compile_and_seal "$$mutation"; \
		rebuilt=$$((rebuilt + 1)); resealed=$$((resealed + 1)); \
		capture "$$selector" "$$witness" \
			"$$mutation_dir/$$mutation.mutated.out" \
			"$$mutation_dir/$$mutation.mutated.status"; \
		if cmp -s "$$mutation_dir/$$mutation.baseline.status" \
				"$$mutation_dir/$$mutation.mutated.status" && \
			cmp -s "$$mutation_dir/$$mutation.baseline.out" \
				"$$mutation_dir/$$mutation.mutated.out"; then \
			inert=$$((inert + 1)); \
			echo "inert lowered-artifact mutation: $$mutation" >&2; \
		else \
			killed=$$((killed + 1)); \
		fi; \
	done; \
	if [[ "$$total" -ne 5 || "$$rebuilt" -ne 5 || \
		"$$resealed" -ne 5 || "$$killed" -ne 5 || "$$inert" -ne 0 ]]; then \
		echo 'lowered generated-C mutations were not all load-bearing' >&2; \
		exit 1; \
	fi; \
	printf '(MetamathCoGSLTLoweredArtifactMutationV1Summary %d %d %d rebuilt %d resealed %d)\n' \
		"$$total" "$$killed" "$$inert" "$$rebuilt" "$$resealed"

.PHONY: test-metamath-cogslt-every-rule-mutations-v1 \
	test-metamath-cogslt-every-rule-mutations-core-v1
test-metamath-cogslt-every-rule-mutations-v1: \
		test-metamath-cogslt-langdef-v1 $(GSLT_RULE_MUTATOR_V1_BIN)
	@set -uo pipefail; \
	jobs='$(METAMATH_COGSLT_MUTATION_JOBS_V1)'; \
	if [[ ! "$$jobs" =~ ^[1-9][0-9]*$$ ]]; then \
		echo 'invalid coGSLT mutation job count' >&2; exit 1; \
	fi; \
	status=0; pids=(); \
	for ((shard = 0; shard < jobs; shard++)); do \
		$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
			METAMATH_COGSLT_MUTATION_MODES_V1='delete falsify' \
			METAMATH_COGSLT_MUTATION_SOURCE_FILTER_V1= \
			METAMATH_COGSLT_MUTATION_RULE_FILTER_V1= \
			METAMATH_COGSLT_MUTATION_SHARD_COUNT_V1="$$jobs" \
			METAMATH_COGSLT_MUTATION_SHARD_INDEX_V1="$$shard" \
			test-metamath-cogslt-every-rule-mutations-core-v1 & \
		pids+=("$$!"); \
	done; \
	for pid in "$${pids[@]}"; do \
		if ! wait "$$pid"; then status=1; fi; \
	done; \
	if [[ "$$status" -ne 0 ]]; then exit "$$status"; fi; \
	rule_count=0; \
	for source in $(METAMATH_COGSLT_AUTHORED_RULE_SOURCES_V1); do \
		while IFS= read -r rule; do \
			if [[ -n "$$rule" ]]; then rule_count=$$((rule_count + 1)); fi; \
		done < <($(GSLT_RULE_MUTATOR_V1_BIN) list --source "$$source"); \
	done; \
	total=$$((rule_count * 2)); \
	printf '(MetamathCoGSLTEveryRuleMutationV1Summary %d %d 0)\n' \
		"$$total" "$$total"

test-metamath-cogslt-every-rule-mutations-core-v1: $(BIN) \
		$(GSLT_RULE_MUTATOR_V1_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@set -uo pipefail; \
	mutation_dir=$$(mktemp -d runtime/metamath-cogslt-every-rule.XXXXXX); \
	trap 'rm -rf "$$mutation_dir"' EXIT INT TERM; \
	mkdir -p "$$mutation_dir/generated"; \
	cp $(METAMATH_LANGDEF_DIR_V1)/*.metta "$$mutation_dir/"; \
	source_filter='$(METAMATH_COGSLT_MUTATION_SOURCE_FILTER_V1)'; \
	rule_filter='$(METAMATH_COGSLT_MUTATION_RULE_FILTER_V1)'; \
	shard_count='$(METAMATH_COGSLT_MUTATION_SHARD_COUNT_V1)'; \
	shard_index='$(METAMATH_COGSLT_MUTATION_SHARD_INDEX_V1)'; \
	modes='$(METAMATH_COGSLT_MUTATION_MODES_V1)'; \
	if [[ ! "$$shard_count" =~ ^[1-9][0-9]*$$ || \
		! "$$shard_index" =~ ^[0-9]+$$ || \
		"$$shard_index" -ge "$$shard_count" ]]; then \
		echo 'invalid coGSLT mutation shard' >&2; exit 1; \
	fi; \
	mode_count=0; \
	for mode in $$modes; do \
		case "$$mode" in delete|falsify) ;; *) \
			echo "invalid coGSLT mutation mode: $$mode" >&2; exit 1 ;; \
		esac; \
		mode_count=$$((mode_count + 1)); \
	done; \
	if [[ "$$mode_count" -eq 0 ]]; then \
		echo 'no coGSLT mutation modes selected' >&2; exit 1; \
	fi; \
	rule_count=0; candidate_ordinal=0; \
	for source in $(METAMATH_COGSLT_AUTHORED_RULE_SOURCES_V1); do \
		base=$$(basename "$$source"); \
		if [[ -n "$$source_filter" && "$$base" != "$$source_filter" ]]; then \
			continue; \
		fi; \
		while IFS= read -r rule; do \
			if [[ -n "$$rule_filter" && "$$rule" != "$$rule_filter" ]]; then \
				continue; \
			fi; \
			if [[ $$((candidate_ordinal % shard_count)) -eq "$$shard_index" ]]; then \
				rule_count=$$((rule_count + 1)); \
			fi; \
			candidate_ordinal=$$((candidate_ordinal + 1)); \
		done < <($(GSLT_RULE_MUTATOR_V1_BIN) list --source "$$source"); \
	done; \
	if [[ "$$rule_count" -eq 0 ]]; then \
		echo 'no authored coGSLT rules selected' >&2; exit 1; \
	fi; \
	expected=$$((rule_count * mode_count)); \
	total=0; killed=0; inert=0; \
	: >"$$mutation_dir/inert.tsv"; \
	generate_case() { \
		timeout 300 $(MAKE) --no-print-directory \
			BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
			METAMATH_LANGDEF_DIR_V1="$$mutation_dir" \
			METAMATH_FOLD_PART_DIR_V1="$$mutation_dir/fold-parts" \
			METAMATH_STATE_PART_DIR_V1="$$mutation_dir/state-parts" \
			generate-metamath-cogslt-langdef-v1 \
			>"$$mutation_dir/generate.log" 2>&1; \
	}; \
	public_witness_changed() { \
		output=$$(CETTA_LANGDEF_TEST_MANIFEST="$$mutation_dir/langdef.metta" \
			timeout 120 $(CETTA_BIN_INVOKE) --lang prime \
			tests/langdef/metamath/langdef_parse.metta 2>&1); \
		status=$$?; \
		[[ "$$status" -ne 0 ]] || printf '%s\n' "$$output" | rg -q '\(Error '; \
	}; \
	candidate_ordinal=0; \
	for source in $(METAMATH_COGSLT_AUTHORED_RULE_SOURCES_V1); do \
		base=$$(basename "$$source"); \
		if [[ -n "$$source_filter" && "$$base" != "$$source_filter" ]]; then \
			continue; \
		fi; \
		cp "$$source" "$$mutation_dir/$$base"; \
		while IFS= read -r rule; do \
			if [[ -n "$$rule_filter" && "$$rule" != "$$rule_filter" ]]; then \
				continue; \
			fi; \
			selected_ordinal=$$candidate_ordinal; \
			candidate_ordinal=$$((candidate_ordinal + 1)); \
			if [[ $$((selected_ordinal % shard_count)) -ne "$$shard_index" ]]; then \
				continue; \
			fi; \
			for mode in $$modes; do \
				total=$$((total + 1)); \
				if ! $(GSLT_RULE_MUTATOR_V1_BIN) mutate \
						--source "$$source" \
						--out "$$mutation_dir/$$base.next" \
						--rule "$$rule" --mode "$$mode"; then \
					echo "mutation construction failed: $$base $$rule $$mode" >&2; \
					exit 1; \
				fi; \
				mv "$$mutation_dir/$$base.next" "$$mutation_dir/$$base"; \
				if ! generate_case; then \
					killed=$$((killed + 1)); \
				elif public_witness_changed; then \
					killed=$$((killed + 1)); \
				else \
					inert=$$((inert + 1)); \
					printf '%s\t%s\t%s\n' "$$base" "$$rule" "$$mode" | \
						tee -a "$$mutation_dir/inert.tsv" >&2; \
				fi; \
				if (( total % 25 == 0 || total == expected )); then \
					printf 'Metamath coGSLT rule mutations: %d/%d, killed=%d, inert=%d\n' \
						"$$total" "$$expected" "$$killed" "$$inert"; \
				fi; \
			done; \
		done < <($(GSLT_RULE_MUTATOR_V1_BIN) list --source "$$source"); \
		cp "$$source" "$$mutation_dir/$$base"; \
	done; \
	if [[ "$$total" -ne "$$expected" || \
		$$((killed + inert)) -ne "$$expected" || "$$inert" -ne 0 ]]; then \
		echo 'inert authored coGSLT mutations remain' >&2; \
		cat "$$mutation_dir/inert.tsv" >&2; \
		exit 1; \
	fi; \
	printf '(MetamathCoGSLTEveryRuleMutationV1Summary %d %d %d)\n' \
		"$$total" "$$killed" "$$inert"

.PHONY: test-metamath-cogslt-generated-form-mutations-v1 \
	test-metamath-cogslt-generated-form-mutations-driver-v1 \
	test-metamath-cogslt-generated-form-mutations-core-v1
test-metamath-cogslt-generated-form-mutations-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		METAMATH_COGSLT_GENERATED_MUTATION_JOBS_V1="$(METAMATH_COGSLT_GENERATED_MUTATION_JOBS_V1)" \
		test-metamath-cogslt-generated-form-mutations-driver-v1

test-metamath-cogslt-generated-form-mutations-driver-v1: \
		test-metamath-cogslt-langdef-v1 $(GSLT_RULE_MUTATOR_V1_BIN)
	@set -uo pipefail; \
	jobs='$(METAMATH_COGSLT_GENERATED_MUTATION_JOBS_V1)'; \
	if [[ ! "$$jobs" =~ ^[1-9][0-9]*$$ ]]; then \
		echo 'invalid generated-form mutation job count' >&2; exit 1; \
	fi; \
	status=0; pids=(); \
	for ((shard = 0; shard < jobs; shard++)); do \
		$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			BIN=$(BIN) \
			GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
			METAMATH_COGSLT_GENERATED_MUTATION_MODES_V1='delete falsify' \
			METAMATH_COGSLT_GENERATED_MUTATION_ARTIFACT_FILTER_V1= \
			METAMATH_COGSLT_GENERATED_MUTATION_FORM_FILTER_V1= \
			METAMATH_COGSLT_GENERATED_MUTATION_SHARD_COUNT_V1="$$jobs" \
			METAMATH_COGSLT_GENERATED_MUTATION_SHARD_INDEX_V1="$$shard" \
			test-metamath-cogslt-generated-form-mutations-core-v1 & \
		pids+=("$$!"); \
	done; \
	for pid in "$${pids[@]}"; do \
		if ! wait "$$pid"; then status=1; fi; \
	done; \
	if [[ "$$status" -ne 0 ]]; then exit "$$status"; fi; \
	form_count=0; \
	for artifact in $(METAMATH_COGSLT_GENERATED_FORM_ARTIFACTS_V1); do \
		while IFS=$$'\t' read -r index head; do \
			if [[ -n "$$head" ]]; then form_count=$$((form_count + 1)); fi; \
		done < <($(GSLT_RULE_MUTATOR_V1_BIN) list-forms \
			--source "$$artifact"); \
	done; \
	total=$$((form_count * 2)); \
	printf '(MetamathCoGSLTGeneratedFormMutationV1Summary %d %d 0 %d)\n' \
		"$$total" "$$total" "$$jobs"

test-metamath-cogslt-generated-form-mutations-core-v1: $(BIN) \
		$(GSLT_RULE_MUTATOR_V1_BIN) \
		$(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN) \
		$(LANGDEF_COMPILER_V1_BIN)
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'generated-form mutations require the C-only core build' >&2; \
		exit 1; \
	fi
	@set -uo pipefail; \
	mutation_dir=$$(mktemp -d runtime/metamath-cogslt-generated-form.XXXXXX); \
	trap 'rm -rf "$$mutation_dir"' EXIT INT TERM; \
	mkdir -p "$$mutation_dir/generated"; \
	cp $(METAMATH_LANGDEF_DIR_V1)/*.metta "$$mutation_dir/"; \
	cp $(METAMATH_LANGDEF_GENERATED_DIR_V1)/* "$$mutation_dir/generated/"; \
	artifact_filter='$(METAMATH_COGSLT_GENERATED_MUTATION_ARTIFACT_FILTER_V1)'; \
	form_filter='$(METAMATH_COGSLT_GENERATED_MUTATION_FORM_FILTER_V1)'; \
	shard_count='$(METAMATH_COGSLT_GENERATED_MUTATION_SHARD_COUNT_V1)'; \
	shard_index='$(METAMATH_COGSLT_GENERATED_MUTATION_SHARD_INDEX_V1)'; \
	modes='$(METAMATH_COGSLT_GENERATED_MUTATION_MODES_V1)'; \
	if [[ ! "$$shard_count" =~ ^[1-9][0-9]*$$ || \
		! "$$shard_index" =~ ^[0-9]+$$ || \
		"$$shard_index" -ge "$$shard_count" ]]; then \
		echo 'invalid generated-form mutation shard' >&2; exit 1; \
	fi; \
	mode_count=0; \
	for mode in $$modes; do \
		case "$$mode" in delete|falsify) ;; *) \
			echo "invalid generated-form mutation mode: $$mode" >&2; \
			exit 1 ;; \
		esac; \
		mode_count=$$((mode_count + 1)); \
	done; \
	if [[ "$$mode_count" -eq 0 ]]; then \
		echo 'no generated-form mutation modes selected' >&2; exit 1; \
	fi; \
	form_count=0; candidate_ordinal=0; \
	for artifact in $(METAMATH_COGSLT_GENERATED_FORM_ARTIFACTS_V1); do \
		base=$$(basename "$$artifact"); \
		if [[ -n "$$artifact_filter" && "$$base" != "$$artifact_filter" ]]; then \
			continue; \
		fi; \
		while IFS=$$'\t' read -r index head; do \
			if [[ -n "$$form_filter" && "$$head" != "$$form_filter" && \
				"$$index" != "$$form_filter" ]]; then \
				continue; \
			fi; \
			if [[ $$((candidate_ordinal % shard_count)) -eq "$$shard_index" ]]; then \
				form_count=$$((form_count + 1)); \
			fi; \
			candidate_ordinal=$$((candidate_ordinal + 1)); \
		done < <($(GSLT_RULE_MUTATOR_V1_BIN) list-forms \
			--source "$$artifact"); \
	done; \
	if [[ "$$form_count" -eq 0 ]]; then \
		echo 'no generated forms selected' >&2; exit 1; \
	fi; \
	expected=$$((form_count * mode_count)); \
	total=0; killed=0; inert=0; rejected=0; witnessed=0; \
	: >"$$mutation_dir/inert.tsv"; \
	compile_case() { \
		mutated_c="$$mutation_dir/generated/syntax_cursor_fold_v1.mutated.c"; \
		mutated_so="$$mutation_dir/generated/syntax_cursor_fold_v1.mutated.so"; \
		mutated_lock="$$mutation_dir/generated/langdef_lock_v1.mutated.metta"; \
		timeout 180 $(PARSER_PACK_CURSOR_COMPILE_V1_STREAM_BIN) \
			--abi "$$mutation_dir/generated/normalized_parser_pack_v1.abi" \
			--lexical-answers "$$mutation_dir/generated/lexical_nfa_v1.answers" \
			--guard-answers "$$mutation_dir/generated/empty_guard_v1.answers" \
			--guard-evidence "$$mutation_dir/generated/empty_guard_v1.evidence" \
			--guarded-answers "$$mutation_dir/generated/empty_guard_v1.answers" \
			--regular-compiler-digest $(METAMATH_REGULAR_COMPILER_DIGEST_V1) \
			--guarded-compiler-digest $(METAMATH_GUARDED_COMPILER_DIGEST_V1) \
			--action-answers "$$mutation_dir/generated/action_bytecode_v1.answers" \
			--action-compiler-digest $(METAMATH_ACTION_COMPILER_DIGEST_V1) \
			--occurrence-fold-answers "$$mutation_dir/generated/occurrence_fold_v1.answers" \
			--occurrence-span-mask-answers "$$mutation_dir/generated/occurrence_span_mask_v1.answers" \
			--occurrence-span-mask-compiler-digest $(METAMATH_OCCURRENCE_SPAN_MASK_COMPILER_DIGEST_V1) \
			--state-answers "$$mutation_dir/generated/state_program_v1.answers" \
			--out-c "$$mutated_c" \
			--prefix $(METAMATH_CURSOR_FOLD_PREFIX_V1) \
			>"$$mutation_dir/compile.log" 2>&1 || return 1; \
		timeout 180 $(CC) $(CPPFLAGS) \
			-Iexperiments/gslt2parse_foundation/native $(CFLAGS) \
			-fPIC -shared -Wl,--build-id=none \
			-DCETTA_LANGDEF_COMPILED_PREFIX=$(METAMATH_CURSOR_FOLD_PREFIX_V1) \
			-DCETTA_LANGDEF_COMPILED_HAS_OCCURRENCE_SPAN_MASK=1 \
			-DCETTA_LANGDEF_COMPILED_HAS_STATE_PROGRAM=1 \
			-o "$$mutated_so" $(LANGDEF_COMPILED_CURSOR_EXPORT_V1) \
			"$$mutated_c" >>"$$mutation_dir/compile.log" 2>&1 || return 1; \
		mv "$$mutated_c" \
			"$$mutation_dir/generated/syntax_cursor_fold_v1.generated.c"; \
		mv "$$mutated_so" \
			"$$mutation_dir/generated/syntax_cursor_fold_v1.generated.so"; \
		timeout 60 $(LANGDEF_COMPILER_V1_BIN) seal \
			--manifest "$$mutation_dir/langdef.metta" \
			--pack "$$mutation_dir/generated/normalized_parser_pack_v1.abi" \
			--compiled-cursor \
			"$$mutation_dir/generated/syntax_cursor_fold_v1.generated.so" \
			--lock-out "$$mutated_lock" \
			>>"$$mutation_dir/compile.log" 2>&1 || return 1; \
		mv "$$mutated_lock" \
			"$$mutation_dir/generated/langdef_lock_v1.metta"; \
	}; \
	public_witness_changed() { \
		output=$$(CETTA_LANGDEF_TEST_MANIFEST="$$mutation_dir/langdef.metta" \
			timeout 180 $(CETTA_BIN_INVOKE) --lang prime \
			tests/langdef/metamath/langdef_parse.metta 2>&1); \
		status=$$?; \
		[[ "$$status" -ne 0 ]] || printf '%s\n' "$$output" | rg -q '\(Error '; \
	}; \
	candidate_ordinal=0; \
	for artifact in $(METAMATH_COGSLT_GENERATED_FORM_ARTIFACTS_V1); do \
		base=$$(basename "$$artifact"); \
		if [[ -n "$$artifact_filter" && "$$base" != "$$artifact_filter" ]]; then \
			continue; \
		fi; \
		while IFS=$$'\t' read -r index head; do \
			if [[ -n "$$form_filter" && "$$head" != "$$form_filter" && \
				"$$index" != "$$form_filter" ]]; then \
				continue; \
			fi; \
			selected_ordinal=$$candidate_ordinal; \
			candidate_ordinal=$$((candidate_ordinal + 1)); \
			if [[ $$((selected_ordinal % shard_count)) -ne "$$shard_index" ]]; then \
				continue; \
			fi; \
			for mode in $$modes; do \
				total=$$((total + 1)); \
				replacement_args=(); \
				if [[ "$$mode" == falsify ]]; then \
					case "$$head" in \
					compile-pack-action-program) \
						replacement_args=(--replacement pbc-nil) ;; \
					occurrence-fold-production-v1) \
						replacement_args=(--replacement \
							'(fold-one gslt-mutation-role-v1)') ;; \
					state-action-v1) \
						replacement_args=(--replacement state-noop-v1 \
							--alternate \
							'(state-require-depth-v1 4294967295)') ;; \
					esac; \
				fi; \
				if ! $(GSLT_RULE_MUTATOR_V1_BIN) mutate-form \
						--source "$$artifact" \
						--out "$$mutation_dir/generated/$$base" \
						--index "$$index" --mode "$$mode" \
						"$${replacement_args[@]}"; then \
					echo "generated mutation construction failed: $$base $$index $$mode" >&2; \
					exit 1; \
				fi; \
				if ! compile_case; then \
					killed=$$((killed + 1)); \
					rejected=$$((rejected + 1)); \
				elif public_witness_changed; then \
					killed=$$((killed + 1)); \
					witnessed=$$((witnessed + 1)); \
				else \
					inert=$$((inert + 1)); \
					printf '%s\t%s\t%s\t%s\n' \
						"$$base" "$$index" "$$head" "$$mode" | \
						tee -a "$$mutation_dir/inert.tsv" >&2; \
				fi; \
				if (( total % 25 == 0 || total == expected )); then \
					printf 'Metamath generated-form mutations: %d/%d, killed=%d, inert=%d, rejected=%d, witnessed=%d\n' \
						"$$total" "$$expected" "$$killed" "$$inert" \
						"$$rejected" "$$witnessed"; \
				fi; \
			done; \
		done < <($(GSLT_RULE_MUTATOR_V1_BIN) list-forms \
			--source "$$artifact"); \
		cp "$$artifact" "$$mutation_dir/generated/$$base"; \
	done; \
	if [[ -z "$$artifact_filter" && -z "$$form_filter" && \
		"$$modes" == 'delete falsify' && "$$witnessed" -eq 0 ]]; then \
		echo 'generated forms never reached a recompiled public witness' >&2; \
		exit 1; \
	fi; \
	if [[ "$$total" -ne "$$expected" || \
		$$((killed + inert)) -ne "$$expected" || "$$inert" -ne 0 ]]; then \
		echo 'inert generated coGSLT forms remain' >&2; \
		cat "$$mutation_dir/inert.tsv" >&2; \
		exit 1; \
	fi; \
	printf '(MetamathCoGSLTGeneratedFormMutationShardV1Summary %d %d %d %d %d)\n' \
		"$$total" "$$killed" "$$inert" "$$rejected" "$$witnessed"

.PHONY: test-metamath-cogslt-include-mutations-v1
test-metamath-cogslt-include-mutations-v1: \
		test-metamath-cogslt-langdef-v1 $(BIN) \
		$(METAMATH_COMPILED_CURSOR_V1) $(METAMATH_LANGDEF_LOCK_V1)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	syntax_dir=$$(mktemp -d langdef/metamath-include-syntax.XXXXXX); \
	fold_dir=$$(mktemp -d langdef/metamath-include-fold.XXXXXX); \
	action_dir=$$(mktemp -d langdef/metamath-include-action.XXXXXX); \
	completed_dir=$$(mktemp -d langdef/metamath-include-completed.XXXXXX); \
	active_dir=$$(mktemp -d langdef/metamath-include-active.XXXXXX); \
	trap 'rm -rf "$$syntax_dir" "$$fold_dir" "$$action_dir" "$$completed_dir" "$$active_dir"' \
		EXIT INT TERM; \
	build_mutation() { \
		$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
			METAMATH_LANGDEF_DIR_V1="$$1" \
			METAMATH_FOLD_PART_DIR_V1="$$1/fold-parts" \
			METAMATH_STATE_PART_DIR_V1="$$1/state-parts" \
			generate-metamath-cogslt-langdef-v1 >/dev/null; \
	}; \
	require_changed_program() { \
		if cmp -s $(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
			"$$1/generated/syntax_cursor_fold_v1.generated.c"; then \
			exit 1; \
		fi; \
	}; \
	import_result() { \
		$(CETTA_BIN_INVOKE) --lang prime \
			-e '!(import! &self langdef)' \
			-e "!(bind! &mutation-langdef (langdef:load \"$$1/langdef.metta\"))" \
			-e "!(println! (langdef:import-file &mutation-langdef \"$$2\"))"; \
	}; \
	for mutation_dir in "$$syntax_dir" "$$fold_dir" "$$action_dir" \
		"$$completed_dir" "$$active_dir"; do \
		cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$mutation_dir/"; \
	done; \
	sed '/(rule def-kw-include-open-lexeme /s/(cp 91)/(cp 60)/' \
		"$$syntax_dir/syntax_v1.metta" \
		>"$$syntax_dir/syntax_v1.mutated"; \
	mv "$$syntax_dir/syntax_v1.mutated" "$$syntax_dir/syntax_v1.metta"; \
	rg -q -U 'def-kw-include-open-lexeme(.|\n)*(cp 60)' \
		"$$syntax_dir/syntax_v1.metta"; \
	build_mutation "$$syntax_dir"; \
	require_changed_program "$$syntax_dir"; \
	mutation_output=$$(import_result "$$syntax_dir" \
		tests/langdef/metamath/include/basic_root.mm); \
	rg -q 'LangDef:ParseRejected MetamathV1' <<<"$$mutation_output"; \
	sed '/    (rule mm-fold-node-include/,/      (body))/d' \
		"$$fold_dir/source_fold_v1.metta" \
		>"$$fold_dir/source_fold_v1.mutated"; \
	mv "$$fold_dir/source_fold_v1.mutated" \
		"$$fold_dir/source_fold_v1.metta"; \
	if rg -q 'mm-fold-node-include' "$$fold_dir/source_fold_v1.metta"; \
		then exit 1; fi; \
	build_mutation "$$fold_dir"; \
	require_changed_program "$$fold_dir"; \
	mutation_output=$$(import_result "$$fold_dir" \
		tests/langdef/metamath/include/basic_root.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	sed '/    (rule mm-state-include-resolve/,/      (body))/d' \
		"$$action_dir/state_v1.metta" \
		>"$$action_dir/state_v1.mutated"; \
	mv "$$action_dir/state_v1.mutated" "$$action_dir/state_v1.metta"; \
	if rg -q 'mm-state-include-resolve' "$$action_dir/state_v1.metta"; \
		then exit 1; fi; \
	build_mutation "$$action_dir"; \
	require_changed_program "$$action_dir"; \
	mutation_output=$$(import_result "$$action_dir" \
		tests/langdef/metamath/include/basic_root.mm); \
	rg -q 'LangDef:StageUnsupported MetamathV1' <<<"$$mutation_output"; \
	sed '/    (rule mm-state-include-resolve/,/      (body))/s/state-skip-completed-source-v1/state-reject-completed-source-v1/' \
		"$$completed_dir/state_v1.metta" \
		>"$$completed_dir/state_v1.mutated"; \
	mv "$$completed_dir/state_v1.mutated" \
		"$$completed_dir/state_v1.metta"; \
	rg -q -U 'mm-state-include-resolve(.|\n)*state-reject-completed-source-v1' \
		"$$completed_dir/state_v1.metta"; \
	build_mutation "$$completed_dir"; \
	require_changed_program "$$completed_dir"; \
	mutation_output=$$(import_result "$$completed_dir" \
		tests/langdef/metamath/include/duplicate_root.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	sed '/    (rule mm-state-include-resolve/,/      (body))/s/state-skip-active-source-v1/state-reject-active-source-v1/' \
		"$$active_dir/state_v1.metta" \
		>"$$active_dir/state_v1.mutated"; \
	mv "$$active_dir/state_v1.mutated" "$$active_dir/state_v1.metta"; \
	rg -q -U 'mm-state-include-resolve(.|\n)*state-reject-active-source-v1' \
		"$$active_dir/state_v1.metta"; \
	build_mutation "$$active_dir"; \
	require_changed_program "$$active_dir"; \
	mutation_output=$$(import_result "$$active_dir" \
		tests/langdef/metamath/include/self_root.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	printf '%s\n' '(MetamathCoGSLTIncludeMutationV1Summary 5 5 0)'

.PHONY: test-metamath-cogslt-state-mutations-v1
test-metamath-cogslt-state-mutations-v1: test-metamath-cogslt-langdef-v1 \
		$(BIN) \
		$(METAMATH_COMPILED_CURSOR_V1) $(METAMATH_LANGDEF_LOCK_V1) \
		$(GSLT_RULE_MUTATOR_V1_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	depth_dir=$$(mktemp -d langdef/metamath-state-depth.XXXXXX); \
	literal_dir=$$(mktemp -d langdef/metamath-state-literal.XXXXXX); \
	active_dir=$$(mktemp -d langdef/metamath-state-active.XXXXXX); \
	block_dir=$$(mktemp -d langdef/metamath-state-block.XXXXXX); \
	disjoint_active_dir=$$(mktemp -d langdef/metamath-state-disjoint-active.XXXXXX); \
	disjoint_pair_dir=$$(mktemp -d langdef/metamath-state-disjoint-pair.XXXXXX); \
	float_type_dir=$$(mktemp -d langdef/metamath-state-float-type.XXXXXX); \
	float_active_dir=$$(mktemp -d langdef/metamath-state-float-active.XXXXXX); \
	float_unique_dir=$$(mktemp -d langdef/metamath-state-float-unique.XXXXXX); \
	label_unique_dir=$$(mktemp -d langdef/metamath-state-label-unique.XXXXXX); \
	label_symbol_dir=$$(mktemp -d langdef/metamath-state-label-symbol.XXXXXX); \
	symbol_label_dir=$$(mktemp -d langdef/metamath-state-symbol-label.XXXXXX); \
	axiom_type_dir=$$(mktemp -d langdef/metamath-state-axiom-type.XXXXXX); \
	axiom_usable_dir=$$(mktemp -d langdef/metamath-state-axiom-usable.XXXXXX); \
	axiom_label_unique_dir=$$(mktemp -d langdef/metamath-state-axiom-label-unique.XXXXXX); \
	axiom_label_symbol_dir=$$(mktemp -d langdef/metamath-state-axiom-label-symbol.XXXXXX); \
	axiom_frame_dir=$$(mktemp -d langdef/metamath-state-axiom-frame.XXXXXX); \
	trap 'rm -rf "$$depth_dir" "$$literal_dir" "$$active_dir" "$$block_dir" "$$disjoint_active_dir" "$$disjoint_pair_dir" "$$float_type_dir" "$$float_active_dir" "$$float_unique_dir" "$$label_unique_dir" "$$label_symbol_dir" "$$symbol_label_dir" "$$axiom_type_dir" "$$axiom_usable_dir" "$$axiom_label_unique_dir" "$$axiom_label_symbol_dir" "$$axiom_frame_dir"' \
		EXIT INT TERM; \
	build_mutation() { \
		$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
			METAMATH_LANGDEF_DIR_V1="$$1" \
			METAMATH_FOLD_PART_DIR_V1="$$1/fold-parts" \
			METAMATH_STATE_PART_DIR_V1="$$1/state-parts" \
			generate-metamath-cogslt-langdef-v1 >/dev/null; \
	}; \
	require_changed_artifacts() { \
		if cmp -s $(METAMATH_STATE_PROGRAM_V1) \
			"$$1/generated/state_program_v1.answers"; then \
			exit 1; \
		fi; \
		if cmp -s $(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
			"$$1/generated/syntax_cursor_fold_v1.generated.c"; then \
			exit 1; \
		fi; \
	}; \
	import_result() { \
		$(CETTA_BIN_INVOKE) --lang prime \
			-e '!(import! &self langdef)' \
			-e "!(bind! &mutation-langdef (langdef:load \"$$1/langdef.metta\"))" \
			-e "!(println! (langdef:import-file &mutation-langdef \"$$2\"))"; \
	}; \
	for mutation_dir in "$$depth_dir" "$$literal_dir" \
		"$$active_dir" "$$block_dir" "$$disjoint_active_dir" \
		"$$disjoint_pair_dir" "$$float_type_dir" "$$float_active_dir" \
		"$$float_unique_dir" \
		"$$label_unique_dir" "$$label_symbol_dir" "$$symbol_label_dir" \
		"$$axiom_type_dir" "$$axiom_usable_dir" \
		"$$axiom_label_unique_dir" "$$axiom_label_symbol_dir" \
		"$$axiom_frame_dir"; do \
		cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$mutation_dir/"; \
	done; \
	sed '/(rule mm-state-constants-top-level/,/      (body))/s/(state-require-depth-v1 0)/state-noop-v1/' \
		"$$depth_dir/state_v1.metta" \
		>"$$depth_dir/state_v1.mutated"; \
	mv "$$depth_dir/state_v1.mutated" \
		"$$depth_dir/state_v1.metta"; \
	rg -q -U 'mm-state-constants-top-level(.|\n)*state-noop-v1' \
		"$$depth_dir/state_v1.metta"; \
	build_mutation "$$depth_dir"; \
	require_changed_artifacts "$$depth_dir"; \
	mutation_output=$$(import_result "$$depth_dir" \
		tests/langdef/metamath/invalid_inner_constant.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 4' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-constants-classify/,/      (body))/s/mm-symbol-constant/mm-symbol-variable/' \
		"$$literal_dir/state_v1.metta" \
		>"$$literal_dir/state_v1.mutated"; \
	mv "$$literal_dir/state_v1.mutated" \
		"$$literal_dir/state_v1.metta"; \
	rg -q -U 'mm-state-constants-classify(.|\n)*mm-symbol-variable' \
		"$$literal_dir/state_v1.metta"; \
	build_mutation "$$literal_dir"; \
	require_changed_artifacts "$$literal_dir"; \
	mutation_output=$$(import_result "$$literal_dir" \
		tests/langdef/metamath/invalid_symbol_kind_conflict.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 2' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-variables-activate/,/      (body))/s/state-require-key-absent-v1/state-insert-or-require-equal-v1/' \
		"$$active_dir/state_v1.metta" \
		>"$$active_dir/state_v1.mutated"; \
	mv "$$active_dir/state_v1.mutated" \
		"$$active_dir/state_v1.metta"; \
	rg -q -U 'mm-state-variables-activate(.|\n)*state-insert-or-require-equal-v1' \
		"$$active_dir/state_v1.metta"; \
	build_mutation "$$active_dir"; \
	require_changed_artifacts "$$active_dir"; \
	mutation_output=$$(import_result "$$active_dir" \
		tests/langdef/metamath/invalid_duplicate_active_variable.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 2' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-complete-block/,/      (body))/d' \
		"$$block_dir/state_v1.metta" \
		>"$$block_dir/state_v1.mutated"; \
	mv "$$block_dir/state_v1.mutated" \
		"$$block_dir/state_v1.metta"; \
	if rg -q 'mm-state-complete-block' \
		"$$block_dir/state_v1.metta"; then exit 1; fi; \
	build_mutation "$$block_dir"; \
	require_changed_artifacts "$$block_dir"; \
	mutation_output=$$(import_result "$$block_dir" \
		tests/langdef/metamath/positive_scoped_reactivation.mm); \
	rg -q 'LangDef:StageUnsupported MetamathV1' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-disjoint-variables-active/,/      (body))/d' \
		"$$disjoint_active_dir/state_v1.metta" | \
		sed '/(rule mm-state-disjoint-variables-pairwise/,/      (body))/s/^          1$$/          0/' \
		>"$$disjoint_active_dir/state_v1.mutated"; \
	mv "$$disjoint_active_dir/state_v1.mutated" \
		"$$disjoint_active_dir/state_v1.metta"; \
	if rg -q 'mm-state-disjoint-variables-active' \
		"$$disjoint_active_dir/state_v1.metta"; then exit 1; fi; \
	rg -q -U 'mm-state-disjoint-variables-pairwise(.|\n)*\n          0\n' \
		"$$disjoint_active_dir/state_v1.metta"; \
	build_mutation "$$disjoint_active_dir"; \
	require_changed_artifacts "$$disjoint_active_dir"; \
	mutation_output=$$(import_result "$$disjoint_active_dir" \
		tests/langdef/metamath/invalid_inactive_disjoint_variable.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 6' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-disjoint-variables-pairwise/,/      (body))/d' \
		"$$disjoint_pair_dir/state_v1.metta" \
		>"$$disjoint_pair_dir/state_v1.mutated"; \
	mv "$$disjoint_pair_dir/state_v1.mutated" \
		"$$disjoint_pair_dir/state_v1.metta"; \
	if rg -q 'mm-state-disjoint-variables-pairwise' \
		"$$disjoint_pair_dir/state_v1.metta"; then exit 1; fi; \
	build_mutation "$$disjoint_pair_dir"; \
	require_changed_artifacts "$$disjoint_pair_dir"; \
	mutation_output=$$(import_result "$$disjoint_pair_dir" \
		tests/langdef/metamath/invalid_duplicate_disjoint_variable.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 2' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-floating-typecode-constant/,/      (body))/s/mm-symbol-constant/mm-symbol-variable/' \
		"$$float_type_dir/state_v1.metta" \
		>"$$float_type_dir/state_v1.mutated"; \
	mv "$$float_type_dir/state_v1.mutated" \
		"$$float_type_dir/state_v1.metta"; \
	rg -q -U 'mm-state-floating-typecode-constant(.|\n)*mm-symbol-variable' \
		"$$float_type_dir/state_v1.metta"; \
	build_mutation "$$float_type_dir"; \
	require_changed_artifacts "$$float_type_dir"; \
	mutation_output=$$(import_result "$$float_type_dir" \
		tests/langdef/metamath/invalid_floating_typecode.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 2' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-floating-variable-active/,/      (body))/s/state-require-row-v1/state-require-row-key-absent-v1/' \
		"$$float_active_dir/state_v1.metta" \
		>"$$float_active_dir/state_v1.mutated"; \
	mv "$$float_active_dir/state_v1.mutated" \
		"$$float_active_dir/state_v1.metta"; \
	rg -q -U 'mm-state-floating-variable-active(.|\n)*state-require-row-key-absent-v1' \
		"$$float_active_dir/state_v1.metta"; \
	build_mutation "$$float_active_dir"; \
	require_changed_artifacts "$$float_active_dir"; \
	mutation_output=$$(import_result "$$float_active_dir" \
		tests/langdef/metamath/invalid_floating_inactive_variable.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 7' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-floating-active-unique/,/      (body))/s/state-require-key-absent-v1/state-insert-or-require-equal-v1/' \
		"$$float_unique_dir/state_v1.metta" \
		>"$$float_unique_dir/state_v1.mutated"; \
	mv "$$float_unique_dir/state_v1.mutated" \
		"$$float_unique_dir/state_v1.metta"; \
	rg -q -U 'mm-state-floating-active-unique(.|\n)*state-insert-or-require-equal-v1' \
		"$$float_unique_dir/state_v1.metta"; \
	build_mutation "$$float_unique_dir"; \
	require_changed_artifacts "$$float_unique_dir"; \
	mutation_output=$$(import_result "$$float_unique_dir" \
		tests/langdef/metamath/invalid_duplicate_active_floating.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 4' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-floating-label-fresh/,/      (body))/s/mm-state-label-name/mm-state-symbol-kind/' \
		"$$label_unique_dir/state_v1.metta" \
		>"$$label_unique_dir/state_v1.mutated"; \
	mv "$$label_unique_dir/state_v1.mutated" \
		"$$label_unique_dir/state_v1.metta"; \
	rg -q -U 'mm-state-floating-label-fresh(.|\n)*mm-state-symbol-kind' \
		"$$label_unique_dir/state_v1.metta"; \
	build_mutation "$$label_unique_dir"; \
	require_changed_artifacts "$$label_unique_dir"; \
	mutation_output=$$(import_result "$$label_unique_dir" \
		tests/langdef/metamath/invalid_duplicate_floating_label.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 7' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-floating-label-not-symbol/,/      (body))/s/mm-state-symbol-kind/mm-state-label-name/' \
		"$$label_symbol_dir/state_v1.metta" \
		>"$$label_symbol_dir/state_v1.mutated"; \
	mv "$$label_symbol_dir/state_v1.mutated" \
		"$$label_symbol_dir/state_v1.metta"; \
	rg -q -U 'mm-state-floating-label-not-symbol(.|\n)*mm-state-label-name' \
		"$$label_symbol_dir/state_v1.metta"; \
	build_mutation "$$label_symbol_dir"; \
	require_changed_artifacts "$$label_symbol_dir"; \
	mutation_output=$$(import_result "$$label_symbol_dir" \
		tests/langdef/metamath/invalid_floating_label_symbol.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 3' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-constants-not-labels/,/      (body))/s/mm-state-label-name/mm-state-symbol-kind/' \
		"$$symbol_label_dir/state_v1.metta" \
		>"$$symbol_label_dir/state_v1.mutated"; \
	mv "$$symbol_label_dir/state_v1.mutated" \
		"$$symbol_label_dir/state_v1.metta"; \
	rg -q -U 'mm-state-constants-not-labels(.|\n)*mm-state-symbol-kind' \
		"$$symbol_label_dir/state_v1.metta"; \
	build_mutation "$$symbol_label_dir"; \
	require_changed_artifacts "$$symbol_label_dir"; \
	mutation_output=$$(import_result "$$symbol_label_dir" \
		tests/langdef/metamath/invalid_symbol_after_label.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 4' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-axiom-typecode-constant/,/      (body))/s/mm-symbol-constant/mm-symbol-variable/' \
		"$$axiom_type_dir/state_v1.metta" \
		>"$$axiom_type_dir/state_v1.mutated"; \
	mv "$$axiom_type_dir/state_v1.mutated" \
		"$$axiom_type_dir/state_v1.metta"; \
	rg -q -U 'mm-state-axiom-typecode-constant(.|\n)*mm-symbol-variable' \
		"$$axiom_type_dir/state_v1.metta"; \
	build_mutation "$$axiom_type_dir"; \
	require_changed_artifacts "$$axiom_type_dir"; \
	mutation_output=$$(import_result "$$axiom_type_dir" \
		tests/langdef/metamath/invalid_axiom_typecode.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 4' \
		<<<"$$mutation_output"; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source "$$axiom_usable_dir/state_v1.metta" \
		--out "$$axiom_usable_dir/state_v1.mutated" \
		--rule mm-state-axiom-symbols-usable --mode falsify; \
	mv "$$axiom_usable_dir/state_v1.mutated" \
		"$$axiom_usable_dir/state_v1.metta"; \
	rg -q -U 'mm-state-axiom-symbols-usable(.|\n)*state-noop-v1' \
		"$$axiom_usable_dir/state_v1.metta"; \
	build_mutation "$$axiom_usable_dir"; \
	require_changed_artifacts "$$axiom_usable_dir"; \
	mutation_output=$$(import_result "$$axiom_usable_dir" \
		tests/langdef/metamath/invalid_axiom_variable_without_float.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 3' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-axiom-label-fresh/,/      (body))/s/mm-state-label-name/mm-state-symbol-kind/' \
		"$$axiom_label_unique_dir/state_v1.metta" \
		>"$$axiom_label_unique_dir/state_v1.mutated"; \
	mv "$$axiom_label_unique_dir/state_v1.mutated" \
		"$$axiom_label_unique_dir/state_v1.metta"; \
	rg -q -U 'mm-state-axiom-label-fresh(.|\n)*mm-state-symbol-kind' \
		"$$axiom_label_unique_dir/state_v1.metta"; \
	build_mutation "$$axiom_label_unique_dir"; \
	require_changed_artifacts "$$axiom_label_unique_dir"; \
	mutation_output=$$(import_result "$$axiom_label_unique_dir" \
		tests/langdef/metamath/invalid_duplicate_axiom_label.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 3' \
		<<<"$$mutation_output"; \
	sed '/(rule mm-state-axiom-label-not-symbol/,/      (body))/s/mm-state-symbol-kind/mm-state-label-name/' \
		"$$axiom_label_symbol_dir/state_v1.metta" \
		>"$$axiom_label_symbol_dir/state_v1.mutated"; \
	mv "$$axiom_label_symbol_dir/state_v1.mutated" \
		"$$axiom_label_symbol_dir/state_v1.metta"; \
	rg -q -U 'mm-state-axiom-label-not-symbol(.|\n)*mm-state-label-name' \
		"$$axiom_label_symbol_dir/state_v1.metta"; \
	build_mutation "$$axiom_label_symbol_dir"; \
	require_changed_artifacts "$$axiom_label_symbol_dir"; \
	mutation_output=$$(import_result "$$axiom_label_symbol_dir" \
		tests/langdef/metamath/invalid_axiom_label_symbol.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 2' \
		<<<"$$mutation_output"; \
	baseline_output=$$(import_result $(METAMATH_LANGDEF_DIR_V1) \
		tests/langdef/metamath/positive_ordered_frame_gap.mm); \
	rg -q 'LangDef:Imported MetamathV1 "[0-9a-f]{64}" 7' \
		<<<"$$baseline_output"; \
	sed '/(rule mm-state-axiom-order-frame-hypotheses/,/      (body))/s/state-source-match-index-v1/(state-literal-v1 mm-label-floating)/' \
		"$$axiom_frame_dir/state_v1.metta" \
		>"$$axiom_frame_dir/state_v1.mutated"; \
	mv "$$axiom_frame_dir/state_v1.mutated" \
		"$$axiom_frame_dir/state_v1.metta"; \
	rg -q -U 'mm-state-axiom-order-frame-hypotheses(.|\n)*state-literal-v1 mm-label-floating' \
		"$$axiom_frame_dir/state_v1.metta"; \
	build_mutation "$$axiom_frame_dir"; \
	require_changed_artifacts "$$axiom_frame_dir"; \
	mutation_output=$$(import_result "$$axiom_frame_dir" \
		tests/langdef/metamath/positive_ordered_frame_gap.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	printf '%s\n' '(MetamathCoGSLTStateMutationV1Summary 17 17 0)'

.PHONY: test-metamath-cogslt-proof-mutations-v1
test-metamath-cogslt-proof-mutations-v1: test-metamath-cogslt-langdef-v1 \
		$(BIN) $(GSLT_RULE_MUTATOR_V1_BIN) \
		$(METAMATH_COMPILED_CURSOR_V1) $(METAMATH_LANGDEF_LOCK_V1)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	check_dir=$$(mktemp -d langdef/metamath-proof-check.XXXXXX); \
	decoder_dir=$$(mktemp -d langdef/metamath-proof-decoder.XXXXXX); \
	policy_dir=$$(mktemp -d langdef/metamath-proof-policy.XXXXXX); \
	kind_dir=$$(mktemp -d langdef/metamath-proof-kind.XXXXXX); \
	disjoint_dir=$$(mktemp -d langdef/metamath-proof-disjoint.XXXXXX); \
	compressed_disjoint_dir=$$(mktemp -d \
		langdef/metamath-proof-compressed-disjoint.XXXXXX); \
	hypothesis_dir=$$(mktemp -d langdef/metamath-proof-hypothesis.XXXXXX); \
	trap 'rm -rf "$$check_dir" "$$decoder_dir" "$$policy_dir" "$$kind_dir" "$$disjoint_dir" "$$compressed_disjoint_dir" "$$hypothesis_dir"' \
		EXIT INT TERM; \
	build_mutation() { \
		$(MAKE) --no-print-directory BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
			GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
			METAMATH_LANGDEF_DIR_V1="$$1" \
			METAMATH_FOLD_PART_DIR_V1="$$1/fold-parts" \
			METAMATH_STATE_PART_DIR_V1="$$1/state-parts" \
			generate-metamath-cogslt-langdef-v1 >/dev/null; \
	}; \
	require_changed_artifacts() { \
		if cmp -s $(METAMATH_STATE_PROGRAM_V1) \
			"$$1/generated/state_program_v1.answers"; then \
			exit 1; \
		fi; \
		if cmp -s $(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
			"$$1/generated/syntax_cursor_fold_v1.generated.c"; then \
			exit 1; \
		fi; \
	}; \
	import_result() { \
		$(CETTA_BIN_INVOKE) --lang prime \
			-e '!(import! &self langdef)' \
			-e "!(bind! &mutation-langdef (langdef:load \"$$1/langdef.metta\"))" \
			-e "!(println! (langdef:import-file &mutation-langdef \"$$2\"))"; \
	}; \
	for mutation_dir in "$$check_dir" "$$decoder_dir" "$$policy_dir" "$$kind_dir" \
		"$$disjoint_dir" "$$compressed_disjoint_dir" "$$hypothesis_dir"; do \
		cp -R $(METAMATH_LANGDEF_DIR_V1)/. "$$mutation_dir/"; \
	done; \
	sed '/    (rule mm-proof-normal-check/,/      (body))/s/mm-label mm-symbol mm-proof-step/mm-label mm-proof-step mm-proof-step/' \
		"$$check_dir/proof_v1.metta" \
		>"$$check_dir/proof_v1.mutated"; \
	mv "$$check_dir/proof_v1.mutated" "$$check_dir/proof_v1.metta"; \
	rg -q -U 'mm-proof-normal-check(.|\n)*mm-label mm-proof-step mm-proof-step' \
		"$$check_dir/proof_v1.metta"; \
	if build_mutation "$$check_dir" 2>/dev/null; then exit 1; fi; \
	sed '/    (rule mm-proof-machine/,/      (body))/s/20 0 5 1/20 1 5 1/' \
		"$$decoder_dir/proof_v1.metta" \
		>"$$decoder_dir/proof_v1.mutated"; \
	mv "$$decoder_dir/proof_v1.mutated" "$$decoder_dir/proof_v1.metta"; \
	rg -q '20 1 5 1' "$$decoder_dir/proof_v1.metta"; \
	build_mutation "$$decoder_dir"; \
	require_changed_artifacts "$$decoder_dir"; \
	mutation_output=$$(import_result "$$decoder_dir" \
		tests/support/metamath_mini_thm_compressed_v0.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	sed 's/state-proof-unknown-push-claim-v1/state-proof-unknown-reject-v1/g' \
		"$$policy_dir/proof_v1.metta" \
		>"$$policy_dir/proof_v1.mutated"; \
	mv "$$policy_dir/proof_v1.mutated" "$$policy_dir/proof_v1.metta"; \
	if rg -q 'state-proof-unknown-push-claim-v1' \
		"$$policy_dir/proof_v1.metta"; then exit 1; fi; \
	[[ $$(rg -o 'state-proof-unknown-reject-v1' \
		"$$policy_dir/proof_v1.metta" | wc -l) -eq 2 ]]; \
	build_mutation "$$policy_dir"; \
	require_changed_artifacts "$$policy_dir"; \
	mutation_output=$$(import_result "$$policy_dir" \
		tests/langdef/metamath/positive_theorem_normal_unknown.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	sed '/    (rule mm-proof-machine/,/      (body))/s/mm-label-axiom/mm-symbol-constant/' \
		"$$kind_dir/proof_v1.metta" \
		>"$$kind_dir/proof_v1.mutated"; \
	mv "$$kind_dir/proof_v1.mutated" "$$kind_dir/proof_v1.metta"; \
	rg -q -U 'mm-proof-machine(.|\n)*mm-symbol-constant' \
		"$$kind_dir/proof_v1.metta"; \
	build_mutation "$$kind_dir"; \
	require_changed_artifacts "$$kind_dir"; \
	mutation_output=$$(import_result "$$kind_dir" \
		tests/support/metamath_mini_thm_v0.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' <<<"$$mutation_output"; \
	sed '/    (rule mm-proof-normal-snapshot-disjoint/,/      (body))/s/mm-state-disjoint-variable/mm-state-active-hypothesis/' \
		"$$disjoint_dir/proof_v1.metta" \
		>"$$disjoint_dir/proof_v1.mutated"; \
	mv "$$disjoint_dir/proof_v1.mutated" "$$disjoint_dir/proof_v1.metta"; \
	rg -q -U 'mm-proof-normal-snapshot-disjoint(.|\n)*mm-state-active-hypothesis' \
		"$$disjoint_dir/proof_v1.metta"; \
	build_mutation "$$disjoint_dir"; \
	require_changed_artifacts "$$disjoint_dir"; \
	mutation_output=$$(import_result "$$disjoint_dir" \
		tests/langdef/metamath/positive_theorem_disjoint_substitution.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' \
		<<<"$$mutation_output"; \
	$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
		--source "$$compressed_disjoint_dir/proof_v1.metta" \
		--out "$$compressed_disjoint_dir/proof_v1.mutated" \
		--rule mm-proof-compressed-snapshot-disjoint --mode falsify; \
	mv "$$compressed_disjoint_dir/proof_v1.mutated" \
		"$$compressed_disjoint_dir/proof_v1.metta"; \
	rg -q -U 'mm-proof-compressed-snapshot-disjoint(.|\n)*state-noop-v1' \
		"$$compressed_disjoint_dir/proof_v1.metta"; \
	build_mutation "$$compressed_disjoint_dir"; \
	require_changed_artifacts "$$compressed_disjoint_dir"; \
	mutation_output=$$(import_result "$$compressed_disjoint_dir" \
		tests/langdef/metamath/positive_theorem_compressed_disjoint_substitution.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' \
		<<<"$$mutation_output"; \
	sed '/    (rule mm-proof-normal-snapshot-hypotheses/,/      (body))/s/mm-state-active-hypothesis/mm-state-disjoint-variable/' \
		"$$hypothesis_dir/proof_v1.metta" \
		>"$$hypothesis_dir/proof_v1.mutated"; \
	mv "$$hypothesis_dir/proof_v1.mutated" \
		"$$hypothesis_dir/proof_v1.metta"; \
	rg -q -U 'mm-proof-normal-snapshot-hypotheses(.|\n)*mm-state-disjoint-variable' \
		"$$hypothesis_dir/proof_v1.metta"; \
	build_mutation "$$hypothesis_dir"; \
	require_changed_artifacts "$$hypothesis_dir"; \
	mutation_output=$$(import_result "$$hypothesis_dir" \
		tests/langdef/metamath/positive_theorem_reused_essential.mm); \
	rg -q 'LangDef:StageRejected MetamathV1' \
		<<<"$$mutation_output"; \
	printf '%s\n' '(MetamathCoGSLTProofMutationV1Summary 7 7 0)'

.PHONY: test-metamath-cogslt-set-mm-v1 \
	test-metamath-cogslt-set-mm-core-v1
test-metamath-cogslt-set-mm-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		METAMATH_SET_MM_V1="$(METAMATH_SET_MM_V1)" \
		test-metamath-cogslt-set-mm-core-v1

test-metamath-cogslt-set-mm-core-v1: test-metamath-cogslt-langdef-v1
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'the Metamath set.mm gate requires the C-only core build'; \
		exit 1; \
	fi
	@if ldd $(BIN) $(LANGDEF_COMPILER_V1_BIN) | rg -qi 'python|libswipl'; \
		then exit 1; fi
	@set -euo pipefail; \
	corpus='$(METAMATH_SET_MM_V1)'; \
	if [[ -z "$$corpus" ]]; then \
		echo 'set METAMATH_SET_MM_V1 to the canonical set.mm corpus'; \
		exit 1; \
	fi; \
	if [[ ! -f "$$corpus" ]]; then \
		echo "Metamath corpus is not a regular file: $$corpus"; \
		exit 1; \
	fi; \
	actual_digest=$$(sha256sum "$$corpus" | cut -d' ' -f1); \
	if [[ "$$actual_digest" != "$(METAMATH_SET_MM_SHA256_V1)" ]]; then \
		echo "Metamath corpus digest mismatch: $$actual_digest"; \
		exit 1; \
	fi; \
	import_output=$$($(CETTA_BIN_INVOKE) --lang prime \
		-e '!(import! &self langdef)' \
		-e '!(bind! &metamath-langdef (langdef:load "$(METAMATH_LANGDEF_MANIFEST_V1)"))' \
		-e "!(println! (langdef:import-file &metamath-langdef \"$$corpus\"))"); \
	if printf '%s\n' "$$import_output" | rg -q '\(Error '; then \
		printf '%s\n' "$$import_output" >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$import_output" | rg -q \
		'^\(LangDef:Imported MetamathV1 "[0-9a-f]{64}" $(METAMATH_SET_MM_IMPORT_COUNT_V1)\)$$'; \
	cli_output=$$($(CETTA_BIN_INVOKE) --lang metamath "$$corpus"); \
	printf '%s\n' "$$cli_output" | rg -q \
		'^\(LangDef:RunAccepted MetamathV1 "[0-9a-f]{64}" $(METAMATH_SET_MM_IMPORT_COUNT_V1)\)$$'; \
	printf '%s\n' \
		'(MetamathCoGSLTCliSetMmV1Summary 1 1 0 "$(METAMATH_SET_MM_SHA256_V1)" $(METAMATH_SET_MM_IMPORT_COUNT_V1))'; \
	printf '%s\n' \
		'(MetamathCoGSLTSetMmV1Summary 1 1 0 "$(METAMATH_SET_MM_SHA256_V1)" $(METAMATH_SET_MM_IMPORT_COUNT_V1))'

.PHONY: test-metamath-cogslt-corpus-differential-v1
test-metamath-cogslt-corpus-differential-v1: \
		test-metamath-cogslt-langdef-v1 \
		$(LANGDEF_IMPORT_VERDICT_DRIVER_V1)
	@if [[ -z "$(strip $(METAMATH_TEST_ROOT_V1))" ]]; then \
		echo 'set METAMATH_TEST_ROOT_V1 to the conformance corpus checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(METAMATH_MMLEAN4_ROOT_V1))" || \
		-z "$(strip $(METAMATH_MMLEAN4_BIN_V1))" ]]; then \
		echo 'set METAMATH_MMLEAN4_ROOT_V1 and METAMATH_MMLEAN4_BIN_V1'; \
		exit 1; \
	fi
	@if [[ "$$(git -C "$(METAMATH_MMLEAN4_ROOT_V1)" rev-parse HEAD)" != \
		"$(METAMATH_MMLEAN4_COMMIT_V1)" ]]; then \
		echo 'mm-lean4 source revision does not match the pinned oracle'; \
		exit 1; \
	fi
	@if ! git -C "$(METAMATH_MMLEAN4_ROOT_V1)" diff --quiet || \
		! git -C "$(METAMATH_MMLEAN4_ROOT_V1)" diff --cached --quiet; then \
		echo 'mm-lean4 tracked source is dirty'; \
		exit 1; \
	fi
	@sh -n $(LANGDEF_IMPORT_VERDICT_DRIVER_V1)
	@shellcheck $(LANGDEF_IMPORT_VERDICT_DRIVER_V1)
	@set -uo pipefail; \
	suite="$(METAMATH_TEST_ROOT_V1)/run-testsuite-all"; \
	tests_root="$(METAMATH_TEST_ROOT_V1)/tests"; \
	runtime_root="$(CURDIR)"; \
	runtime_bin="$$(realpath $(BIN))"; \
	manifest="$$(realpath $(METAMATH_LANGDEF_MANIFEST_V1))"; \
	driver="$$(realpath $(LANGDEF_IMPORT_VERDICT_DRIVER_V1))"; \
	mmlean="$$(realpath $(METAMATH_MMLEAN4_BIN_V1))"; \
	if [[ ! -f "$$suite" || ! -d "$$tests_root" || \
		! -x "$$runtime_bin" || ! -f "$$manifest" || \
		! -x "$$driver" || ! -x "$$mmlean" ]]; then \
		echo 'corpus differential resource is missing or invalid'; \
		exit 1; \
	fi; \
	total=0; ours_checked=0; oracle_checked=0; agreement=0; \
	failures=0; harness_failures=0; \
	while IFS=$$'\t' read -r suite_expected relative; do \
		total=$$((total + 1)); \
		file="$$tests_root/$$relative"; \
		if [[ ! -f "$$file" ]]; then \
			printf 'missing corpus database: %s\n' "$$relative" >&2; \
			failures=$$((failures + 1)); \
			continue; \
		fi; \
		if CETTA_LANGDEF_ROOT="$$runtime_root" \
			CETTA_LANGDEF_BIN="$$runtime_bin" \
			CETTA_LANGDEF_MANIFEST="$$manifest" \
			"$$driver" "$$file" >/dev/null 2>&1; then \
			ours=pass; \
		else \
			status=$$?; \
			case "$$status" in \
				1) ours=fail ;; \
				*) ours=harness ;; \
			esac; \
		fi; \
		directory=$$(dirname "$$file"); \
		basename=$$(basename "$$file"); \
		if (cd "$$directory" && \
			timeout 300 "$$mmlean" "$$basename" >/dev/null 2>&1); then \
			oracle=pass; \
		else \
			status=$$?; \
			case "$$status" in \
				1) oracle=fail ;; \
				*) oracle=harness ;; \
			esac; \
		fi; \
		if [[ "$$ours" == harness || "$$oracle" == harness ]]; then \
			printf 'harness failure: %s (generated=%s oracle=%s)\n' \
				"$$relative" "$$ours" "$$oracle" >&2; \
			harness_failures=$$((harness_failures + 1)); \
			continue; \
		fi; \
		if [[ "$$ours" == "$$suite_expected" ]]; then \
			ours_checked=$$((ours_checked + 1)); \
		else \
			printf 'spec mismatch: %s (expected=%s generated=%s)\n' \
				"$$relative" "$$suite_expected" "$$ours" >&2; \
			failures=$$((failures + 1)); \
		fi; \
		if [[ "$$oracle" == "$$suite_expected" ]]; then \
			oracle_checked=$$((oracle_checked + 1)); \
		else \
			printf 'oracle/corpus mismatch: %s (expected=%s oracle=%s)\n' \
				"$$relative" "$$suite_expected" "$$oracle" >&2; \
			failures=$$((failures + 1)); \
		fi; \
		if [[ "$$ours" == "$$oracle" ]]; then \
			agreement=$$((agreement + 1)); \
		else \
			printf 'unexplained differential: %s (generated=%s oracle=%s)\n' \
				"$$relative" "$$ours" "$$oracle" >&2; \
			failures=$$((failures + 1)); \
		fi; \
	done < <(awk '$$1 == "pass" || $$1 == "fail" { \
		gsub(/\\/, "", $$2); print $$1 "\t" $$2 \
	}' "$$suite"); \
	if [[ "$$total" -ne 177 || "$$ours_checked" -ne 177 || \
		"$$oracle_checked" -ne 177 || "$$agreement" -ne 177 || \
		"$$failures" -ne 0 || \
		"$$harness_failures" -ne 0 ]]; then \
		exit 1; \
	fi; \
	printf '%s\n' \
		'(MetamathCoGSLTCorpusDifferentialV1Summary 177 177 177 177 0 0 "$(METAMATH_MMLEAN4_COMMIT_V1)")'

.PHONY: test-metamath-cogslt-generated-relational-corpus-v1
test-metamath-cogslt-generated-relational-corpus-v1: \
		test-metamath-cogslt-langdef-v1 \
		$(LANGDEF_PROOF_VERDICT_DRIVER_V1)
	@if [[ -z "$(strip $(METAMATH_TEST_ROOT_V1))" ]]; then \
		echo 'set METAMATH_TEST_ROOT_V1 to the conformance corpus checkout'; \
		exit 1; \
	fi
	@sh -n $(LANGDEF_PROOF_VERDICT_DRIVER_V1)
	@shellcheck $(LANGDEF_PROOF_VERDICT_DRIVER_V1)
	@set -uo pipefail; \
	suite="$(METAMATH_TEST_ROOT_V1)/run-testsuite-all"; \
	tests_root="$(METAMATH_TEST_ROOT_V1)/tests"; \
	registry_root="$$(realpath langdef)"; \
	runtime_bin="$$(realpath $(BIN))"; \
	manifest="$$(realpath $(METAMATH_LANGDEF_MANIFEST_V1))"; \
	driver="$$(realpath $(LANGDEF_PROOF_VERDICT_DRIVER_V1))"; \
	if [[ ! -f "$$suite" || ! -d "$$tests_root" || \
		! -d "$$registry_root" || ! -x "$$runtime_bin" || \
		! -f "$$manifest" || ! -x "$$driver" ]]; then \
		echo 'generated relational corpus resource is missing or invalid'; \
		exit 1; \
	fi; \
	total=0; checked=0; failures=0; harness_failures=0; \
	while IFS=$$'\t' read -r suite_expected relative; do \
		total=$$((total + 1)); \
		file="$$tests_root/$$relative"; \
		if [[ ! -f "$$file" ]]; then \
			printf 'missing corpus database: %s\n' "$$relative" >&2; \
			failures=$$((failures + 1)); \
			continue; \
		fi; \
		if timeout 300 env \
			CETTA_LANGDEF_ROOT="$$registry_root" \
			CETTA_LANGDEF_BIN="$$runtime_bin" \
			CETTA_LANGDEF_MANIFEST="$$manifest" \
			CETTA_LANGDEF_PROOF_BACKEND=generated-relational-audit-v1 \
			"$$driver" "$$file" >/dev/null 2>&1; then \
			ours=pass; \
		else \
			status=$$?; \
			case "$$status" in \
				1) ours=fail ;; \
				*) ours=harness ;; \
			esac; \
		fi; \
		if [[ "$$ours" == harness ]]; then \
			printf 'harness failure: %s\n' "$$relative" >&2; \
			harness_failures=$$((harness_failures + 1)); \
			continue; \
		fi; \
		if [[ "$$ours" == "$$suite_expected" ]]; then \
			checked=$$((checked + 1)); \
		else \
			printf 'spec mismatch: %s (expected=%s generated=%s)\n' \
				"$$relative" "$$suite_expected" "$$ours" >&2; \
			failures=$$((failures + 1)); \
		fi; \
	done < <(awk '$$1 == "pass" || $$1 == "fail" { \
		gsub(/\\/, "", $$2); print $$1 "\t" $$2 \
	}' "$$suite"); \
	if [[ "$$total" -ne 177 || "$$checked" -ne 177 || \
		"$$failures" -ne 0 || "$$harness_failures" -ne 0 ]]; then \
		exit 1; \
	fi; \
	printf '%s\n' \
		'(MetamathCoGSLTGeneratedRelationalCorpusV1Summary 177 177 0 0)'

$(TPTP_LANGDEF_ATP_PROGRAM_V1): $(TPTP_LANGDEF_ATP_BRIDGE_V1) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) equations \
		--source $(TPTP_LANGDEF_ATP_BRIDGE_V1) \
		--out $@

$(TPTP_LANGDEF_PARSER_PACK_V1) $(TPTP_LANGDEF_LOCK_V1) &: \
		$(TPTP_LANGDEF_MANIFEST_V1) \
		$(TPTP_LANGDEF_PARSER_SOURCES_V1) \
		$(TPTP_LANGDEF_ATP_PROGRAM_V1) \
		$(LANGDEF_COMPILER_V1_BIN) \
		$(GSLT2PARSE_PARSER_PACK_COMPILER_SOURCES)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	$(LANGDEF_COMPILER_V1_BIN) parser-pack \
		--manifest $(TPTP_LANGDEF_MANIFEST_V1) \
		--compiler-root "$(GSLT2PARSE_COMPILER_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations"

.PHONY: generate-tptp-langdef-v1
generate-tptp-langdef-v1: $(TPTP_LANGDEF_ATP_PROGRAM_V1) \
		$(TPTP_LANGDEF_PARSER_PACK_V1) $(TPTP_LANGDEF_LOCK_V1)

.PHONY: test-tptp-langdef-regeneration-v1
test-tptp-langdef-regeneration-v1: $(LANGDEF_COMPILER_V1_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@program_check=$$(mktemp runtime/tptp-langdef-program.XXXXXX); \
	pack_check=$$(mktemp runtime/tptp-langdef-pack.XXXXXX); \
	lock_check=$$(mktemp runtime/tptp-langdef-lock.XXXXXX); \
	trap 'rm -f "$$program_check" "$$pack_check" "$$lock_check"' \
		EXIT INT TERM; \
	$(LANGDEF_COMPILER_V1_BIN) equations \
		--source $(TPTP_LANGDEF_ATP_BRIDGE_V1) \
		--out "$$program_check"; \
	$(LANGDEF_COMPILER_V1_BIN) parser-pack \
		--manifest $(TPTP_LANGDEF_MANIFEST_V1) \
			--compiler-root "$(GSLT2PARSE_COMPILER_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations" \
		--pack-out "$$pack_check" --lock-out "$$lock_check"; \
	cmp -s "$$program_check" $(TPTP_LANGDEF_ATP_PROGRAM_V1); \
	cmp -s "$$pack_check" $(TPTP_LANGDEF_PARSER_PACK_V1); \
	cmp -s "$$lock_check" $(TPTP_LANGDEF_LOCK_V1); \
	if ldd $(LANGDEF_COMPILER_V1_BIN) | rg -qi 'python|libswipl'; then \
		echo 'langdef compiler unexpectedly links Python or SWI-Prolog'; \
		exit 1; \
	fi; \
	if $(MAKE) -B -n BUILD=$(BUILD) ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		generate-tptp-langdef-v1 | rg -q \
		'(^|[[:space:]/])python(3)?([[:space:]]|$$)|[[:alnum:]_/.-]+\.py([[:space:]]|$$)'; then \
		echo 'TPTP regeneration has a Python build dependency'; \
		exit 1; \
	fi; \
	echo '(TPTPLangDefRegenerationV1Summary 5 5 0)'

.PHONY: test-tptp-langdef-v1
test-tptp-langdef-v1: $(BIN) test-tptp-langdef-digest-closure-v1 \
		test-gslt2parse-generic-engine-purity-v1 \
		test-gslt2parse-schema-v1-native-tptp
	@$(CETTA_BIN_INVOKE) --fuel 1000000 --lang prime \
		tests/langdef/tptp/langdef_atp.metta >/dev/null
	@if rg -ni \
		'tptp|fof|cnf|metamath|megalodon|\\.mm([^a-z]|$$)|\\.p([^a-z]|$$)' \
		native/langdef_module.c native/langdef_module.h \
		native/langdef_metta_equation_compiler_v1.c \
		native/langdef_metta_equation_compiler_v1.h \
		tools/langdef_compile_v1.c lib/langdef.metta; then \
		echo 'language vocabulary leaked into generic langdef machinery'; \
		exit 1; \
	fi
	@if ldd ./cetta | rg -qi 'python|libswipl'; then \
		echo 'TPTP runtime unexpectedly links Python or SWI-Prolog'; \
		exit 1; \
	fi
	@if strings ./cetta | rg -qi \
		'TptpFofCnfV1|TPTP[.:]|metamath|megalodon|ground-cnf-subset|cnf-disjunction'; then \
		echo 'language vocabulary leaked into the generic runtime binary'; \
		exit 1; \
	fi
	@echo '(TPTPLangDefV1Summary 7 7 0)'

.PHONY: test-tptp-langdef-digest-closure-v1
test-tptp-langdef-digest-closure-v1: $(LANGDEF_COMPILER_V1_BIN)
	@mutation_dir=$$(mktemp -d runtime/tptp-langdef-mutation.XXXXXX); \
	diagnostic=$$mutation_dir/diagnostic; \
	trap 'rm -rf "$$mutation_dir"' EXIT INT TERM; \
	cp -R langdef/tptp/. "$$mutation_dir/"; \
	$(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$mutation_dir/langdef.metta"; \
	printf '\n' >>"$$mutation_dir/langdef.metta"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$mutation_dir/langdef.metta" \
		>/dev/null 2>"$$diagnostic"; then exit 1; fi; \
	rg -q 'artifact hash does not match' "$$diagnostic"; \
	cp langdef/tptp/langdef.metta "$$mutation_dir/langdef.metta"; \
	printf '\n' >>"$$mutation_dir/syntax_fof_cnf_v1.metta"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$mutation_dir/langdef.metta" \
		>/dev/null 2>"$$diagnostic"; then exit 1; fi; \
	rg -q 'source hash does not match' "$$diagnostic"; \
	cp langdef/tptp/syntax_fof_cnf_v1.metta \
		"$$mutation_dir/syntax_fof_cnf_v1.metta"; \
	printf '\n' >>"$$mutation_dir/generated/atp_bridge_v1.generated.metta"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$mutation_dir/langdef.metta" \
		>/dev/null 2>"$$diagnostic"; then exit 1; fi; \
	rg -q 'program hash does not match' "$$diagnostic"; \
	cp langdef/tptp/generated/atp_bridge_v1.generated.metta \
		"$$mutation_dir/generated/atp_bridge_v1.generated.metta"; \
	printf 'X' >>"$$mutation_dir/generated/parser_pack_v1.abi"; \
	if $(LANGDEF_COMPILER_V1_BIN) validate \
		--manifest "$$mutation_dir/langdef.metta" \
		>/dev/null 2>"$$diagnostic"; then exit 1; fi; \
	rg -q 'artifact hash does not match' "$$diagnostic"; \
	echo '(TPTPLangDefDigestClosureV1Summary 5 5 0)'

.PHONY: test-tptp-langdef-load-bearing-mutations-v1
test-tptp-langdef-load-bearing-mutations-v1: $(BIN) \
		$(LANGDEF_COMPILER_V1_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_COMPILER_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_COMPILER_ROOT to the parser-pack compiler source directory'; \
		exit 1; \
	fi
	@fold_dir=$$(mktemp -d runtime/tptp-langdef-fold-mutation.XXXXXX); \
	syntax_dir=$$(mktemp -d runtime/tptp-langdef-syntax-mutation.XXXXXX); \
	trap 'rm -rf "$$fold_dir" "$$syntax_dir"' EXIT INT TERM; \
	cp -R langdef/tptp/. "$$fold_dir/"; \
	sed '/(rule eq-ground-role-axiom/,/(body))/s/          True/          False/' \
		"$$fold_dir/atp_bridge_v1.metta" \
		>"$$fold_dir/atp_bridge_v1.mutated"; \
	mv "$$fold_dir/atp_bridge_v1.mutated" \
		"$$fold_dir/atp_bridge_v1.metta"; \
	rg -q -U 'eq-ground-role-axiom(.|\n)*False' \
		"$$fold_dir/atp_bridge_v1.metta"; \
	$(LANGDEF_COMPILER_V1_BIN) equations \
		--source "$$fold_dir/atp_bridge_v1.metta" \
		--out "$$fold_dir/generated/atp_bridge_v1.generated.metta"; \
	$(LANGDEF_COMPILER_V1_BIN) parser-pack \
		--manifest "$$fold_dir/langdef.metta" \
			--compiler-root "$(GSLT2PARSE_COMPILER_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations"; \
	$(CETTA_BIN_INVOKE) --fuel 1000000 --lang prime \
		-e '!(import! &self langdef)' \
		-e '(= (mutation-fold-killed (LangDef.ImportRejected TptpFofCnfV1 $$pack (TPTP.Unsupported ground-cnf-subset))) True)' \
		-e "!(bind! &mutation-langdef (langdef:load \"$$fold_dir/langdef.metta\"))" \
		-e '!(bind! &mutation-result (langdef:import-file &mutation-langdef "tests/langdef/tptp/ground_refutation.p"))' \
		-e '!(assertEqual (mutation-fold-killed &mutation-result) True)' \
		>/dev/null; \
	cp -R langdef/tptp/. "$$syntax_dir/"; \
	sed '/(rule def-k-cnf /s/(cp 99)/(cp 120)/' \
		"$$syntax_dir/syntax_fof_cnf_v1.metta" \
		>"$$syntax_dir/syntax_fof_cnf_v1.mutated"; \
	mv "$$syntax_dir/syntax_fof_cnf_v1.mutated" \
		"$$syntax_dir/syntax_fof_cnf_v1.metta"; \
	rg -q 'def-k-cnf.*(cp 120)' \
		"$$syntax_dir/syntax_fof_cnf_v1.metta"; \
	$(LANGDEF_COMPILER_V1_BIN) parser-pack \
		--manifest "$$syntax_dir/langdef.metta" \
			--compiler-root "$(GSLT2PARSE_COMPILER_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations"; \
	$(CETTA_BIN_INVOKE) --fuel 1000000 --lang prime \
		-e '!(import! &self langdef)' \
		-e '(= (mutation-syntax-killed (LangDef.ParseRejected TptpFofCnfV1 $$pack $$byte $$expected)) True)' \
		-e "!(bind! &mutation-langdef (langdef:load \"$$syntax_dir/langdef.metta\"))" \
		-e '!(bind! &mutation-result (langdef:import-file &mutation-langdef "tests/langdef/tptp/ground_refutation.p"))' \
		-e '!(assertEqual (mutation-syntax-killed &mutation-result) True)' \
		>/dev/null; \
	echo '(TPTPLangDefLoadBearingMutationsV1Summary 2 2 0)'

%.$(BUILD_OBJ_TAG).o: %.c $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

%.$(BUILD_OBJ_TAG).runtime-stats.o: %.c $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

clean:
	rm -f $(OBJ) $(STAGE0_OBJ) $(DEPS) $(BIN) $(STAGE0_BIN) cetta-stage0 \
		runtime/cetta-*-runtime-stats runtime/cetta-stage0-* \
		runtime/test_fallback_eval_session-* runtime/bootstrap/test_fallback_eval_session.*.o \
		runtime/bootstrap/test_fallback_eval_session.*.d \
		runtime/test_prime_delayed_ambiguity-* runtime/bootstrap/test_prime_delayed_ambiguity.*.o \
		runtime/bootstrap/test_prime_delayed_ambiguity.*.d \
		runtime/test_prime_package_validation-* runtime/bootstrap/test_prime_package_validation.*.o \
		runtime/bootstrap/test_prime_package_validation.*.d \
		runtime/test_rhometta_payload_map_capacity-* runtime/bootstrap/test_rhometta_payload_map_capacity.*.o \
		runtime/bootstrap/test_rhometta_payload_map_capacity.*.d \
			src/*.runtime-stats.o src/*.runtime-stats.d \
			src/*.nogmp.o src/*.nogmp.d src/*.nogmp.stage0.o src/*.nogmp.stage0.d src/*.nogmp.runtime-stats.o src/*.nogmp.runtime-stats.d \
			native/*.runtime-stats.o native/*.runtime-stats.d \
			native/*.nogmp.o native/*.nogmp.d native/*.nogmp.stage0.o native/*.nogmp.stage0.d native/*.nogmp.runtime-stats.o native/*.nogmp.runtime-stats.d \
		$(STDLIB_BLOB) $(ABT_DEFAULT_SIGNATURES_BLOB) runtime/bootstrap/mork-bridge.*.stamp \
		runtime/bootstrap/libcetta_space_bridge.*.a \
		$(BUILD_CONFIG_HEADER) $(STAGE0_BUILD_CONFIG_HEADER) runtime/bootstrap/build_config.h runtime/bootstrap/build_config.*.h runtime/bootstrap/build_config.stage0.h runtime/bootstrap/build_config.stage0.*.h \
		runtime/bootstrap/build_config*.stamp \
		$(BUILD_CONFIG_STAMP) $(STAGE0_BUILD_CONFIG_STAMP) $(STDLIB_BLOB_STAMP) $(ABT_DEFAULT_SIGNATURES_BLOB_STAMP) \
		src/foreign.o src/foreign.d src/foreign.stage0.o src/foreign.stage0.d \
		src/foreign_stub.o src/foreign_stub.d src/foreign_stub.stage0.o src/foreign_stub.stage0.d
	rm -rf $(BOOTSTRAP_TMPDIR)/bridge-workspace.*

.PHONY: test-clean-rebuild
test-clean-rebuild:
	@$(MAKE) clean
	@$(MAKE) -s -j2
	@$(MAKE) -s test-petta-typecheck-v2-isolation-stats

promote-runtime: $(BIN)
	@BUILD=$(BUILD_CANON) \
	PATHMAP_REPO_DIR="$(PATHMAP_REPO_DIR)" \
	MORK_REPO_DIR="$(MORK_REPO_DIR)" \
	./scripts/promote_runtime.sh

bridge-setup: $(MORK_BRIDGE_WORKSPACE_MANIFEST)
	@if [ "$(MORK_BRIDGE_DEPS_READY)" != "1" ]; then \
		echo "FAIL: missing bridge manifests"; \
		printf '  %s\n' $(MORK_BRIDGE_MISSING_MANIFESTS); \
		exit 1; \
	fi
	@echo "PASS: bridge workspace manifest ready"
	@echo "  manifest: $(MORK_BRIDGE_WORKSPACE_MANIFEST)"
	@echo "  PathMap:  $(PATHMAP_DEP_DIR)"
	@echo "  MORK:     $(MORK_REPO_DIR)"
	@echo "  Try: make BUILD=main"
	@echo "  Try: make BUILD=mork"
	@echo "  Note: BUILD=full and BUILD=pathmap remain compatibility aliases"

doctor-bridge:
	@echo "CeTTa repo: $(CETTA_REPO_DIR)"
	@echo "Rust dir:   $(CETTA_RUST_DIR)"
	@echo "PathMap:    $(PATHMAP_DEP_DIR)"
	@echo "MORK:       $(MORK_REPO_DIR)"
	@echo "Manifest:   $(MORK_BRIDGE_MANIFEST)"
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
		echo "PASS: bridge manifest present"; \
	else \
		echo "FAIL: bridge manifest missing"; \
		exit 1; \
	fi
	@if [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		echo "PASS: external bridge manifests present"; \
		echo "INFO: generated bridge workspace will be $(MORK_BRIDGE_WORKSPACE_MANIFEST)"; \
		echo "INFO: recommended builds are BUILD=main and BUILD=mork"; \
		echo "INFO: BUILD=full and BUILD=pathmap remain compatibility aliases"; \
	else \
		echo "FAIL: missing bridge manifests"; \
		printf '  %s\n' $(MORK_BRIDGE_MISSING_MANIFESTS); \
		echo "INFO: set PATHMAP_REPO_DIR=/path/to/PathMap and MORK_REPO_DIR=/path/to/MORK"; \
		exit 1; \
	fi

doctor-gmp:
ifeq ($(ENABLE_GMP),0)
	@echo "PASS: GMP disabled by ENABLE_GMP=0"
else
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp="$(BOOTSTRAP_TMPDIR)/doctor-gmp"; \
	if printf '%s\n' '#include <gmp.h>' 'int main(void) { mpz_t z; mpz_init_set_ui(z, 1); mpz_clear(z); return 0; }' | \
		$(CC) $(GMP_CFLAGS) -x c - -o "$$tmp" $(GMP_LDFLAGS) >/dev/null 2>&1; then \
		echo "PASS: GMP compile/link probe"; \
		rm -f "$$tmp"; \
	else \
		echo "FAIL: GMP compile/link probe"; \
		echo "INFO: set GMP_CFLAGS and GMP_LDFLAGS if GMP is installed in a non-default prefix"; \
		rm -f "$$tmp"; \
		exit 1; \
	fi
endif

test-bigint-no-gmp-fallback:
	@$(MAKE) -s BUILD=core ENABLE_GMP=0 test-bigint-no-gmp-fallback-body

test-bigint-no-gmp-fallback-body: $(BIN)
	@set -e; \
	tmp="$(BOOTSTRAP_TMPDIR)/test-bigint-no-gmp-fallback.out"; \
	$(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/support/test_bigint_no_gmp_fallback.metta > "$$tmp"; \
	diff -u tests/support/test_bigint_no_gmp_fallback.expected "$$tmp"; \
	rm -f "$$tmp"; \
	echo "PASS: bigint no-GMP fallback is loud and parseable"

test-rational-no-gmp-fallback:
	@$(MAKE) -s BUILD=core ENABLE_GMP=0 test-rational-no-gmp-fallback-body

test-rational-no-gmp-fallback-body: $(BIN)
	@set -e; \
	tmp="$(BOOTSTRAP_TMPDIR)/test-rational-no-gmp-fallback.out"; \
	$(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/support/test_rational_no_gmp_fallback.metta > "$$tmp"; \
	diff -u tests/support/test_rational_no_gmp_fallback.expected "$$tmp"; \
	rm -f "$$tmp"; \
	echo "PASS: rational no-GMP fallback is loud and default-compatible"

.PHONY: test-bigint-no-gmp-fallback-body test-rational-no-gmp-fallback-body

# Fast test: compare CeTTa output against pre-computed .expected files.
# No oracle invocation — safe and instant.
prepare-git-test-fixture:
	@mkdir -p "$(GIT_TEST_FIXTURE_ROOT)" "$(GIT_TEST_CACHE_DIR)"
	@cp -R tests/support/git_module_seed/. "$(GIT_TEST_FIXTURE_ROOT)/"
	@if [ ! -d "$(GIT_TEST_FIXTURE_ROOT)/.git" ]; then \
		git -C "$(GIT_TEST_FIXTURE_ROOT)" init -q -b master >/dev/null; \
		git -C "$(GIT_TEST_FIXTURE_ROOT)" config user.email "cetta-tests@example.invalid"; \
		git -C "$(GIT_TEST_FIXTURE_ROOT)" config user.name "CeTTa Tests"; \
	fi
	@git -C "$(GIT_TEST_FIXTURE_ROOT)" add . >/dev/null
	@if ! git -C "$(GIT_TEST_FIXTURE_ROOT)" rev-parse --verify HEAD >/dev/null 2>&1 || \
	    ! git -C "$(GIT_TEST_FIXTURE_ROOT)" diff --cached --quiet; then \
		git -C "$(GIT_TEST_FIXTURE_ROOT)" commit -m "cetta git fixture" >/dev/null; \
	fi

test-git-module: $(BIN) prepare-git-test-fixture
	@pass=0; fail=0; \
	update_mod="update_$$(date +%s)"; \
	printf '%s\n' \
		'; Local git-module! fixture through the typed git-remote provider.' \
		'!(git-module! "$(GIT_TEST_URL)")' \
		'!(bind! &mods (module-inventory!))' \
		'!(assertEqualToResult (match &mods (module-provider git-remote enabled) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-provider-implementation git-remote implemented) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-provider-transport git-remote remote) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-provider-locator-kind git-remote git-url) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-provider-update-policy git-remote try-fetch-latest) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-provider-revision-policy git-remote default-branch-only) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-mount git_module_fixture $$path git-remote) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-mount-source git_module_fixture "$(GIT_TEST_URL)" git-url) ok) (ok))' \
		'!(assertEqualToResult (match &mods (module-mount-revision-policy git_module_fixture default-branch-only "none") ok) (ok))' \
		'!(import! &gitdb git_module_fixture)' \
		'!(assertEqualToResult (match &gitdb (git-root $$x) $$x) (loaded))' \
		> "$(GIT_TEST_DYNAMIC)"; \
	result=$$(CETTA_GIT_MODULE_CACHE_DIR="$(GIT_TEST_CACHE_DIR)" $(CETTA_BIN_INVOKE) --profile he-extended --lang he "$(GIT_TEST_DYNAMIC)" 2>&1); \
	expected=$$'[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]\n[()]'; \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: dynamic git-module! fixture"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: dynamic git-module! fixture"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	printf '%s\n%s\n' "; $$update_mod fetched via TryFetchLatest" "(git-update fetched)" > "$(GIT_TEST_FIXTURE_ROOT)/$$update_mod.metta"; \
	git -C "$(GIT_TEST_FIXTURE_ROOT)" add "$$update_mod.metta" >/dev/null; \
	if ! git -C "$(GIT_TEST_FIXTURE_ROOT)" diff --cached --quiet; then \
		git -C "$(GIT_TEST_FIXTURE_ROOT)" commit -m "cetta git fixture $$update_mod" >/dev/null; \
	fi; \
	printf '%s\n' \
		'; Re-running git-module! should soft-refresh an existing cache entry.' \
		'!(git-module! "$(GIT_TEST_URL)")' \
		'!(import! &gitupd git_module_fixture:'"$$update_mod"')' \
		'!(assertEqualToResult (match &gitupd (git-update $$x) $$x) (fetched))' \
		> "$(GIT_TEST_COMPAT_DYNAMIC)"; \
	result=$$(CETTA_GIT_MODULE_CACHE_DIR="$(GIT_TEST_CACHE_DIR)" $(CETTA_BIN_INVOKE) --profile he-extended --lang he "$(GIT_TEST_COMPAT_DYNAMIC)" 2>&1); \
	expected=$$'[()]\n[()]\n[()]'; \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: git-module! cache refresh"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: git-module! cache refresh"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-git-module-profiles: test-git-module $(BIN) prepare-git-test-fixture
	@pass=0; fail=0; \
	printf '%s\n' \
		'; he-compat should still expose the public HE git-module! surface.' \
		'!(git-module! "$(GIT_TEST_URL)")' \
		'!(import! &gitdb git_module_fixture)' \
		'!(assertEqualToResult (match &gitdb (git-root $$x) $$x) (loaded))' \
		> "$(GIT_TEST_COMPAT_DYNAMIC)"; \
	result=$$(CETTA_GIT_MODULE_CACHE_DIR="$(GIT_TEST_CACHE_DIR)" $(CETTA_BIN_INVOKE) --profile he-compat --lang he "$(GIT_TEST_COMPAT_DYNAMIC)" 2>&1); \
	expected=$$'[()]\n[()]\n[()]'; \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: he-compat git-module! surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat git-module! surface"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

# Keep the bridge lane's tiny MM2 fixtures local so it does not depend on
# sibling-repo example paths that may be reorganized upstream.
MORK_MM2_FIXTURE_DIR := $(abspath tests/support/mork_mm2)
MORK_MM2_TEST3 := $(MORK_MM2_FIXTURE_DIR)/test3_var_binding.mm2
MORK_MM2_VAR_SCOPE := $(MORK_MM2_FIXTURE_DIR)/test_var_scope_across_exprs.mm2
MORK_MM2_TEST4 := $(MORK_MM2_FIXTURE_DIR)/test4_conjunctive.mm2
MORK_MM2_TEST5 := $(MORK_MM2_FIXTURE_DIR)/test5_equal_pair.mm2
MORK_MM2_TEST6 := $(MORK_MM2_FIXTURE_DIR)/test6_no_match.mm2
MORK_MM2_TEST7 := $(MORK_MM2_FIXTURE_DIR)/test7_nested.mm2
MORK_MM2_TEST8 := $(MORK_MM2_FIXTURE_DIR)/test8_multi_step.mm2
MORK_MM2_TEST9 := $(MORK_MM2_FIXTURE_DIR)/test9_priority_ordering.mm2
MORK_MM2_TEST10 := $(MORK_MM2_FIXTURE_DIR)/test10_conjunctive_wq.mm2
MORK_MM2_SINK_ADD_CONSTANT := $(MORK_MM2_FIXTURE_DIR)/test_add_constant.mm2
MORK_MM2_SINK_ADD_SIMPLE := $(MORK_MM2_FIXTURE_DIR)/test_add_simple.mm2
MORK_MM2_SINK_REMOVE_SIMPLE := $(MORK_MM2_FIXTURE_DIR)/test_remove_simple.mm2
MORK_MM2_SINK_BULK_REMOVE := $(MORK_MM2_FIXTURE_DIR)/test_bulk_remove.mm2
MORK_MM2_SINK_COUNT_SIMPLE := $(MORK_MM2_FIXTURE_DIR)/test_count_simple.mm2
MORK_MM2_SINK_HEAD_LIMIT := $(MORK_MM2_FIXTURE_DIR)/test_head_limit.mm2

MORK_BRIDGE_ACTIVE := $(if $(filter 1,$(MORK_BUILD_HAS_BRIDGE)),1,$(if $(strip $(CETTA_MORK_SPACE_BRIDGE_LIB)),1,0))
BRIDGE_REEXEC_BUILD := $(if $(filter 1,$(ENABLE_PYTHON)),main,mork)

define reexec_mork_bridge_or_skip
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		echo "INFO: $(1) requires the MORK bridge; re-running with BUILD=$(BRIDGE_REEXEC_BUILD)"; \
		$(MAKE) BUILD=$(BRIDGE_REEXEC_BUILD) $(2); \
	elif [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
		echo "SKIP: $(1) (no MORK bridge manifest configured)"; \
	else \
		echo "SKIP: $(1) (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
	fi
endef

define reexec_pathmap_bridge_or_skip
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		echo "INFO: $(1) requires generic pathmap-backed spaces; re-running with BUILD=$(BRIDGE_REEXEC_BUILD)"; \
		$(MAKE) BUILD=$(BRIDGE_REEXEC_BUILD) $(2); \
	elif [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
		echo "SKIP: $(1) (no MORK bridge manifest configured)"; \
	else \
		echo "SKIP: $(1) (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
	fi
endef

test-list-lanes: $(BIN)
	@./scripts/check_list_lanes.py --cetta ./$(BIN) \
		--timeout "$${CETTA_LIST_LANES_TIMEOUT:-60}"

bench-list: $(BIN) test-list-lanes
	@./scripts/bench_list_lanes.py --cetta ./$(BIN)

test: $(BIN) test-manifest-strict test-git-module test-symbolid-guard test-variant-shape-roundtrip test-bindings-lookup-index test-atom-deep-copy-iterative test-abt test-rhometta-payload-map-capacity-c test-space-term-universe-membership test-help-flags test-rhocalc test-he-contract-suite test-he-return-contract-correlation test-closed-stream-fastpath test-parse-depth-guard test-stdlib-growth-memory-regression test-rhometta-macro-audit test-eval-gc-adversarial test-list-lanes test-syn-lanes test-lib-prolog test-petta-libpl test-petta-process-text test-match-decision test-petta-search-machine test-petta-semantics test-petta-corpus-manifest-unit test-petta-chainer-manifest-unit test-gslt-provider-generation-v1 test-gslt-provider-runtime test-prime-nik-core-v1 test-subzero test-mettazero test-gslt-il test-zerouv test-metta-interact test-mm2-gslt-profile-v1
	@pass=0; fail=0; skip=0; no_exp=0; \
	cache_dir="$(GIT_TEST_CACHE_DIR)"; mkdir -p "$$cache_dir"; export CETTA_GIT_MODULE_CACHE_DIR="$$cache_dir"; \
	for f in tests/test_*.metta tests/spec_*.metta tests/he_*.metta; do \
		[ -f "$$f" ] || continue; \
		if [ "$(ENABLE_PYTHON)" != "1" ] && \
		   { [ "$$f" = "tests/test_py_ops_surface.metta" ] || \
		     [ "$$f" = "tests/test_import_foreign_python_file.metta" ] || \
		     [ "$$f" = "tests/test_import_foreign_pkg_error.metta" ] || \
		     [ "$$f" = "tests/test_namespace_sugar_guardrails.metta" ]; }; then \
			echo "SKIP: $$f (requires a Python-enabled build)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
		if printf '%s\n' $(PATHMAP_REQUIRED_TESTS) | grep -Fxq "$$f"; then \
			echo "SKIP: $$f (covered by test-pathmap-lane)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
		if printf '%s\n' $(PATHMAP_PROBE_TESTS) | grep -Fxq "$$f"; then \
			echo "SKIP: $$f (covered by probe-pathmap-lane)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
		if printf '%s\n' $(CORE_PROBE_TESTS) | grep -Fxq "$$f"; then \
			echo "SKIP: $$f (covered by probe-core-lane)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
		if printf '%s\n' $(CORE_XFAIL_TESTS) | grep -Fxq "$$f"; then \
			result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
			if printf '%s\n' "$$result" | grep -Fq "(Error "; then \
				echo "XFAIL: $$f"; \
				skip=$$((skip + 1)); \
			else \
				echo "XPASS: $$f"; \
				printf '%s\n' "$$result" | head -20; \
				fail=$$((fail + 1)); \
			fi; \
			continue; \
		fi; \
		if printf '%s\n' $(BACKEND_HEAVY_TESTS) | grep -Fxq "$$f"; then \
			echo "SKIP: $$f (covered by test-heavy)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
		if printf '%s\n' $(BACKEND_DEDICATED_TESTS) | grep -Fxq "$$f"; then \
			continue; \
		fi; \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat $$exp)" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	summary="$$pass passed, $$fail failed"; \
	if [ $$skip -gt 0 ]; then summary="$$summary, $$skip skipped"; fi; \
	if [ $$no_exp -gt 0 ]; then summary="$$summary, $$no_exp no .expected file"; fi; \
	echo "$$summary"; \
	[ $$fail -eq 0 ]
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-step-rules
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" = "1" ] || [ -n "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lane-core-body; \
	fi
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-lane-body
endif

test-light: test test-width-tuple-stack test-wide-typed-call-stack

test-correctness: test

# HE step-rules lane: baseline witnesses must pass, then the same witnesses
# must FAIL with each covered rule disabled (anti-decorative gate: proves the
# one-step behavior depends on the rule table, not on a duplicate branch).
test-step-rules: $(BIN)
	@set -e; \
	expected_file=tests/test_step_rules.expected; \
	echo "[step-rules] baseline witnesses"; \
	out=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_step_rules.metta 2>/dev/null); \
	if [ "$$out" != "$$(cat $$expected_file)" ]; then \
		echo "FAIL [step-rules]: baseline witnesses diverged from expected"; exit 1; \
	fi; \
	count=0; \
	for pf in tests/support/step_rules/HES_*.metta; do \
		rule=$$(basename $$pf .metta); \
		echo "[step-rules] rule-removal: $$rule"; \
		out=$$(CETTA_LANGDEF_DISABLED_RULES=$$rule $(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_step_rules.metta 2>/dev/null); \
		if [ "$$out" = "$$(cat $$expected_file)" ]; then \
			echo "FAIL [step-rules]: witnesses did not fail with $$rule disabled (rule table is decorative)"; exit 1; \
		fi; \
		out=$$(CETTA_LANGDEF_DISABLED_RULES=$$rule $(CETTA_BIN_INVOKE) --profile he-extended --lang he $$pf 2>/dev/null); \
		if [ "$$out" != "$$(cat tests/support/step_rules/$$rule.disabled.expected)" ]; then \
			echo "FAIL [step-rules]: disabled-$$rule one-step shape unexpected:"; echo "$$out"; exit 1; \
		fi; \
		count=$$((count + 1)); \
	done; \
	echo "[step-rules] PASS (baseline + $$count rule-removal failures confirmed)"

test-parse-depth-guard: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) ./scripts/test_parse_depth_guard.sh

test-stdlib-growth-memory-regression: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) ./tests/test_stdlib_growth_memory_regression.sh "$(CETTA_SCRIPT_BIN)"

# Differential soundness audit for the rhometta quiet-frontier macro step:
# generate a corpus saturating the payload/effect/space-sharing surface, run
# each program macro-on vs CETTA_RHO_NO_MACRO=1 (the exact reference oracle),
# and assert the may-sets coincide.  Any divergence = the macro optimization is
# unsound on that program.  This is the permanent backstop behind the C3 gate.
test-rhometta-macro-audit: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@pass=0; fail=0; \
	if python3 scripts/rhometta_macro_differential_audit.py --cetta "$(BIN)" --n 120 --seed 1 --timeout "$${CETTA_RHOMETTA_AUDIT_TIMEOUT:-240}"; then \
		echo "PASS: rhometta macro differential audit"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhometta macro differential audit"; fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-eval-gc-adversarial: $(BIN)
	@CETTA_BIN="$(abspath $(BIN))" scripts/gc_adversarial_audit.sh

test-eval-gc-survivor-reset:
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@result=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 $(CETTA_BIN_INVOKE) --profile he-extended --lang he $(GC_SURVIVOR_RESET_TEST) 2>&1); \
	exp="$(GC_SURVIVOR_RESET_TEST:.metta=.expected)"; \
	if [ "$$result" = "$$(cat "$$exp")" ]; then \
		echo "PASS: $(GC_SURVIVOR_RESET_TEST)"; \
	else \
		echo "FAIL: $(GC_SURVIVOR_RESET_TEST)"; \
		diff <(cat "$$exp") <(echo "$$result") | head -80; \
		exit 1; \
	fi
else
	@echo "INFO: eval GC survivor reset diagnostic requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-eval-gc-asan-selected:
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 ENABLE_SANITIZERS=1 SANITIZERS=address,undefined CETTA_PROVENANCE_ASSERT=1 ASAN_REPEATABLE=1 test-eval-gc-asan-selected-body

test-eval-gc-asan-selected-body: $(BIN)
	@CETTA_BIN="$(abspath $(BIN))" scripts/gc_asan_selected_audit.sh

test-eval-gc-asan-full-differential: test-gc-full-fast-differential-contract
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 ENABLE_SANITIZERS=1 SANITIZERS=address,undefined CETTA_PROVENANCE_ASSERT=1 ASAN_REPEATABLE=1 test-eval-gc-asan-full-differential-body

test-eval-gc-asan-full-differential-body: $(BIN)
	@CETTA_BIN="$(abspath $(BIN))" scripts/gc_full_fast_differential_audit.sh

.PHONY: test-gc-full-fast-differential-contract
test-gc-full-fast-differential-contract:
	@scripts/test_gc_full_fast_differential_contract.sh

test-asan:
	@CETTA_RHOMETTA_AUDIT_TIMEOUT=240 CETTA_LIST_LANES_TIMEOUT=240 $(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_SANITIZERS=1 SANITIZERS=address,undefined ASAN_REPEATABLE=1 test-correctness-all

test-asan-main:
	@CETTA_RHOMETTA_AUDIT_TIMEOUT=240 CETTA_LIST_LANES_TIMEOUT=240 $(MAKE) -s BUILD=main ENABLE_SANITIZERS=1 SANITIZERS=address,undefined ASAN_REPEATABLE=1 test-correctness-all

test-asan-mork:
	@CETTA_RHOMETTA_AUDIT_TIMEOUT=240 CETTA_LIST_LANES_TIMEOUT=240 $(MAKE) -s BUILD=mork ENABLE_SANITIZERS=1 SANITIZERS=address,undefined ASAN_REPEATABLE=1 test-correctness-all

test-tsan:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_SANITIZERS=1 SANITIZERS=thread test-rhocalc

test-tsan-main:
	@$(MAKE) -s BUILD=main ENABLE_SANITIZERS=1 SANITIZERS=thread test-rhocalc

test-tsan-mork:
	@$(MAKE) -s BUILD=mork ENABLE_SANITIZERS=1 SANITIZERS=thread test-rhocalc

test-rhocalc-cost-differential: $(BIN)
	@mettapedia_root="$${METTAPEDIA_ROOT:-../../Mettapedia/lean/mettapedia}"; \
	if [ -d "$$mettapedia_root" ]; then \
		METTAPEDIA_ROOT="$$mettapedia_root" $(CETTA_SCRIPT_RUN_ENV) \
			python3 scripts/rhocalc_cost_differential.py "$(CETTA_SCRIPT_BIN)"; \
	else \
		echo "SKIP: cost-rho differential harness (set METTAPEDIA_ROOT to a local Mettapedia checkout)"; \
	fi

test-rhocalc-cost-differential-required: $(BIN)
	@if [ -z "$${METTAPEDIA_ROOT:-}" ]; then \
		echo "FAIL: METTAPEDIA_ROOT is required for the release Cost-Rho differential"; \
		exit 2; \
	elif [ ! -d "$$METTAPEDIA_ROOT" ]; then \
		echo "FAIL: METTAPEDIA_ROOT is not a directory: $$METTAPEDIA_ROOT"; \
		exit 2; \
	else \
		METTAPEDIA_ROOT="$$METTAPEDIA_ROOT" $(CETTA_SCRIPT_RUN_ENV) \
			python3 scripts/rhocalc_cost_differential.py "$(CETTA_SCRIPT_BIN)"; \
	fi

test-rhocalc-cost-parallel-stress: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_parallel_stress.py "$(CETTA_SCRIPT_BIN)"

test-rhocalc-cost-observer-transparency: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_observer_transparency.py "$(CETTA_SCRIPT_BIN)"
else
	@echo "INFO: Cost-Rho observer transparency requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 RHOCOST_COMMIT_AUDIT=$(RHOCOST_COMMIT_AUDIT) $@
endif

test-rhocalc-cost-commit-audit:
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 RHOCOST_COMMIT_AUDIT=1 test-rhocalc-cost-commit-audit-body

test-rhocalc-cost-commit-audit-asan:
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 RHOCOST_COMMIT_AUDIT=1 ENABLE_SANITIZERS=1 SANITIZERS=address,undefined ASAN_REPEATABLE=1 test-rhocalc-cost-commit-audit-body

test-rhocalc-cost-commit-audit-tsan:
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 RHOCOST_COMMIT_AUDIT=1 ENABLE_SANITIZERS=1 SANITIZERS=thread test-rhocalc-cost-commit-audit-body

test-rhocalc-cost-commit-audit-body: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_parallel_stress.py "$(CETTA_SCRIPT_BIN)"
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_observer_transparency.py "$(CETTA_SCRIPT_BIN)"

test-rhocalc-canonical-selector-differential: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_canonical_selector_differential.py "$(CETTA_SCRIPT_BIN)"

test-rho-examples: $(BIN)
	@pass=0; fail=0; \
	for f in examples/rho/pure/*.mrho examples/rho/pure/*.rho; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.*}.expected"; \
		if [ ! -f "$$exp" ]; then continue; fi; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for f in examples/rho/cost/*.mrho examples/rho/cost/*.rho; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.*}.expected"; \
		if [ ! -f "$$exp" ]; then continue; fi; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc --profile cost "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for f in examples/rho/cost/*.metta; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.*}.expected"; \
		if [ ! -f "$$exp" ]; then continue; fi; \
		result=$$($(CETTA_BIN_INVOKE) "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$($(CETTA_BIN_INVOKE) --num-threads 4 --lang rhocalc --profile cost examples/rho/cost/parallel_settlement.mrho 2>&1); \
	if [ "$$result" = "$$(cat examples/rho/cost/parallel_settlement.expected)" ]; then \
		echo "PASS: examples/rho/cost/parallel_settlement.mrho (threaded matches sequential)"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: examples/rho/cost/parallel_settlement.mrho (threaded)"; \
		diff <(cat examples/rho/cost/parallel_settlement.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ "$$fail" -eq 0 ]

test-rhocalc: $(BIN) test-rhocalc-canonical-selector-differential test-rhocalc-abt-substitution test-rho-examples
	@pass=0; fail=0; \
	for f in tests/rhocalc_run/*.mrho tests/rhocalc_run/*.rho; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.*}.expected"; \
		if [ ! -f "$$exp" ]; then continue; fi; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for f in tests/rhocalc_cost_run/*.mrho tests/rhocalc_cost_run/*.rho; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.*}.expected"; \
		if [ ! -f "$$exp" ]; then continue; fi; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc --profile cost "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$($(CETTA_BIN_INVOKE) --rho-reduction-limit 4 --lang rhocalc --syntax mrho tests/rhocalc/paper_divergence_self_recreates_h4.mrho 2>&1); \
	status=$$?; \
	if [ "$$status" -eq 3 ] && [ "$$result" = "$$(cat tests/rhocalc_run/paper_divergence_self_recreates_reduction_limit.expected)" ]; then \
		echo "PASS: rhocalc run reduction-limit exhaustion"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc run reduction-limit exhaustion"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	for f in tests/rhocalc/reject_fresh.mrho \
	         tests/rhocalc/reject_join.mrho \
	         tests/rhocalc/reject_quoted_payload.rho \
	         tests/rhocalc/reject_send_continuation.mrho; do \
		exp="$${f%.*}.expected"; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc "$$f" 2>&1); \
		status=$$?; \
		if [ "$$status" -eq 1 ] && [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: rhocalc strict-boundary reject $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc strict-boundary reject $$f"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for f in tests/rhocalc_cost/reject_dequote.rho; do \
		exp="$${f%.*}.expected"; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc --profile cost "$$f" 2>&1); \
		status=$$?; \
		if [ "$$status" -eq 0 ] && [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: rhocalc cost dequotation $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc cost reject $$f"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for threads in 2 4; do \
		result=$$($(CETTA_BIN_INVOKE) --num-threads $$threads --lang rhocalc --profile cost --syntax mrho tests/rhocalc_cost_run/internal_split_tokens_basic.mrho 2>&1); \
		status=$$?; \
		if [ "$$status" -eq 0 ] && [ "$$result" = "$$(cat tests/rhocalc_cost_run/internal_split_tokens_basic.expected)" ]; then \
			echo "PASS: rhocalc cost threaded execution ($$threads workers)"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc cost threaded execution ($$threads workers)"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$($(CETTA_BIN_INVOKE) --num-threads 0 --lang rhocalc --syntax mrho tests/rhocalc_run/stuck_pending_send.mrho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc_run/stuck_pending_send.expected)" ]; then \
		echo "PASS: rhocalc zero thread budget uses sequential path"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc zero thread budget uses sequential path"; \
		diff <(cat tests/rhocalc_run/stuck_pending_send.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --num-threads 2 --rho-scheduler rotating --lang rhocalc --syntax mrho tests/rhocalc_run/core_comm_run.mrho 2>&1); \
	status=$$?; \
	if [ "$$status" -eq 2 ] && printf '%s\n' "$$result" | grep -q "does not combine with --num-threads"; then \
		echo "PASS: rhocalc rejects scheduler with threaded execution"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc rejects scheduler with threaded execution"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	for f in tests/rhocalc/no_reduction_under_recv_puritanical.mrho \
	         tests/rhocalc/quoted_process_not_reduced_in_name_eq.mrho \
	         tests/rhocalc/open_name_variable_roundtrip_h7.mrho \
	         tests/rhocalc/open_quoted_name_roundtrip_h7.mrho \
	         tests/rhocalc/free_drop_is_stuck_h4.mrho \
	         tests/rhocalc/surface_open_name_variable.rho; do \
		exp="$${f%.*}.expected"; \
		result=$$($(CETTA_BIN_INVOKE) --lang rhocalc "$$f" 2>&1); \
		status=$$?; \
		if [ "$$status" -eq 0 ] && [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: rhocalc quiescent/open-name contract $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc quiescent/open-name contract $$f"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for f in tests/rhocalc/*.mrho; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.*}.expected"; \
		[ -f "$$exp" ] || continue; \
		first=$$(python3 scripts/extract_first_rhocalc_successor_item.py "$$exp"); \
		[ -n "$$first" ] || continue; \
		result=$$($(CETTA_BIN_INVOKE) --rho-reduction-limit 1 --lang rhocalc --syntax mrho "$$f" 2>/dev/null || true); \
		if [ "$$result" = "$$first" ]; then \
			echo "PASS: rhocalc canonical single-reduction parity $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc canonical single-reduction parity $$f"; \
			diff <(printf '%s\n' "$$first") <(printf '%s\n' "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$($(CETTA_BIN_INVOKE) --num-threads 4 --lang rhocalc --syntax mrho tests/rhocalc_run/core_comm_run.mrho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc_run/core_comm_run.expected)" ]; then \
		echo "PASS: rhocalc thread-budget plumbing"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc thread-budget plumbing"; \
		diff <(cat tests/rhocalc_run/core_comm_run.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_threaded_stress.py "$(CETTA_SCRIPT_BIN)"; then \
		pass=$$((pass + 7)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_parallel_stress.py "$(CETTA_SCRIPT_BIN)"; then \
		pass=$$((pass + 6)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --rho-reduction-limit 2 --lang rhocalc --syntax mrho tests/rhocalc/rotating_scheduler_persistent_branch.mrho 2>&1); \
	status=$$?; \
	if [ "$$status" -eq 3 ] && [ "$$result" = "$$(cat tests/rhocalc/rotating_scheduler_persistent_branch_canonical_reduction_limit2.expected)" ]; then \
		echo "PASS: rhocalc canonical scheduler witness"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc canonical scheduler witness"; \
		diff <(cat tests/rhocalc/rotating_scheduler_persistent_branch_canonical_reduction_limit2.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --rho-reduction-limit 2 --rho-scheduler rotating --lang rhocalc --syntax mrho tests/rhocalc/rotating_scheduler_persistent_branch.mrho 2>&1); \
	status=$$?; \
	if [ "$$status" -eq 3 ] && [ "$$result" = "$$(cat tests/rhocalc/rotating_scheduler_persistent_branch_rotating_reduction_limit2.expected)" ]; then \
		echo "PASS: rhocalc rotating scheduler witness"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc rotating scheduler witness"; \
		diff <(cat tests/rhocalc/rotating_scheduler_persistent_branch_rotating_reduction_limit2.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	while IFS=$$(printf '\t') read -r name syntax depth policy fixture; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_bounded_reachability.py "$(CETTA_SCRIPT_BIN)" "$$syntax" "$$depth" "$$policy" "$$fixture"; then \
			echo "PASS: rhocalc scheduler reachability $$name"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc scheduler reachability $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done < tests/rhocalc_scheduler_membership.tsv; \
	while IFS=$$(printf '\t') read -r name depth lhs_syntax lhs_fixture rhs_syntax rhs_fixture; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_bounded_relation_equivalence.py "$(CETTA_SCRIPT_BIN)" "$$depth" "$$lhs_syntax" "$$lhs_fixture" "$$rhs_syntax" "$$rhs_fixture"; then \
			echo "PASS: rhocalc bounded relation equivalence $$name"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc bounded relation equivalence $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done < tests/rhocalc_relation_equivalence.tsv; \
	if ! $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_bounded_relation_equivalence.py "$(CETTA_SCRIPT_BIN)" 1 mrho tests/rhocalc/core_comm.mrho mrho tests/rhocalc/literal_drop_quote_static_after_comm.mrho >/dev/null 2>&1; then \
		echo "PASS: rhocalc bounded relation inequivalence literal-static-drop"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc bounded relation inequivalence literal-static-drop"; \
		fail=$$((fail + 1)); \
	fi; \
	while IFS=$$(printf '\t') read -r name depth lhs_syntax lhs_fixture rhs_syntax rhs_fixture expect; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_bounded_bisimulation.py "$(CETTA_SCRIPT_BIN)" "$$depth" "$$lhs_syntax" "$$lhs_fixture" "$$rhs_syntax" "$$rhs_fixture" "$$expect"; then \
			echo "PASS: rhocalc bounded bisimulation $$name"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc bounded bisimulation $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done < tests/rhocalc_bisimulation.tsv; \
	while IFS=$$(printf '\t') read -r name contract depth rho_fixture core_fixture; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		case "$$contract" in \
			bisim) \
				if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_bounded_bisimulation.py "$(CETTA_SCRIPT_BIN)" "$$depth" rho "$$rho_fixture" mrho "$$core_fixture" yes; then \
					echo "PASS: rhocalc M3 overlap $$name"; \
					pass=$$((pass + 1)); \
				else \
					echo "FAIL: rhocalc M3 overlap $$name"; \
					fail=$$((fail + 1)); \
				fi ;; \
			relation) \
				if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_bounded_relation_equivalence.py "$(CETTA_SCRIPT_BIN)" "$$depth" rho "$$rho_fixture" mrho "$$core_fixture"; then \
					echo "PASS: rhocalc M3 overlap $$name"; \
					pass=$$((pass + 1)); \
				else \
					echo "FAIL: rhocalc M3 overlap $$name"; \
					fail=$$((fail + 1)); \
				fi ;; \
			*) \
				echo "FAIL: rhocalc M3 overlap $$name"; \
				echo "unknown overlap contract: $$contract"; \
				fail=$$((fail + 1)) ;; \
		esac; \
	done < tests/rhocalc_m3_overlap.tsv; \
	if python3 scripts/rhocalc_m3_rholang_cli_compare_selftest.py; then \
		echo "PASS: rhocalc M3 rholang-cli parser/oracle selftest"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc M3 rholang-cli parser/oracle selftest"; \
		fail=$$((fail + 1)); \
	fi; \
	while IFS=$$(printf '\t') read -r name cetta_syntax cetta_fixture expected_outputs; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_m3_may_must.py "$(CETTA_SCRIPT_BIN)" "$$cetta_syntax" "$$cetta_fixture" "$$expected_outputs"; then \
			echo "PASS: rhocalc M3 may/must frontier $$name"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc M3 may/must frontier $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done < tests/rhocalc_m3_may_must.tsv; \
	rholang_cli="$${RHOLANG_CLI:-$$(command -v rholang-cli || true)}"; \
	if [ -z "$$rholang_cli" ] && [ -x ../f1r3node/target/release/rholang-cli ]; then \
		rholang_cli=../f1r3node/target/release/rholang-cli; \
	fi; \
	if [ -x "$$rholang_cli" ]; then \
		while IFS=$$(printf '\t') read -r name cetta_syntax cetta_fixture rholang_fixture expected_outputs; do \
			[ -n "$$name" ] || continue; \
			case "$$name" in \#*) continue ;; esac; \
			if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_m3_rholang_cli_compare.py "$(CETTA_SCRIPT_BIN)" "$$rholang_cli" "$$cetta_syntax" "$$cetta_fixture" "$$rholang_fixture" "$$expected_outputs"; then \
				echo "PASS: rhocalc M3 rholang-cli overlap $$name"; \
				pass=$$((pass + 1)); \
			else \
				echo "FAIL: rhocalc M3 rholang-cli overlap $$name"; \
				fail=$$((fail + 1)); \
			fi; \
		done < tests/rhocalc_m3_rholang_cli.tsv; \
		if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_m3_rholang_cli_compare.py "$(CETTA_SCRIPT_BIN)" "$$rholang_cli" metta benchmarks/rho/route-policy/example.metta benchmarks/rho/route-policy/example.rho 'c1=nil' >/dev/null 2>&1; then \
			echo "FAIL: rhocalc M3 rholang-cli rejects wrong payload"; \
			fail=$$((fail + 1)); \
		else \
			echo "PASS: rhocalc M3 rholang-cli rejects wrong payload"; \
			pass=$$((pass + 1)); \
		fi; \
	else \
		echo "SKIP: rhocalc M3 rholang-cli overlap (set RHOLANG_CLI or install rholang-cli)"; \
	fi; \
	for f in tests/test_lts_surface.metta tests/test_rho_lib_surface.metta tests/test_rho_lib_hygiene_surface.metta tests/test_rhometta_lib_surface.metta tests/test_rhometta_isolation_oracle.metta tests/test_rhometta_demo_dedfarm.metta tests/test_rhometta_demo_revision.metta tests/test_rhometta_demo_mayset.metta tests/test_rhometta_demo_ecan.metta tests/test_lts_rho_surface.metta tests/test_lts_rho_cost_surface.metta tests/test_lts_rho_cost_causal_trace.metta tests/test_lts_rho_cost_parallel_branches.metta tests/test_lts_rho_cost_search_budget.metta; do \
		exp="$${f%.metta}.expected"; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: rhocalc lib/rho surface $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc lib/rho surface $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	while IFS=$$(printf '\t') read -r name fixture; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_tiny_oracle_check.py "$(CETTA_SCRIPT_BIN)" "$$fixture"; then \
			echo "PASS: rhocalc tiny oracle $$name"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: rhocalc tiny oracle $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done < tests/rhocalc_tiny_oracle.tsv; \
	mettapedia_root="$${METTAPEDIA_ROOT:-../../Mettapedia/lean/mettapedia}"; \
	rhocalc_lean_skip=85; \
	if [ -d "$$mettapedia_root" ]; then \
			while IFS=$$(printf '\t') read -r name fixture expected_count mode expected_file lean_file anchor; do \
				[ -n "$$name" ] || continue; \
				case "$$name" in \#*) continue ;; esac; \
				if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_lean_trace_bridge.py "$(CETTA_SCRIPT_BIN)" "$$fixture" "$$expected_count" "$$mode" "$$expected_file" "$$lean_file" "$$anchor"; then \
					echo "PASS: rhocalc lean trace bridge $$name"; \
				pass=$$((pass + 1)); \
			else \
				echo "FAIL: rhocalc lean trace bridge $$name"; \
				fail=$$((fail + 1)); \
			fi; \
		done < tests/rhocalc_lean_trace_bridge.tsv; \
		while IFS=$$(printf '\t') read -r name fixture expected_count mode expected_file lean_file anchor; do \
			[ -n "$$name" ] || continue; \
			case "$$name" in \#*) continue ;; esac; \
			if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_lean_trace_bridge.py "$(CETTA_SCRIPT_BIN)" "$$fixture" "$$expected_count" "$$mode" "$$expected_file" "$$lean_file" "$$anchor"; then \
				echo "PASS: rhocalc run lean bridge $$name"; \
				pass=$$((pass + 1)); \
			else \
				echo "FAIL: rhocalc run lean bridge $$name"; \
				fail=$$((fail + 1)); \
			fi; \
		done < tests/rhocalc_run_lean_bridge.tsv; \
		while IFS=$$(printf '\t') read -r name fixture expected_file lean_file anchor; do \
			[ -n "$$name" ] || continue; \
			case "$$name" in \#*) continue ;; esac; \
			bridge_output=$$(METTAPEDIA_ROOT="$$mettapedia_root" $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_lean_bridge.py "$(CETTA_SCRIPT_BIN)" "$$fixture" "$$expected_file" "$$lean_file" "$$anchor" 2>&1); \
			bridge_status=$$?; \
			if [ "$$bridge_status" -eq 0 ]; then \
				echo "PASS: rhocalc cost lean bridge $$name"; \
				pass=$$((pass + 1)); \
			elif [ "$$bridge_status" -eq "$$rhocalc_lean_skip" ]; then \
				printf '%s\n' "$$bridge_output"; \
				echo "SKIP: rhocalc cost lean bridge $$name"; \
			else \
				printf '%s\n' "$$bridge_output"; \
				echo "FAIL: rhocalc cost lean bridge $$name"; \
				fail=$$((fail + 1)); \
			fi; \
		done < tests/rhocalc_cost_lean_bridge.tsv; \
		differential_output=$$(METTAPEDIA_ROOT="$$mettapedia_root" $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_differential.py "$(CETTA_SCRIPT_BIN)" 2>&1); \
		differential_status=$$?; \
		if [ "$$differential_status" -eq 0 ]; then \
			printf '%s\n' "$$differential_output"; \
			pass=$$((pass + 1)); \
		else \
			printf '%s\n' "$$differential_output"; \
			echo "FAIL: bounded cost-rho CeTTa/Lean differential"; \
			fail=$$((fail + 1)); \
		fi; \
		microcheck_output=$$(python3 scripts/rhocalc_lean_microcheck.py "$$mettapedia_root" tests/rhocalc_lean_microcheck.lean 2>&1); \
		microcheck_status=$$?; \
		if [ "$$microcheck_status" -eq 0 ]; then \
			echo "PASS: rhocalc lean microcheck"; \
			pass=$$((pass + 1)); \
		elif [ "$$microcheck_status" -eq "$$rhocalc_lean_skip" ]; then \
			printf '%s\n' "$$microcheck_output"; \
			echo "SKIP: rhocalc lean microcheck"; \
		else \
			printf '%s\n' "$$microcheck_output"; \
			echo "FAIL: rhocalc lean microcheck"; \
			fail=$$((fail + 1)); \
		fi; \
	else \
		echo "SKIP: rhocalc lean trace bridge (set METTAPEDIA_ROOT to a local Mettapedia checkout)"; \
		echo "SKIP: rhocalc cost lean bridge (set METTAPEDIA_ROOT to a local Mettapedia checkout)"; \
		echo "SKIP: cost-rho differential harness (set METTAPEDIA_ROOT to a local Mettapedia checkout)"; \
		echo "SKIP: rhocalc lean microcheck (set METTAPEDIA_ROOT to a local Mettapedia checkout)"; \
	fi; \
	if python3 -c "from pathlib import Path; import re, sys; lines = Path('lib/rho.metta').read_text().splitlines(); pat = re.compile(r'^\\s*\\((=|:)\\s+\\((rho[.:](step|frontier|reduce|eval))\\b'); sys.exit(1 if any((not line.lstrip().startswith(';')) and pat.search(line) for line in lines) else 0)" >/dev/null; then \
		echo "PASS: rhocalc lib/rho hygiene"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc lib/rho hygiene"; \
		python3 -c "from pathlib import Path; import re; lines = Path('lib/rho.metta').read_text().splitlines(); pat = re.compile(r'^\\s*\\((=|:)\\s+\\((rho[.:](step|frontier|reduce|eval)\\b)'); [print(f'{i}:{line}') for i, line in enumerate(lines, 1) if (not line.lstrip().startswith(';')) and pat.search(line)]" || true; \
		fail=$$((fail + 1)); \
	fi; \
	if ! rg -n 'rhocalc_one_step|rhocalc_steps_atom|RHO_STEPS|rho[.:]steps' src lib tests scripts benchmarks >/dev/null; then \
		echo "PASS: rhocalc old step surface purged"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc old step surface purged"; \
		rg -n 'rhocalc_one_step|rhocalc_steps_atom|RHO_STEPS|rho[.:]steps' src lib tests scripts benchmarks || true; \
		fail=$$((fail + 1)); \
	fi; \
expected_allow_files=$$(printf '%s\n' lib/rho.metta tests/test_rho_lib_hygiene_surface.metta | sort); \
	actual_allow_files=$$(rg -l 'rho[.:](step|frontier|reduce|eval)([^[:alnum:]_-]|$$)' lib tests src scripts benchmarks | sort); \
	if [ "$$actual_allow_files" = "$$expected_allow_files" ]; then \
		echo "PASS: rhocalc de-step allow-list surface"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc de-step allow-list surface"; \
		printf '%s\n' '--- expected files ---'; \
		printf '%s\n' "$$expected_allow_files"; \
		printf '%s\n' '--- actual files ---'; \
		printf '%s\n' "$$actual_allow_files"; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --translate --syntax rho --lang rhocalc --lang rhocalc --syntax mrho tests/rhocalc/pure_surface.rho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc/translate_rho_to_mrho.expected)" ]; then \
		echo "PASS: rhocalc translate rho -> mrho"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc translate rho -> mrho"; \
		diff <(cat tests/rhocalc/translate_rho_to_mrho.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --translate --syntax mrho --lang rhocalc --lang rhocalc --syntax rho tests/rhocalc/core_comm.mrho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc/translate_mrho_to_rho.expected)" ]; then \
		echo "PASS: rhocalc translate mrho -> rho"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc translate mrho -> rho"; \
		diff <(cat tests/rhocalc/translate_mrho_to_rho.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --translate --syntax mrho --lang rhocalc --lang rhocalc --syntax mrho tests/rhocalc/mrho_free_name_same_spelling_binder.mrho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc/translate_mrho_alpha_to_mrho.expected)" ]; then \
		echo "PASS: rhocalc translate alpha mrho -> mrho"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc translate alpha mrho -> mrho"; \
		diff <(cat tests/rhocalc/translate_mrho_alpha_to_mrho.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --translate --syntax mrho --lang rhocalc --lang rhocalc --syntax rho tests/rhocalc/mrho_free_name_same_spelling_binder.mrho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc/translate_mrho_alpha_to_rho.expected)" ]; then \
		echo "PASS: rhocalc translate alpha mrho -> rho"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc translate alpha mrho -> rho"; \
		diff <(cat tests/rhocalc/translate_mrho_alpha_to_rho.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --translate --syntax rho --lang rhocalc --lang rhocalc --syntax mrho tests/rhocalc/surface_shadowing.rho 2>&1); \
	if [ "$$result" = "$$(cat tests/rhocalc/translate_rho_shadow_to_mrho.expected)" ]; then \
		echo "PASS: rhocalc translate shadow rho -> mrho"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc translate shadow rho -> mrho"; \
		diff <(cat tests/rhocalc/translate_rho_shadow_to_mrho.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-lib-parse-oracles: $(BIN)
	@if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/lib_parse_metamath_sealed_oracle.py "$(CETTA_SCRIPT_BIN)"; then \
		echo "PASS: lib_parse metamath mm-lean4 oracle"; \
	else \
		echo "FAIL: lib_parse metamath mm-lean4 oracle"; \
		exit 1; \
	fi

test-rhocalc-lib-parse-reference: $(BIN)
	@if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_lib_parse_oracle.py "$(CETTA_SCRIPT_BIN)"; then \
		echo "PASS: rhocalc lib_parse reference oracle"; \
	else \
		echo "FAIL: rhocalc lib_parse reference oracle"; \
		exit 1; \
	fi

test-lib-parse-shared-cert: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_shared_cert_regression.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_lib_parse_shared_cert_regression.expected)" ]; then \
		echo "PASS: lib_parse shared cert regression"; \
	else \
		echo "FAIL: lib_parse shared cert regression"; \
		diff <(cat tests/test_lib_parse_shared_cert_regression.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-lib-parse-gll-utf8-forest: $(LIB_PARSE_GLL_UTF8_FOREST_TEST_BIN)
	@$(LIB_PARSE_GLL_UTF8_FOREST_TEST_BIN)

test-lib-parse-glr-utf8-forest: $(LIB_PARSE_GLR_UTF8_FOREST_TEST_BIN)
	@$(LIB_PARSE_GLR_UTF8_FOREST_TEST_BIN)

test-lib-parse-slr-prepared: $(LIB_PARSE_SLR_PREPARED_TEST_BIN)
	@$(LIB_PARSE_SLR_PREPARED_TEST_BIN)

test-gslt2parse-parser-pack-gll-v1-native: \
		test-gslt-dense-bitset-v1 $(PARSER_PACK_GLL_V1_TEST_BIN)
	@$(PARSER_PACK_GLL_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into the generic ParserPack GLL path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-glr-v1-native: $(PARSER_PACK_GLR_V1_TEST_BIN)
	@$(PARSER_PACK_GLR_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into the generic ParserPack GLR path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-lexical-v1-native: $(PARSER_PACK_LEXICAL_V1_TEST_BIN)
	@$(PARSER_PACK_LEXICAL_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|cetta[-_ ]?prime' \
		experiments/gslt2parse_foundation/native/parser_pack_lexical_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_lexical_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		experiments/gslt2parse_foundation/native/regular_span_dfa_v1.c \
		experiments/gslt2parse_foundation/native/regular_span_dfa_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into the generic lexical projection path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-guard-plan-v1-native: \
		$(PARSER_PACK_GUARD_PLAN_V1_TEST_BIN)
	@$(PARSER_PACK_GUARD_PLAN_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|cetta[-_ ]?prime' \
		experiments/gslt2parse_foundation/native/parser_pack_guard_plan_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_guard_plan_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_guard_relation_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h; then \
		echo 'guest-language name leaked into the generic positive guard path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-lexical-v1-matrix: \
		test-gslt2parse-parser-pack-lexical-v1-native \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(REGULAR_SPAN_DFA_V1_STREAM_BIN) \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'GSLT2PARSE_PETTA_ROOT is required for the lexical projection matrix'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--projection-binary $(REGULAR_SPAN_DFA_V1_STREAM_BIN) \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-gll-v1-matrix: \
		test-gslt2parse-parser-pack-gll-v1-native \
		$(PARSER_PACK_GLL_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_gll_v1.py \
		--binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into the generic ParserPack GLL path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-glr-v1-matrix: \
		test-gslt2parse-parser-pack-gll-v1-native \
		test-gslt2parse-parser-pack-glr-v1-native \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_gll_v1.py \
		--binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into a generic ParserPack parser path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-wide-scale-v1: \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/probe_parser_pack_wide_scale_v1.py \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-regular-span-dfa-v1-native: \
		$(REGULAR_SPAN_DFA_V1_TEST_BIN) \
		$(REGULAR_SPAN_NFA_V1_TEST_BIN) \
		$(REGULAR_SPAN_DFA_V1_STREAM_BIN)
	@$(REGULAR_SPAN_DFA_V1_TEST_BIN)
	@$(REGULAR_SPAN_NFA_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|cetta[-_ ]?prime' \
		experiments/gslt2parse_foundation/native/regular_span_dfa_v1.c \
		experiments/gslt2parse_foundation/native/regular_span_dfa_v1.h \
		experiments/gslt2parse_foundation/native/regular_span_nfa_v1.c \
		experiments/gslt2parse_foundation/native/regular_span_nfa_v1.h \
		experiments/gslt2parse_foundation/native/regular_span_dfa_v1_stream.c; then \
		echo 'guest-language name leaked into the generic regular-span DFA path'; \
		exit 1; \
	fi

test-gslt2parse-regular-span-dfa-v1-matrix: \
		test-gslt2parse-regular-span-dfa-v1-native \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_regular_span_dfa_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--dfa-binary $(REGULAR_SPAN_DFA_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

ifeq ($(ENABLE_PIC),1)
test-gslt2parse-parser-pack-native-api-v1-matrix: \
		$(PARSER_PACK_NATIVE_API_V1_LIB)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@$(GSLT2PARSE_SHARED_ASAN_ENV) python3 \
		tools/test_parser_pack_native_api_v1.py \
		--library $(PARSER_PACK_NATIVE_API_V1_LIB) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		$(GSLT2PARSE_SHARED_ASAN_ARGS)
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_native_api_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_api_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into the native ParserPack API'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-native-petta-v1: \
		test-gslt2parse-parser-pack-native-api-v1-matrix
	@$(if $(and $(filter 1,$(ENABLE_SANITIZERS)),$(filter address,$(SANITIZER_WORDS))), \
		$(MAKE) --no-print-directory ENABLE_SANITIZERS=0 ENABLE_PIC=1 \
			GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
			test-gslt2parse-parser-pack-native-petta-v1-body, \
		$(MAKE) --no-print-directory \
			GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
			test-gslt2parse-parser-pack-native-petta-v1-body)

test-gslt2parse-parser-pack-native-petta-v1-body: \
		$(PARSER_PACK_NATIVE_API_V1_LIB) \
		$(PETTA_DOCUMENT_PIPELINE_V1_LIB)
	@$(MAKE) -C \
		"$(GSLT2PARSE_PETTA_ROOT)/experiments/gslt2parse_foundation/native" \
		test CETTA_ROOT="$(CURDIR)" CETTA_OBJ_TAG="$(BUILD_OBJ_TAG)"
else
test-gslt2parse-parser-pack-native-api-v1-matrix \
test-gslt2parse-parser-pack-native-petta-v1:
	@echo 'rerun this gate with ENABLE_PIC=1'; \
	exit 1
endif

test-gslt2parse-generic-engine-purity-v1:
	@if rg -ni '\b(he|petta|metta|rho|mrho|rhocalc|hyperon)\b|metamath|megalodon|tptp|cetta[-_ ]?prime|set\.mm' \
		$(GSLT2PARSE_GENERIC_ENGINE_SOURCES) \
		$(GSLT2PARSE_GENERIC_COMPILER_SOURCES); then \
		echo 'guest-language knowledge leaked into a generic GSLT2Parse engine or compiler'; \
		exit 1; \
	fi
	@printf '(GSLT2ParseGenericEnginePurityV1Summary %s 0)\n' \
		"$(words $(GSLT2PARSE_GENERIC_ENGINE_SOURCES))"
	@printf '(GSLT2ParseGenericCompilerPurityV1Summary %s 0)\n' \
		"$(words $(GSLT2PARSE_GENERIC_COMPILER_SOURCES))"

test-gslt2parse-c-production-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@$(MAKE) --no-print-directory ENABLE_PIC=1 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		GSLT2PARSE_HE_ROOT="$(GSLT2PARSE_HE_ROOT)" \
		test-gslt2parse-c-production-v1-body

test-gslt2parse-c-production-v1-body: \
		test-gslt2parse-generic-engine-purity-v1 \
		test-gslt2parse-schema-v1 \
		test-gslt2parse-c-horn-v1-differential \
		test-gslt2parse-parser-pack-guard-compiler-v1 \
		test-gslt2parse-parser-pack-guard-regular-v1 \
		test-gslt2parse-parser-pack-lr1-v1 \
		test-gslt2parse-parser-pack-lexical-plan-v1 \
		test-gslt2parse-parser-pack-guarded-lexical-v1 \
		test-gslt2parse-parser-pack-guard-plan-he-v1 \
		test-gslt2parse-rho-abstract-syntax-v1 \
		test-gslt2parse-parser-term-projection-v1-native \
		test-gslt2parse-parser-atom-projection-v1-native \
		test-gslt2parse-parser-atom-projection-closure-v1 \
		test-gslt2parse-semantic-mask-span-compiler-v1 \
		test-gslt2parse-rhocalc-reader-authority-v1 \
		test-gslt2parse-rhocalc-parser-pack-v1 \
		test-gslt2parse-rho-surface-convergence-v1 \
		test-gslt2parse-he-reader-source-faithfulness-v1 \
		test-gslt2parse-he-reader-source-correspondence-v1 \
		test-gslt2parse-he-reader-guard-exec-v1 \
		test-gslt2parse-he-reader-guarded-lexical-v1 \
		test-gslt2parse-he-document-pipeline-v1 \
		test-gslt2parse-he-gslt-parse-only-v1 \
		test-gslt2parse-petta-form-guard-exec-v1 \
		test-gslt2parse-petta-document-splitter-v1 \
		test-gslt2parse-petta-ffi-v1 \
		test-gslt2parse-stable-parser-parse-only-v1 \
		test-gslt2parse-parser-pack-guard-plan-prime-v1 \
		test-gslt2parse-parser-pack-guard-plan-v1-native \
		test-gslt2parse-parser-pack-abi-v1-matrix \
		test-gslt2parse-parser-pack-glr-v1-matrix \
		test-gslt2parse-parser-pack-wide-scale-v1 \
		test-gslt2parse-parser-pack-lexical-v1-matrix \
		test-gslt2parse-regular-span-dfa-v1-matrix \
		test-lib-parse-slr-prepared \
		test-lib-parse-gll-utf8-forest \
		test-lib-parse-glr-utf8-forest
	@echo '(GSLT2ParseCProductionV1Summary 34 0)'

test-lib-parse-native-gparse: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_grammar_summary.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_grammar_summary.expected)" ]; then \
		echo "PASS: gparse native grammar summary"; \
	else \
		echo "FAIL: gparse native grammar summary"; \
		diff <(cat tests/test_gparse_native_grammar_summary.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_grammar_data.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_grammar_data.expected)" ]; then \
		echo "PASS: gparse native grammar data"; \
	else \
		echo "FAIL: gparse native grammar data"; \
		diff <(cat tests/test_gparse_native_grammar_data.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_slr_summary.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_slr_summary.expected)" ]; then \
		echo "PASS: gparse native SLR summary"; \
	else \
		echo "FAIL: gparse native SLR summary"; \
		diff <(cat tests/test_gparse_native_slr_summary.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_slr_parse_shared.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_slr_parse_shared.expected)" ]; then \
		echo "PASS: gparse native SLR shared parse"; \
	else \
		echo "FAIL: gparse native SLR shared parse"; \
		diff <(cat tests/test_gparse_native_slr_parse_shared.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_glr_class.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_glr_class.expected)" ]; then \
		echo "PASS: gparse native GLR class"; \
	else \
		echo "FAIL: gparse native GLR class"; \
		diff <(cat tests/test_gparse_native_glr_class.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_glr_parse_shared.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_glr_parse_shared.expected)" ]; then \
		echo "PASS: gparse native GLR shared parse"; \
	else \
		echo "FAIL: gparse native GLR shared parse"; \
		diff <(cat tests/test_gparse_native_glr_parse_shared.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_gll_parse_shared.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_gll_parse_shared.expected)" ]; then \
		echo "PASS: gparse native GLL shared parse"; \
	else \
		echo "FAIL: gparse native GLL shared parse"; \
		diff <(cat tests/test_gparse_native_gll_parse_shared.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_glr_forest_summary.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_glr_forest_summary.expected)" ]; then \
		echo "PASS: gparse native GLR forest summary"; \
	else \
		echo "FAIL: gparse native GLR forest summary"; \
		diff <(cat tests/test_gparse_native_glr_forest_summary.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_gll_forest_summary.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_gll_forest_summary.expected)" ]; then \
		echo "PASS: gparse native GLL forest summary"; \
	else \
		echo "FAIL: gparse native GLL forest summary"; \
		diff <(cat tests/test_gparse_native_gll_forest_summary.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_glr_forest_data.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_glr_forest_data.expected)" ]; then \
		echo "PASS: gparse native GLR forest data"; \
	else \
		echo "FAIL: gparse native GLR forest data"; \
		diff <(cat tests/test_gparse_native_glr_forest_data.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_gll_forest_data.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_gll_forest_data.expected)" ]; then \
		echo "PASS: gparse native GLL forest data"; \
	else \
		echo "FAIL: gparse native GLL forest data"; \
		diff <(cat tests/test_gparse_native_gll_forest_data.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_dual_forest_signature.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_dual_forest_signature.expected)" ]; then \
		echo "PASS: gparse native dual forest signature"; \
	else \
		echo "FAIL: gparse native dual forest signature"; \
		diff <(cat tests/test_gparse_native_dual_forest_signature.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_forest_digest.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_forest_digest.expected)" ]; then \
		echo "PASS: gparse native forest digest"; \
	else \
		echo "FAIL: gparse native forest digest"; \
		diff <(cat tests/test_gparse_native_forest_digest.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_dispatch.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_dispatch.expected)" ]; then \
		echo "PASS: gparse native dispatch"; \
	else \
		echo "FAIL: gparse native dispatch"; \
		diff <(cat tests/test_gparse_native_dispatch.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_corpus.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_corpus.expected)" ]; then \
		echo "PASS: gparse native Metamath corpus"; \
	else \
		echo "FAIL: gparse native Metamath corpus"; \
		diff <(cat tests/test_gparse_native_metamath_corpus.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_frontier.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_frontier.expected)" ]; then \
		echo "PASS: gparse native Metamath frontier"; \
	else \
		echo "FAIL: gparse native Metamath frontier"; \
		diff <(cat tests/test_gparse_native_metamath_frontier.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_defs_theorem_length_ladder.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_defs_theorem_length_ladder.expected)" ]; then \
		echo "PASS: gparse native Metamath defs theorem-length ladder"; \
	else \
		echo "FAIL: gparse native Metamath defs theorem-length ladder"; \
		diff <(cat tests/test_gparse_native_metamath_defs_theorem_length_ladder.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_defs_component_ladder.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_defs_component_ladder.expected)" ]; then \
		echo "PASS: gparse native Metamath defs component ladder"; \
	else \
		echo "FAIL: gparse native Metamath defs component ladder"; \
		diff <(cat tests/test_gparse_native_metamath_defs_component_ladder.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_plus_weq_variant_matrix.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_plus_weq_variant_matrix.expected)" ]; then \
		echo "PASS: gparse native Metamath plus/weq variant matrix"; \
	else \
		echo "FAIL: gparse native Metamath plus/weq variant matrix"; \
		diff <(cat tests/test_gparse_native_metamath_plus_weq_variant_matrix.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_backend_report.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_backend_report.expected)" ]; then \
		echo "PASS: gparse native backend report"; \
	else \
		echo "FAIL: gparse native backend report"; \
		diff <(cat tests/test_gparse_native_backend_report.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-lib-parse-generalized-native-integration: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_generalized_integration.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_generalized_integration.expected)" ]; then \
		echo "PASS: lib_parse generalized native integration"; \
	else \
		echo "FAIL: lib_parse generalized native integration"; \
			diff <(cat tests/test_gparse_native_generalized_integration.expected) <(echo "$$result") | head -20; \
			exit 1; \
	fi

test-lib-parse-generalized-cli: test-lib-parse-generalized-native-integration

test-lib-parse-python-shadow-audit:
	@cd "$(CURDIR)" && \
	if rg -n 'MM_LR|from lib_parse_generalized_backend_runtime|from lib_parse_generalized_runtime|import lib_parse_generalized_backend_runtime|import lib_parse_generalized_runtime' scripts/lib_parse_*py | grep -v 'scripts/lib_parse_generalized_backend_runtime.py' | grep -v 'scripts/lib_parse_generalized_runtime.py'; then \
		echo "FAIL: lib_parse python shadow audit"; \
		exit 1; \
	else \
		echo "PASS: lib_parse python shadow audit"; \
	fi
	@cd "$(CURDIR)" && \
	py_audit_files=$$(find scripts tests/support -path '*/__pycache__' -prune -o -name '*.py' -print); \
	if rg -n 'class (GLL|GLR|SPPF|GSS|BSR)|def (closure|goto|shift|reduce|predict|scan|complete)|metamath_db_grammar|generic_parser_as_data|lib_parse_chart_backend|Earley|run_earley' $$py_audit_files tests/support/lib_parse_generalized_cases.json; then \
		echo "FAIL: lib_parse python parser-implementation audit"; \
		exit 1; \
	else \
		echo "PASS: lib_parse python parser-implementation audit"; \
	fi
	@cd "$(CURDIR)" && \
	json_grammar_sources=$$(find scripts tests/support -path '*/__pycache__' -prune -o -name '*grammar*.json' -print | grep -v '^tests/support/lib_parse_generalized_cases\.json$$' || true); \
	if [ -n "$$json_grammar_sources" ]; then \
		echo "FAIL: lib_parse json grammar source audit"; \
		echo "$$json_grammar_sources"; \
		exit 1; \
	elif rg -n '"(productions|terminals|lexical_classes|rules)"|metamath_db_grammar|generic_parser_as_data|lib_parse_chart_backend|Earley' tests/support/lib_parse_generalized_cases.json; then \
		echo "FAIL: lib_parse json grammar source audit"; \
		exit 1; \
	else \
		echo "PASS: lib_parse json grammar source audit"; \
	fi
	@cd "$(CURDIR)" && \
	if rg -n 'Metamath|metamath|MM_|mm-db|mm_|\$$[cvfeap]([^A-Za-z0-9_-]|$$)|proof_hdr|proof_list' src/lib_parse_native_grammar.c src/lib_parse_native_grammar.h native/native_modules.c lib/gparse.metta; then \
		echo "FAIL: lib_parse generic core contains Metamath benchmark detail"; \
		exit 1; \
	else \
		echo "PASS: lib_parse generic core benchmark-detail audit"; \
	fi
	@cd "$(CURDIR)" && \
	if rg -n 'python3 scripts/lib_parse_(generalized_cli|generalized_audit|metamath_generalized_compare|rho_generalized_compare)\.py' Makefile; then \
		echo "FAIL: lib_parse python integration-surface audit"; \
		exit 1; \
	else \
		echo "PASS: lib_parse python integration-surface audit"; \
	fi
	@cd "$(CURDIR)" && \
	if rg -n 'subprocess\.run' scripts/lib_parse_*py scripts/metamath_mmlean4_summary_oracle.py | grep -v 'scripts/lib_parse_metamath_native_probe_support.py'; then \
		echo "FAIL: lib_parse python subprocess bridge-scope audit"; \
		exit 1; \
	else \
		echo "PASS: lib_parse python subprocess bridge-scope audit"; \
	fi
	@cd "$(CURDIR)" && \
	if rg -n 'from metta_payload_io|import metta_payload_io|parse_sexprs|sexpr_tokenize' scripts tests; then \
		echo "FAIL: lib_parse payload parser scope audit"; \
		exit 1; \
	else \
		echo "PASS: lib_parse payload parser retired audit"; \
	fi
	@cd "$(CURDIR)" && \
	retired='scripts/lib_parse_metamath_lr_runtime.py scripts/lib_parse_metamath_lr_summary_oracle.py scripts/lib_parse_shared_witness.py scripts/lib_parse_gparse_native_runtime.py scripts/lib_parse_generalized_runtime.py scripts/lib_parse_generalized_backend_runtime.py scripts/lib_parse_generalized_cli.py scripts/lib_parse_generalized_audit.py scripts/lib_parse_metamath_generalized_compare.py scripts/lib_parse_metamath_frontier_probe.py scripts/lib_parse_metamath_prefix_frontier.py scripts/lib_parse_metamath_stmt_prefix_frontier.py scripts/lib_parse_metamath_theorem_length_ladder.py scripts/lib_parse_metamath_context_ladder.py scripts/lib_parse_metamath_context_theorem_matrix.py scripts/lib_parse_metamath_defs_theorem_length_ladder.py scripts/lib_parse_metamath_defs_component_ladder.py scripts/lib_parse_metamath_plus_weq_variant_matrix.py scripts/lib_parse_gparse_native_grammar.py scripts/lib_parse_native_replay_bridge.py scripts/lib_parse_metta_lexer_bridge.py scripts/lib_parse_rho_generalized_compare.py scripts/lib_parse_generalized_adapters.py scripts/lib_parse_generalized_adapter_examples.py scripts/lib_parse_metamath_token_adapter.py scripts/lib_parse_rho_token_adapter.py scripts/metta_payload_io.py'; \
	for f in $$retired; do \
		if ! rg -n 'Retired compatibility module|Retired Python prototype module|Retired Python integration surface|Retired CLI|Retired audit wrapper|Retired comparison wrapper|Retired oracle wrapper|Retired adapter' "$$f" >/dev/null; then \
			echo "FAIL: lib_parse retired python stubs ($$f)"; \
			exit 1; \
		elif ! rg -n 'RETIRED_MESSAGE' "$$f" >/dev/null; then \
			echo "FAIL: lib_parse retired python stub lacks fail-fast message ($$f)"; \
			exit 1; \
		elif ! rg -n 'raise SystemExit\(main\(\)\)' "$$f" >/dev/null; then \
			echo "FAIL: lib_parse retired python stub lacks fail-fast main ($$f)"; \
			exit 1; \
		elif rg -n '^(class |def )' "$$f" | grep -v 'def main() -> int:'; then \
			echo "FAIL: lib_parse retired python stub contains non-main code ($$f)"; \
			exit 1; \
		fi; \
	done; \
	echo "PASS: lib_parse retired python stubs"
	@cd "$(CURDIR)" && \
	expected_tmp=$$(mktemp); actual_tmp=$$(mktemp); \
	printf '%s\n' \
		scripts/lib_parse_generalized_adapter_examples.py \
		scripts/lib_parse_generalized_adapters.py \
		scripts/lib_parse_generalized_audit.py \
		scripts/lib_parse_generalized_backend_runtime.py \
		scripts/lib_parse_generalized_cli.py \
		scripts/lib_parse_generalized_runtime.py \
		scripts/lib_parse_gparse_native_grammar.py \
		scripts/lib_parse_gparse_native_runtime.py \
		scripts/lib_parse_metamath_context_ladder.py \
		scripts/lib_parse_metamath_context_theorem_matrix.py \
		scripts/lib_parse_metamath_defs_component_ladder.py \
		scripts/lib_parse_metamath_defs_theorem_length_ladder.py \
		scripts/lib_parse_metamath_frontier_probe.py \
		scripts/lib_parse_metamath_generalized_compare.py \
		scripts/lib_parse_metamath_lr_runtime.py \
		scripts/lib_parse_metamath_lr_summary_oracle.py \
		scripts/lib_parse_metamath_native_probe_support.py \
		scripts/lib_parse_metamath_plus_weq_variant_matrix.py \
		scripts/lib_parse_metamath_prefix_frontier.py \
		scripts/lib_parse_metamath_sealed_oracle.py \
		scripts/lib_parse_metamath_stmt_prefix_frontier.py \
		scripts/lib_parse_metamath_theorem_length_ladder.py \
		scripts/lib_parse_metamath_token_adapter.py \
		scripts/lib_parse_metta_lexer_bridge.py \
		scripts/lib_parse_native_replay_bridge.py \
		scripts/lib_parse_rho_generalized_compare.py \
		scripts/lib_parse_rho_token_adapter.py \
		scripts/lib_parse_shared_witness.py \
		scripts/metamath_mmlean4_summary_oracle.py \
		scripts/metta_payload_io.py \
		scripts/rhocalc_lib_parse_oracle.py | sort > "$$expected_tmp"; \
	find scripts -maxdepth 1 -type f \( -name 'lib_parse_*.py' -o -name 'metamath_mmlean4_summary_oracle.py' -o -name 'rhocalc_lib_parse_oracle.py' -o -name 'metta_payload_io.py' \) | sort > "$$actual_tmp"; \
	if ! cmp -s "$$expected_tmp" "$$actual_tmp"; then \
		echo "FAIL: lib_parse python inventory allowlist"; \
		diff -u "$$expected_tmp" "$$actual_tmp" || true; \
		rm -f "$$expected_tmp" "$$actual_tmp"; \
		exit 1; \
	fi; \
	rm -f "$$expected_tmp" "$$actual_tmp"; \
	echo "PASS: lib_parse python inventory allowlist"
	@cd "$(CURDIR)" && \
	for f in scripts/lib_parse_*py scripts/metamath_mmlean4_summary_oracle.py scripts/rhocalc_lib_parse_oracle.py scripts/metta_payload_io.py; do \
		if ! rg -n 'Retired |Porting-only|Oracle-only' "$$f" >/dev/null; then \
			echo "FAIL: lib_parse python role marker ($$f)"; \
			exit 1; \
		fi; \
	done; \
	echo "PASS: lib_parse python role markers"

test-lib-parse-generalized-toys: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_generalized_toys.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_generalized_toys.expected)" ]; then \
		echo "PASS: lib_parse generalized native toys"; \
	else \
		echo "FAIL: lib_parse generalized native toys"; \
		diff <(cat tests/test_gparse_native_generalized_toys.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-lib-parse-native-rho-mrho-text: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_rho_mrho_text.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_rho_mrho_text.expected)" ]; then \
		echo "PASS: lib_parse native rho mrho text"; \
	else \
		echo "FAIL: lib_parse native rho mrho text"; \
		diff <(cat tests/test_gparse_native_rho_mrho_text.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-lib-parse-generalized: $(BIN)
	@if $(MAKE) -s test-lib-parse-native-gparse; then \
		:; \
	else \
		exit 1; \
	fi
	@if $(MAKE) -s test-lib-parse-python-shadow-audit; then \
		:; \
	else \
		exit 1; \
	fi
	@if $(MAKE) -s test-lib-parse-generalized-native-integration; then \
		:; \
	else \
		exit 1; \
	fi
	@if $(MAKE) -s test-lib-parse-generalized-toys; then \
		:; \
	else \
		exit 1; \
	fi
	@if $(MAKE) -s test-lib-parse-native-rho-mrho-text; then \
		:; \
	else \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:glr-class \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/surface_shadowing.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^Unique$$'; then \
		echo "PASS: lib_parse generalized native corpus (rho glr unique shadowing)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho glr unique shadowing)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:gll-parse-shared \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/surface_shadowing.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^(Unique '; then \
		echo "PASS: lib_parse generalized native corpus (rho gll unique shadowing)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho gll unique shadowing)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:glr-class \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/surface_name_output.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^Unique$$'; then \
		echo "PASS: lib_parse generalized native corpus (rho glr unique name-output)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho glr unique name-output)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:gll-parse-shared \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/surface_name_output.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^(Unique '; then \
		echo "PASS: lib_parse generalized native corpus (rho gll unique name-output)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho gll unique name-output)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:glr-class \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/reject_quoted_payload.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^NoParse$$'; then \
		echo "PASS: lib_parse generalized native corpus (rho glr reject)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho glr reject)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:gll-parse-shared \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/reject_quoted_payload.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^NoParse$$'; then \
		echo "PASS: lib_parse generalized native corpus (rho gll reject)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho gll reject)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:glr-class \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/rotating_scheduler_persistent_branch.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^Ambiguous$$'; then \
		echo "PASS: lib_parse generalized native corpus (rho glr ambiguous)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho glr ambiguous)"; \
		echo "$$result"; \
		exit 1; \
	fi
	@result=$$("$(CETTA_SCRIPT_BIN)" -e "!(import! &self ./tests/support/rhocalc_lib_parse_translator_v3.metta)" -e "!(import! &self gparse)" -e "!(println! (gparse:gll-parse-shared \"tests/support/rhocalc_lib_parse_translator_v3.metta\" rho-g proc (rho-lex-file->toks \"tests/rhocalc/rotating_scheduler_persistent_branch.rho\")))" 2>&1 | grep -v '^Failed to create stream fd:'); \
	if echo "$$result" | grep -q '^Ambiguous$$'; then \
		echo "PASS: lib_parse generalized native corpus (rho gll ambiguous)"; \
	else \
		echo "FAIL: lib_parse generalized native corpus (rho gll ambiguous)"; \
		echo "$$result"; \
		exit 1; \
	fi

test-lib-parse-bounded: $(BIN)
	@pass=0; fail=0; \
	if $(CETTA_SCRIPT_RUN_ENV) python3 scripts/lib_parse_metamath_sealed_oracle.py "$(CETTA_SCRIPT_BIN)"; then \
		echo "PASS: lib_parse metamath mm-lean4 oracle"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: lib_parse metamath mm-lean4 oracle"; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_regression.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_lib_parse_regression.expected)" ]; then \
		echo "PASS: lib_parse core regression"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: lib_parse core regression"; \
		diff <(cat tests/test_lib_parse_regression.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_binding_regression.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_lib_parse_binding_regression.expected)" ]; then \
		echo "PASS: lib_parse binding regression"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: lib_parse binding regression"; \
		diff <(cat tests/test_lib_parse_binding_regression.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	if $(MAKE) -s test-lib-parse-abt-bridge; then \
		pass=$$((pass + 1)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	if $(MAKE) -s test-lib-parse-shared-cert; then \
		pass=$$((pass + 1)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	if $(MAKE) -s test-lib-parse-native-gparse; then \
		pass=$$((pass + 1)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	if $(MAKE) -s test-lib-parse-generalized; then \
		pass=$$((pass + 1)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	if $(MAKE) -s test-rhocalc-lib-parse-reference; then \
		pass=$$((pass + 1)); \
	else \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-lib-parse-abt-bridge: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_abt_bridge.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	summary='(LibParseABTBridgeSummary 25 25 0 admitted 18 rejected 5 preserved 2)'; \
	if [ "$$result" = "$$(cat tests/test_lib_parse_abt_bridge.expected)" ] && \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc "$$summary")" -eq 1 ]; then \
		echo "PASS: lib_parse ABT bridge (18 admitted, 5 rejected, 2 preserved)"; \
	else \
		echo "FAIL: lib_parse ABT bridge"; \
		diff <(cat tests/test_lib_parse_abt_bridge.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-frontier: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_frontier.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_frontier.expected)" ]; then \
		echo "PASS: gparse native Metamath frontier"; \
	else \
		echo "FAIL: gparse native Metamath frontier"; \
		diff <(cat tests/test_gparse_native_metamath_frontier.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-prefix-frontier: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_metamath_prefix_frontier_native.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_lib_parse_metamath_prefix_frontier_native.expected)" ]; then \
		echo "PASS: lib_parse Metamath prefix frontier native"; \
	else \
		echo "FAIL: lib_parse Metamath prefix frontier native"; \
		diff <(cat tests/test_lib_parse_metamath_prefix_frontier_native.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-stmt-prefix-frontier: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_metamath_stmt_prefix_frontier_native.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_lib_parse_metamath_stmt_prefix_frontier_native.expected)" ]; then \
		echo "PASS: lib_parse Metamath stmt prefix frontier native"; \
	else \
		echo "FAIL: lib_parse Metamath stmt prefix frontier native"; \
		diff <(cat tests/test_lib_parse_metamath_stmt_prefix_frontier_native.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-theorem-length-ladder: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_lib_parse_metamath_theorem_length_ladder_native.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_lib_parse_metamath_theorem_length_ladder_native.expected)" ]; then \
		echo "PASS: lib_parse Metamath theorem length ladder native"; \
	else \
		echo "FAIL: lib_parse Metamath theorem length ladder native"; \
		diff <(cat tests/test_lib_parse_metamath_theorem_length_ladder_native.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-context-ladder: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_context_ladder.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_context_ladder.expected)" ]; then \
		echo "PASS: gparse native Metamath context ladder"; \
	else \
		echo "FAIL: gparse native Metamath context ladder"; \
		diff <(cat tests/test_gparse_native_metamath_context_ladder.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-context-theorem-matrix: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_context_theorem_matrix.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_context_theorem_matrix.expected)" ]; then \
		echo "PASS: gparse native Metamath context theorem matrix"; \
	else \
		echo "FAIL: gparse native Metamath context theorem matrix"; \
		diff <(cat tests/test_gparse_native_metamath_context_theorem_matrix.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-defs-theorem-length-ladder: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_defs_theorem_length_ladder.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_defs_theorem_length_ladder.expected)" ]; then \
		echo "PASS: gparse native Metamath defs theorem-length ladder"; \
	else \
		echo "FAIL: gparse native Metamath defs theorem-length ladder"; \
		diff <(cat tests/test_gparse_native_metamath_defs_theorem_length_ladder.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-defs-component-ladder: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_defs_component_ladder.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_defs_component_ladder.expected)" ]; then \
		echo "PASS: gparse native Metamath defs component ladder"; \
	else \
		echo "FAIL: gparse native Metamath defs component ladder"; \
		diff <(cat tests/test_gparse_native_metamath_defs_component_ladder.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-lib-parse-metamath-plus-weq-variant-matrix: $(BIN)
	@result=$$("$(CETTA_SCRIPT_BIN)" tests/test_gparse_native_metamath_plus_weq_variant_matrix.metta 2>&1 | grep -v '^Failed to create stream fd:'); \
	if [ "$$result" = "$$(cat tests/test_gparse_native_metamath_plus_weq_variant_matrix.expected)" ]; then \
		echo "PASS: gparse native Metamath plus/weq variant matrix"; \
	else \
		echo "FAIL: gparse native Metamath plus/weq variant matrix"; \
		diff <(cat tests/test_gparse_native_metamath_plus_weq_variant_matrix.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

probe-core-lane: $(BIN)
	@for f in $(CORE_PROBE_TESTS); do \
		echo "PROBE: $$f"; \
		$(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f"; \
	done

test-heavy: $(BIN)
	@pass=0; fail=0; no_exp=0; \
	for f in $(BACKEND_HEAVY_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	summary="$$pass passed, $$fail failed"; \
	if [ $$no_exp -gt 0 ]; then summary="$$summary, $$no_exp no .expected file"; fi; \
	echo "$$summary"; \
	[ $$fail -eq 0 ]

test-heavy-golden: $(BIN)
	@pass=0; fail=0; \
	for f in $(BACKEND_HEAVY_GOLDEN_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "FAIL: $$f (missing .expected file in golden lane)"; \
			fail=$$((fail + 1)); \
			continue; \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

list-heavy-diagnostics:
	@echo "heavy golden tests:"; \
	for f in $(BACKEND_HEAVY_GOLDEN_TESTS); do echo "  $$f"; done; \
	echo; \
	echo "heavy diagnostic probes:"; \
	for f in $(BACKEND_HEAVY_DIAGNOSTIC_TESTS); do echo "  $$f"; done

probe-heavy-diagnostics: $(BIN)
	@count=0; \
	for f in $(BACKEND_HEAVY_DIAGNOSTIC_TESTS); do \
		count=$$((count + 1)); \
		echo "DIAGNOSTIC: $$f"; \
		set +e; \
		result=$$(timeout "$${CETTA_HEAVY_DIAGNOSTIC_TIMEOUT:-300}" $(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		status=$$?; \
		set -e; \
		printf '%s\n' "$$result" | head -40; \
		echo "DIAGNOSTIC-STATUS: $$status"; \
	done; \
	echo "---"; \
	echo "$$count heavy diagnostic probes"

test-correctness-all:
	@$(MAKE) -s BUILD=$(BUILD_CANON) test
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-prime-nik-qualification-v1
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-prime-all
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mam-symbolic-battery
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mam-held-symbolic
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mam-jetta-smoke
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-heavy-golden
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats

test-runtime-stats:
	@if [ "$(ENABLE_RUNTIME_STATS)" != "1" ]; then \
		echo "INFO: runtime-stats test lane requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"; \
		$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-lane-body; \
	else \
		$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-lane-body; \
	fi

test-runtime-stats-lane: test-runtime-stats

test-runtime-stats-lane-body:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-term-universe-store-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-term-universe-backend-add-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-cli
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-rhocalc-runtime-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-closed-stream-runtime-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-eval-gc-survivor-reset
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prime-need-heap-index-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prime-need-planner-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prime-relational-plan-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prepared-pure-call-machine-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-he-prepared-program-cache-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prime-prepared-control-program-cache-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prime-prepared-match-decision-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-petta-specialized-pure-call-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-petta-prepared-program-cache-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-petta-prepared-collection-pull-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-petta-libpl-runtime-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-metta-suite
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" = "1" ] || [ -n "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-lane-body; \
	fi
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-runtime-stats-lane-body
endif

test-runtime-stats-metta-suite:
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@pass=0; fail=0; no_exp=0; \
	for f in $(RUNTIME_STATS_METTA_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	summary="$$pass passed, $$fail failed"; \
	if [ $$no_exp -gt 0 ]; then summary="$$summary, $$no_exp no .expected file"; fi; \
	echo "$$summary"; \
	[ $$fail -eq 0 ]
else
	@echo "INFO: runtime-stats MeTTa suite requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-lib-prolog-runtime-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
ifeq ($(LIB_PROLOG_ENABLED),1)
	@for lang in he prime; do \
		idle_stats=$$(mktemp runtime/lib-prolog-$$lang-idle.XXXXXX); \
		query_out=$$(mktemp runtime/lib-prolog-$$lang-query.XXXXXX); \
		query_stats=$$(mktemp runtime/lib-prolog-$$lang-stats.XXXXXX); \
		trap 'rm -f "$$idle_stats" "$$query_out" "$$query_stats"' \
			EXIT INT TERM; \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang "$$lang" \
			-e '!(+ 1 2)' >/dev/null 2>"$$idle_stats"; \
		idle_claim=$$(awk '$$1 == "runtime-counter" && \
			$$2 == "lib-prolog-engine-claim" { print $$3 }' \
			"$$idle_stats"); \
		idle_release=$$(awk '$$1 == "runtime-counter" && \
			$$2 == "lib-prolog-engine-release" { print $$3 }' \
			"$$idle_stats"); \
		idle_prepare=$$(awk '$$1 == "runtime-counter" && \
			$$2 == "lib-prolog-prepare" { print $$3 }' \
			"$$idle_stats"); \
		if [ "$$idle_claim" -ne 0 ] || [ "$$idle_release" -ne 0 ] || \
			[ "$$idle_prepare" -ne 0 ]; then \
			echo "FAIL: $$lang prepared Prolog without a query"; \
			exit 1; \
		fi; \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang "$$lang" \
			tests/lib_prolog_surface.metta \
			>"$$query_out" 2>"$$query_stats"; \
		diff -u tests/lib_prolog_surface.he-prime.expected "$$query_out"; \
		query_claim=$$(awk '$$1 == "runtime-counter" && \
			$$2 == "lib-prolog-engine-claim" { print $$3 }' \
			"$$query_stats"); \
		query_release=$$(awk '$$1 == "runtime-counter" && \
			$$2 == "lib-prolog-engine-release" { print $$3 }' \
			"$$query_stats"); \
		query_prepare=$$(awk '$$1 == "runtime-counter" && \
			$$2 == "lib-prolog-prepare" { print $$3 }' \
			"$$query_stats"); \
		if [ -z "$$query_prepare" ] || [ "$$query_prepare" -le 0 ] || \
			[ "$$query_claim" -ne "$$query_release" ]; then \
			echo "FAIL: $$lang Prolog lifecycle was not lazy and balanced"; \
			exit 1; \
		fi; \
		rm -f "$$idle_stats" "$$query_out" "$$query_stats"; \
		trap - EXIT INT TERM; \
	done; \
	echo "PASS: HE and Prime prepare Prolog lazily with balanced worker claims"
else
	@echo "SKIP: shared lib_prolog runtime stats (adapter disabled)"
endif
else
	@echo "INFO: shared lib_prolog runtime stats require ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif
.PHONY: test-lib-prolog-runtime-stats

test-petta-libpl-runtime-stats: test-lib-prolog-runtime-stats $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
ifeq ($(LIB_PROLOG_ENABLED),1)
	@native_out=$$(mktemp runtime/petta-libpl-negative.XXXXXX); \
	native_stats=$$(mktemp runtime/petta-libpl-negative-stats.XXXXXX); \
	foreign_out=$$(mktemp runtime/petta-libpl-worker.XXXXXX); \
	foreign_stats=$$(mktemp runtime/petta-libpl-worker-stats.XXXXXX); \
	trap 'rm -f "$$native_out" "$$native_stats" "$$foreign_out" "$$foreign_stats"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 $(CETTA_BIN_INVOKE) \
		--emit-runtime-stats --lang petta \
		tests/petta/libpl_negative_admission_hyperpose.metta \
		>"$$native_out" 2>"$$native_stats"; \
	if ! diff -u tests/petta/libpl_negative_admission_hyperpose.expected \
			"$$native_out"; then \
		echo "FAIL: PeTTa native-head libpl admission result"; \
		exit 1; \
	fi; \
	negative=$$(awk '$$1 == "runtime-counter" && \
		$$2 == "petta-libpl-admission-negative" { print $$3 }' \
		"$$native_stats"); \
	claim=$$(awk '$$1 == "runtime-counter" && \
		$$2 == "lib-prolog-engine-claim" { print $$3 }' \
		"$$native_stats"); \
	release=$$(awk '$$1 == "runtime-counter" && \
		$$2 == "lib-prolog-engine-release" { print $$3 }' \
		"$$native_stats"); \
	if [ -z "$$negative" ] || [ "$$negative" -le 0 ] || \
			[ "$$claim" -ne 0 ] || [ "$$release" -ne 0 ]; then \
		echo "FAIL: native heads crossed the optional Prolog boundary"; \
		echo "negative=$$negative claim=$$claim release=$$release"; \
		exit 1; \
	fi; \
	CETTA_PETTA_SEARCH_MACHINE=1 $(CETTA_BIN_INVOKE) \
		--emit-runtime-stats --lang petta --num-threads 2 \
		tests/petta/foreign_predicate_hyperpose.metta \
		>"$$foreign_out" 2>"$$foreign_stats"; \
	if ! diff -u tests/petta/foreign_predicate_hyperpose.expected \
			"$$foreign_out"; then \
		echo "FAIL: PeTTa libpl worker result under runtime stats"; \
		exit 1; \
	fi; \
	claim=$$(awk '$$1 == "runtime-counter" && \
		$$2 == "lib-prolog-engine-claim" { print $$3 }' \
		"$$foreign_stats"); \
	release=$$(awk '$$1 == "runtime-counter" && \
		$$2 == "lib-prolog-engine-release" { print $$3 }' \
		"$$foreign_stats"); \
	if [ -z "$$claim" ] || [ "$$claim" -le 0 ] || \
			[ "$$claim" -ne "$$release" ]; then \
		echo "FAIL: libpl worker engine claims were not balanced"; \
		echo "claim=$$claim release=$$release"; \
		exit 1; \
	fi; \
	echo "PASS: PeTTa libpl negative admission and pooled worker engine"
else
	@echo "SKIP: PeTTa libpl runtime stats (adapter disabled)"
endif
else
	@echo "INFO: PeTTa libpl runtime stats require ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif
.PHONY: test-petta-libpl-runtime-stats

perf-list:
	@./scripts/run_witness.sh --list
	@echo "native_fc_memory_probe (runtime stats): make ENABLE_RUNTIME_STATS=1 probe-fc-native-memory"
	@echo "rhocalc_threaded: make bench-rho-threaded"
	@echo "rhocalc_threaded_corpus: make bench-rho-threaded-corpus"
	@echo "rhocalc_threaded_generated: make bench-rho-threaded-generated"
	@echo "rhocalc_threaded_generated_runtime_stats: make ENABLE_RUNTIME_STATS=1 bench-rho-threaded-generated-runtime-stats"
	@echo "rhocalc_cost_threaded: make bench-rho-cost-threaded"
	@echo "rhocalc_cost_threaded_heavy: make bench-rho-cost-threaded-heavy"

perf-show-baselines:
	@./scripts/run_witness.sh --show-baselines

perf-bench-rhocalc:
	@for name in rhocalc_threaded_standard rhocalc_cost_threaded_heavy; do \
		if ! out=$$(./scripts/compare_witness.sh --enforce-material "$$name"); then \
			printf '%s\n' "$$out"; exit 1; \
		fi; \
		printf '%s\n' "$$out"; \
		status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
		test "$$status" = "pass" || exit 1; \
	done
	@python3 scripts/rhocalc_benchmark_regression.py

test-main-readiness-model:
	@python3 scripts/test_cetta_readiness_model.py
	@python3 scripts/test_rhocalc_paired_samples.py

main-readiness-space-ladders:
	@out=$$(./scripts/run_witness.sh main_readiness_space_ladders); \
	printf '%s\n' "$$out"; \
	status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
	test "$$status" = "pass"

main-readiness-thresholds:
	@out=$$(./scripts/run_witness.sh main_readiness_thresholds); \
	printf '%s\n' "$$out"; \
	status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
	test "$$status" = "pass"

main-readiness-mutation-qualification:
	@out=$$(./scripts/run_witness.sh main_readiness_mutation_qualification); \
	printf '%s\n' "$$out"; \
	status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
	evidence=$$(printf '%s\n' "$$out" | awk -F= '/^WITNESS_EVIDENCE_STATUS=/{ print $$2; exit }'); \
	test "$$status" = "pass" && test "$$evidence" = "passed"

main-readiness-rho-adaptive:
	@if [ -z "$(RHO_BENCH_BASELINE_BIN)" ]; then \
		echo "RHO_BENCH_BASELINE_BIN is required for adaptive paired timing" >&2; \
		exit 2; \
	fi
	@for name in rhocalc_threaded_adaptive rhocalc_cost_threaded_adaptive; do \
		out=$$(./scripts/run_witness.sh "$$name"); \
		printf '%s\n' "$$out"; \
		status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
		test "$$status" = "pass" || exit 1; \
	done
	@python3 scripts/rhocalc_benchmark_regression.py

main-readiness-routine:
	@python3 scripts/cetta_main_readiness.py --tier routine

main-readiness-routine-authoritative:
	@python3 scripts/cetta_main_readiness.py --tier routine
	@python3 scripts/cetta_readiness_calibrate.py --require-qualified

main-readiness-exhaustive:
	@BENCH_ALLOW_HEAVY=1 python3 scripts/cetta_main_readiness.py --tier exhaustive

main-readiness-calibration-status:
	@python3 scripts/cetta_readiness_calibrate.py --status

main-readiness-calibrate:
	@if [ "$(MAIN_READINESS_ALLOW_EXHAUSTIVE)" != "1" ]; then \
		echo "MAIN_READINESS_ALLOW_EXHAUSTIVE=1 is required" >&2; \
		exit 2; \
	fi
	@args=""; \
	if [ -n "$(MAIN_READINESS_MAX_NEW_PAIRS)" ]; then \
		args="--max-new-pairs $(MAIN_READINESS_MAX_NEW_PAIRS)"; \
	fi; \
	python3 scripts/cetta_readiness_calibrate.py --allow-exhaustive $$args

main-readiness-cost-rho: main-readiness-exhaustive

perf-capacity-tu:
	@./scripts/run_witness.sh tu_tail_special_forms
	@./scripts/run_witness.sh tu_tilepuzzle

perf-bench-tu:
	@out=$$(./scripts/run_witness.sh tu_fc_d3_variant); \
	printf '%s\n' "$$out"; \
	status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
	test "$$status" = "pass"

perf-compare-tu:
	@./scripts/compare_witness.sh tu_fc_d3_variant
	@./scripts/compare_witness.sh tu_tail_special_forms
	@./scripts/compare_witness.sh tu_tilepuzzle

list:
	@awk -F'\t' -v lane="$(LANE)" 'NR>1 && (lane=="" || $$7==lane) {printf "%-28s %s\n", $$7, $$1}' tests/test_manifest.tsv

bench-index:
	@{ printf 'kind\tname_or_path\tinfo\n'; \
	awk -F'\t' 'NR>1{print "witness\t"$$1"\t"$$2}' benchmarks/witness_catalog.tsv; \
	awk -F'\t' 'NR>1 && $$1 ~ /^benchmarks\// {print "heavy\t"$$1"\t"$$8}' tests/test_manifest.tsv; \
	for f in benchmarks/bench_*.metta benchmarks/prime/bench_*.metta; do \
		[ -f "$$f" ] && printf 'driver\t%s\t-\n' "$$f"; \
	done; } > benchmarks/INDEX.tsv
	@wc -l benchmarks/INDEX.tsv

test-manifest test-manifest-check:
	@./scripts/sync_test_manifest.py --check

test-manifest-sync:
	@./scripts/sync_test_manifest.py --write

test-manifest-strict: test-manifest-check

test-forbidden-availability-errors:
	@python3 scripts/check_forbidden_availability_errors.py

test-he-prime-search-mutation: $(BIN)
	@mutation_dir=runtime/he-prime-search-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_he_prime_search_cap_one.py src/he_typing.c "$$mutation_dir/he_typing.c"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" -o "$$mutation_dir/he_typing.o"; \
	$(CC) $(filter-out src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) "$$mutation_dir/he_typing.o" -o "$$mutation_dir/cetta-cap-one" $(LDFLAGS); \
	baseline=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_search_enumeration.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/profile_he_prime_search_enumeration.expected)" ]; then \
		echo "FAIL: typed-search mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-cap-one" --profile he-prime --lang he tests/profile_he_prime_search_enumeration.metta 2>&1); \
	if [ "$$mutant" = "$$baseline" ] || printf '%s\n' "$$mutant" | grep -Fq '(MakeC a2 b1)' || ! printf '%s\n' "$$mutant" | grep -Fq '(MakeC a1 b1)'; then \
		echo "FAIL: premise-cap-one mutation survived its semantic gate"; exit 1; \
	fi; \
	echo "PASS: premise-cap-one mutation is killed by proof-enumeration gates"

test-he-prime-scheme-mutation: $(BIN)
	@mutation_dir=runtime/he-prime-scheme-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_he_prime_implicit_scheme.py src/he_typing.c "$$mutation_dir/he_typing.c"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" -o "$$mutation_dir/he_typing.o"; \
	$(CC) $(filter-out src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) "$$mutation_dir/he_typing.o" -o "$$mutation_dir/cetta-implicit-scheme" $(LDFLAGS); \
	baseline=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_explicit_schemes.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/profile_he_prime_explicit_schemes.expected)" ]; then \
		echo "FAIL: explicit-scheme mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-implicit-scheme" --profile he-prime --lang he tests/profile_he_prime_explicit_schemes.metta 2>&1); \
	baseline_rigid=$$(printf '%s\n' "$$baseline" | sed -n '1p'); \
	mutant_rigid=$$(printf '%s\n' "$$mutant" | sed -n '1p'); \
	if [ "$$baseline_rigid" != '[(he-reject (no-inhabitant-at-depth))]' ] || \
	   [ "$$mutant_rigid" = "$$baseline_rigid" ] || \
	   ! printf '%s\n' "$$mutant_rigid" | grep -Fq '(he-accept '; then \
		echo "FAIL: implicit-scheme mutation survived its rigid-variable gate"; exit 1; \
	fi; \
	echo "PASS: implicit-scheme mutation is killed by rigid-variable gates"

test-prime-unbounded-search-mutation: $(BIN)
	@mutation_dir=runtime/prime-unbounded-search-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_unbounded_search.py \
		src/he_typing.c "$$mutation_dir/he_typing.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" \
		-o "$$mutation_dir/he_typing.o" || exit 1; \
	$(CC) $(filter-out src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/he_typing.o" -o "$$mutation_dir/cetta-bounded-only" \
		$(LDFLAGS) || exit 1; \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/conformance/unbounded_search.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime/conformance/unbounded_search.expected)" ]; then \
		echo "FAIL: unbounded-search mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-bounded-only" --lang prime \
		tests/prime/conformance/unbounded_search.metta 2>&1); \
	if [ "$$mutant" = "$$baseline" ] || \
	   [ "$$(printf '%s\n' "$$mutant" | grep -Fxc '[(he-unknown (fuel-exhausted))]')" -lt 2 ]; then \
		echo "FAIL: bounded-only mutation survived the unbounded-search gate"; exit 1; \
	fi; \
	echo "PASS: bounded-only mutation is killed by resource-omission gates"

test-prime-occurs-check-mutation: $(BIN)
	@mutation_dir=runtime/prime-occurs-check-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_occurs_check.py \
		src/match.c "$$mutation_dir/match.c" \
		src/he_typing.c "$$mutation_dir/he_typing.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/match.c" \
		-o "$$mutation_dir/match.o" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" \
		-o "$$mutation_dir/he_typing.o" || exit 1; \
	base_objects='$(filter-out src/match.$(BUILD_OBJ_TAG).o src/match.$(BUILD_OBJ_TAG).runtime-stats.o src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ))'; \
	he_object='$(filter src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ))'; \
	$(CC) $$base_objects "$$mutation_dir/match.o" "$$he_object" \
		-o "$$mutation_dir/cetta-corrupt-producer" $(LDFLAGS) || exit 1; \
	$(CC) $$base_objects "$$mutation_dir/match.o" \
		"$$mutation_dir/he_typing.o" \
		-o "$$mutation_dir/cetta-no-occurs-check" $(LDFLAGS) || exit 1; \
	fixture=tests/prime/conformance/occurs_check.metta; \
	expected=$$(cat tests/prime/conformance/occurs_check.expected); \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime "$$fixture" 2>&1); \
	if [ "$$baseline" != "$$expected" ]; then \
		echo "FAIL: occurs-check mutation baseline is not green"; exit 1; \
	fi; \
	producer=$$("$$mutation_dir/cetta-corrupt-producer" \
		--lang prime "$$fixture" 2>&1); \
	if [ "$$producer" != "$$expected" ]; then \
		echo "FAIL: checker accepted a cyclic substitution from a corrupt producer"; \
		exit 1; \
	fi; \
	unsound=$$("$$mutation_dir/cetta-no-occurs-check" \
		--lang prime "$$fixture" 2>&1); \
	if [ "$$unsound" = "$$expected" ] || \
	   ! printf '%s\n' "$$unsound" | grep -Fq '[CyclicSearchUnsound]'; then \
		echo "FAIL: combined occurs-check mutation survived its soundness gate"; \
		exit 1; \
	fi; \
	echo "PASS: replay rejects a corrupt producer and the occurs-check mutation is killed"

test-prime-completion-mutation: $(BIN)
	@mutation_dir=runtime/prime-completion-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_completion_gate.py src/prime_semantics.c "$$mutation_dir/prime_semantics.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/prime_semantics.c" -o "$$mutation_dir/prime_semantics.o" || exit 1; \
	$(CC) $(filter-out src/prime_semantics.$(BUILD_OBJ_TAG).o src/prime_semantics.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) "$$mutation_dir/prime_semantics.o" -o "$$mutation_dir/cetta-forged-completion" $(LDFLAGS) || exit 1; \
	baseline=$$(timeout $(PRIME_COMPLETION_TIMEOUT) $(CETTA_BIN_INVOKE) --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime_02_completion_resources.expected)" ]; then \
		echo "FAIL: Prime completion mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$(timeout $(PRIME_COMPLETION_TIMEOUT) "$$mutation_dir/cetta-forged-completion" --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	baseline_delayed=$$(printf '%s\n' "$$baseline" | grep -F '(DelayedLow '); \
	mutant_delayed=$$(printf '%s\n' "$$mutant" | grep -F '(DelayedLow '); \
	if [ "$$baseline_delayed" != '[(DelayedLow Incomplete)]' ] || \
	   [ "$$mutant_delayed" != '[(DelayedLow Established)]' ]; then \
		echo "FAIL: completion-gate mutation survived delayed ambiguity"; exit 1; \
	fi; \
	echo "PASS: answer-bag completion mutation is killed by delayed counterexample"

test-prime-delayed-ambiguity-mutation: $(BIN) $(PRIME_DELAYED_AMBIGUITY_TEST_BIN)
	@mutation_dir=runtime/prime-delayed-ambiguity-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_delayed_ambiguity.py \
		src/he_typing.c "$$mutation_dir/he_typing.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" -o "$$mutation_dir/he_typing.o" || exit 1; \
	$(CC) $(PRIME_DELAYED_AMBIGUITY_TEST_OBJ) \
		$(filter-out src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(PRIME_DELAYED_AMBIGUITY_TEST_LINK_OBJ)) \
		"$$mutation_dir/he_typing.o" -o "$$mutation_dir/test-false-unique" $(LDFLAGS) || exit 1; \
	baseline=$$("$(PRIME_DELAYED_AMBIGUITY_TEST_BIN)" 2>&1); baseline_rc=$$?; \
	mutant=$$("$$mutation_dir/test-false-unique" 2>&1); mutant_rc=$$?; \
	baseline_line=$$(printf '%s\n' "$$baseline" | grep '^low=' | head -n 1); \
	mutant_line=$$(printf '%s\n' "$$mutant" | grep '^low=' | head -n 1); \
	if [ "$$baseline_rc" -ne 0 ] || \
	   [ "$$baseline_line" != 'low=resource high=ambiguous' ] || \
	   [ "$$mutant_rc" -eq 0 ] || \
	   [ "$$mutant_line" != 'low=complete high=ambiguous' ]; then \
		printf '%s\n' "baseline: $$baseline" "mutant: $$mutant"; \
		echo "FAIL: normalization-completion mutation survived delayed ambiguity"; exit 1; \
	fi; \
	echo "PASS: normalization-completion mutation is killed by delayed TagA/TagB ambiguity"

test-prime-variable-mutation: $(BIN)
	@mutation_dir=runtime/prime-variable-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_variable_discipline.py \
		src/he_typing.c "$$mutation_dir/he_typing.c" \
		src/prime_semantics.c "$$mutation_dir/prime_semantics.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" -o "$$mutation_dir/he_typing.o" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/prime_semantics.c" -o "$$mutation_dir/prime_semantics.o" || exit 1; \
	$(CC) $(filter-out src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o src/prime_semantics.$(BUILD_OBJ_TAG).o src/prime_semantics.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/he_typing.o" "$$mutation_dir/prime_semantics.o" \
		-o "$$mutation_dir/cetta-variable-unsound" $(LDFLAGS) || exit 1; \
	baseline=$$(timeout $(PRIME_COMPLETION_TIMEOUT) $(CETTA_BIN_INVOKE) --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime_02_completion_resources.expected)" ]; then \
		echo "FAIL: Prime variable mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$(timeout $(PRIME_COMPLETION_TIMEOUT) "$$mutation_dir/cetta-variable-unsound" --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	baseline_probes=$$(printf '%s\n' "$$baseline" | sed -n '1,3p'); \
	mutant_probes=$$(printf '%s\n' "$$mutant" | sed -n '1,3p'); \
	if [ "$$baseline_probes" = "$$mutant_probes" ] || \
	   ! printf '%s\n' "$$mutant_probes" | grep -Fq '(OpenExpected Established)' || \
	   ! printf '%s\n' "$$mutant_probes" | grep -Fq '(UnconstrainedDeclaration Established)' || \
	   ! printf '%s\n' "$$mutant_probes" | grep -Fq '(EscapedBinder Undetermined)'; then \
		echo "FAIL: variable-discipline mutation survived or bypassed canonical scope"; exit 1; \
	fi; \
	echo "PASS: bind-and-discard/implicit-formation mutation is killed; canonical scope independently blocks escape"

test-prime-canonical-binder-mutation: $(BIN) $(PRIME_PACKAGE_VALIDATION_TEST_BIN)
	@mutation_dir=runtime/prime-canonical-binder-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_canonical_binders.py \
		src/prime_semantics.c "$$mutation_dir/prime_semantics.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/prime_semantics.c" \
		-o "$$mutation_dir/prime_semantics.o" || exit 1; \
	$(CC) $(PRIME_PACKAGE_VALIDATION_TEST_OBJ) \
		$(filter-out src/prime_semantics.$(BUILD_OBJ_TAG).o src/prime_semantics.$(BUILD_OBJ_TAG).runtime-stats.o,$(PRIME_PACKAGE_VALIDATION_TEST_LINK_OBJ)) \
		"$$mutation_dir/prime_semantics.o" \
		-o "$$mutation_dir/test-captured-free-name" $(LDFLAGS) || exit 1; \
	baseline=$$("$(PRIME_PACKAGE_VALIDATION_TEST_BIN)" 2>&1); baseline_rc=$$?; \
	mutant=$$("$$mutation_dir/test-captured-free-name" 2>&1); mutant_rc=$$?; \
	if [ "$$baseline_rc" -ne 0 ] || \
	   [ "$$mutant_rc" -eq 0 ] || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'free same-spelling variable was captured'; then \
		printf '%s\n' "baseline: $$baseline" "mutant: $$mutant"; \
		echo "FAIL: canonical-binder capture mutation survived"; exit 1; \
	fi; \
	echo "PASS: canonical-binder mutation is killed by the free same-spelling capture falsifier"

test-prime-abt-chain-mutation: $(BIN)
	@mutation_dir=runtime/prime-abt-chain-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_abt_chain.py \
		src/eval.c "$$mutation_dir/eval.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/eval.c" \
		-o "$$mutation_dir/eval.o" || exit 1; \
	$(CC) $(filter-out src/eval.$(BUILD_OBJ_TAG).o src/eval.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/eval.o" -o "$$mutation_dir/cetta-chain-capture" \
		$(LDFLAGS) || exit 1; \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/conformance/abt_chain_scope.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime/conformance/abt_chain_scope.expected)" ]; then \
		echo "FAIL: Prime ABT chain mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-chain-capture" --lang prime \
		tests/prime/conformance/abt_chain_scope.metta 2>&1); \
	if [ "$$mutant" = "$$baseline" ] || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'Expected: []' || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'Got: [(pair a)]'; then \
		printf '%s\n' "$$mutant" | tail -20; \
		echo "FAIL: Prime ABT chain refinement mutation survived"; exit 1; \
	fi; \
	echo "PASS: Prime ABT chain mutation is killed by repeated-name refinement"

test-prime-abt-let-mutation: $(BIN)
	@mutation_dir=runtime/prime-abt-let-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_abt_let.py \
		src/eval.c "$$mutation_dir/eval.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/eval.c" \
		-o "$$mutation_dir/eval.o" || exit 1; \
	$(CC) $(filter-out src/eval.$(BUILD_OBJ_TAG).o src/eval.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/eval.o" -o "$$mutation_dir/cetta-let-order" \
		$(LDFLAGS) || exit 1; \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/conformance/abt_let_scope.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime/conformance/abt_let_scope.expected)" ]; then \
		echo "FAIL: Prime ABT let mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-let-order" --lang prime \
		tests/prime/conformance/abt_let_scope.metta 2>&1); \
	if [ "$$mutant" = "$$baseline" ] || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'Expected: [(got a b), (got a b)]' || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'Got: [(got b a), (got b a)]'; then \
		printf '%s\n' "$$mutant" | tail -24; \
		echo "FAIL: Prime ABT let slot-order mutation survived"; exit 1; \
	fi; \
	echo "PASS: Prime ABT let mutation is killed by positional slot ordering"

test-prime-abt-sealed-mutation: $(BIN)
	@mutation_dir=runtime/prime-abt-sealed-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_abt_sealed.py \
		src/match.c "$$mutation_dir/match.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/match.c" \
		-o "$$mutation_dir/match.o" || exit 1; \
	$(CC) $(filter-out src/match.$(BUILD_OBJ_TAG).o src/match.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/match.o" -o "$$mutation_dir/cetta-sealed-ignore" \
		$(LDFLAGS) || exit 1; \
	baseline=$$($(CETTA_BIN_INVOKE) --lang prime \
		tests/prime/conformance/abt_sealed_boundary.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime/conformance/abt_sealed_boundary.expected)" ]; then \
		echo "FAIL: Prime ABT sealed mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$("$$mutation_dir/cetta-sealed-ignore" --lang prime \
		tests/prime/conformance/abt_sealed_boundary.metta 2>&1); \
	if [ "$$mutant" = "$$baseline" ] || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'Expected: [(pair True False)]' || \
	   ! printf '%s\n' "$$mutant" | grep -Fq 'Got: [(pair False False)]'; then \
		printf '%s\n' "$$mutant" | tail -24; \
		echo "FAIL: Prime ABT sealed ignore-list mutation survived"; exit 1; \
	fi; \
	echo "PASS: Prime ABT sealed mutation is killed by exclusion identity"

test-prime-applicability-capacity-mutation: $(BIN)
	@mutation_dir=runtime/prime-applicability-capacity-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_applicability_capacity.py \
		src/eval.c "$$mutation_dir/eval.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/eval.c" -o "$$mutation_dir/eval.o" || exit 1; \
	$(CC) $(filter-out src/eval.$(BUILD_OBJ_TAG).o src/eval.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/eval.o" -o "$$mutation_dir/cetta-silent-applicability-cap" $(LDFLAGS) || exit 1; \
	baseline=$$(timeout $(PRIME_COMPLETION_TIMEOUT) $(CETTA_BIN_INVOKE) --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime_02_completion_resources.expected)" ]; then \
		echo "FAIL: Prime applicability-capacity mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$(timeout $(PRIME_COMPLETION_TIMEOUT) "$$mutation_dir/cetta-silent-applicability-cap" --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	baseline_cap=$$(printf '%s\n' "$$baseline" | grep -F '[(DynamicApplicability ' | head -1); \
	mutant_cap=$$(printf '%s\n' "$$mutant" | grep -F '[(DynamicApplicability ' | head -1); \
	if [ "$$baseline_cap" != '[(DynamicApplicability Established)]' ] || \
	   [ "$$mutant_cap" != '[(DynamicApplicability Incomplete)]' ]; then \
		echo "FAIL: fixed applicability-frontier mutation survived its gate"; exit 1; \
	fi; \
	echo "PASS: reintroduced 64-entry applicability frontier is killed by 81-way Must"

test-prime-type-capacity-mutation: $(BIN)
	@mutation_dir=runtime/prime-type-capacity-mutation; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_prime_type_capacity.py \
		src/he_typing.c "$$mutation_dir/he_typing.c" || exit 1; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$mutation_dir/he_typing.c" -o "$$mutation_dir/he_typing.o" || exit 1; \
	$(CC) $(filter-out src/he_typing.$(BUILD_OBJ_TAG).o src/he_typing.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/he_typing.o" -o "$$mutation_dir/cetta-fixed-type-frontier" $(LDFLAGS) || exit 1; \
	baseline=$$(timeout $(PRIME_COMPLETION_TIMEOUT) $(CETTA_BIN_INVOKE) --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	if [ "$$baseline" != "$$(cat tests/prime_02_completion_resources.expected)" ]; then \
		echo "FAIL: Prime type-storage mutation baseline is not green"; exit 1; \
	fi; \
	mutant=$$(timeout $(PRIME_COMPLETION_TIMEOUT) "$$mutation_dir/cetta-fixed-type-frontier" --lang prime tests/prime_02_completion_resources.metta 2>&1); \
	baseline_types=$$(printf '%s\n' "$$baseline" | grep -F '[(DynamicTypeStorage ' | head -1); \
	mutant_types=$$(printf '%s\n' "$$mutant" | grep -F '[(DynamicTypeStorage ' | head -1); \
	if [ "$$baseline_types" != '[(DynamicTypeStorage Established)]' ] || \
	   [ "$$mutant_types" != '[(DynamicTypeStorage Incomplete)]' ]; then \
		echo "FAIL: fixed inferred-type frontier mutation survived its gate"; exit 1; \
	fi; \
	echo "PASS: reintroduced 64-entry inferred-type frontier is killed by 81-type synthesis"

test-prime-budget-monotonicity: $(BIN)
	@python3 scripts/check_prime_budget_monotonicity.py "$(abspath $(BIN))"

.PHONY: test-prime-type-langdef-source-binding-v1
test-prime-type-langdef-source-binding-v1:
	@python3 tools/gslt2parse_schema_v1.py \
		langdef/prime/generated/closed_formation_v1.metta >/dev/null
	@mkdir -p $(BOOTSTRAP_TMPDIR); \
	tmpdir=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/prime-type-binding.XXXXXX"); \
	trap 'rm -f "$$tmpdir/binding.h" "$$tmpdir/binding.c" "$$tmpdir/binding-normalized.c"; rmdir "$$tmpdir"' EXIT INT TERM; \
	python3 tools/generate_nik_direct_source_binding_v1.py \
		--presentation langdef/prime/generated/closed_formation_v1.metta \
		--symbol-prefix prime_typing_closed_formation \
		--authority-symbol cetta_prime_typing_direct_authority_v1 \
		--authority-header prime_semantics.h \
		--semantic-scope prime.typing.closed-formation \
		--coverage fragment \
		--header-output "$$tmpdir/binding.h" \
		--source-output "$$tmpdir/binding.c"; \
	sed 's#generated/binding.h#generated/prime_typing_closed_formation_source_binding_v1.generated.h#' \
		"$$tmpdir/binding.c" > "$$tmpdir/binding-normalized.c"; \
	cmp -s "$$tmpdir/binding.h" \
		src/generated/prime_typing_closed_formation_source_binding_v1.generated.h; \
	cmp -s "$$tmpdir/binding-normalized.c" \
		src/generated/prime_typing_closed_formation_source_binding_v1.generated.c; \
	rm -f "$$tmpdir/binding-normalized.c"; \
	echo "PASS: Prime type langdef source binding is valid and deterministic"

test-prime-package-validation: $(PRIME_PACKAGE_VALIDATION_TEST_BIN) test-prime-type-langdef-source-binding-v1
	@"$(PRIME_PACKAGE_VALIDATION_TEST_BIN)"

test-prime-coverage:
	@python3 scripts/check_prime_coverage.py

test-prime-crossdialect: $(BIN)
	@python3 scripts/check_prime_crossdialect.py "$(abspath $(BIN))"

test-prime-internal-graduality: $(BIN)
	@python3 scripts/check_prime_internal_graduality.py "$(abspath $(BIN))"

test-prime-practical: $(BIN)
	@pass=0; fail=0; \
	for f in $(PRIME_PRACTICAL_TESTS); do \
		exp="$${f%.metta}.expected"; \
		result=$$($(CETTA_BIN_INVOKE) --lang prime "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -30; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "Prime practical gate: $$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

.PHONY: test-prime-relational-plan
test-prime-relational-plan: $(BIN)
	@set -eu; \
	guard=tests/prime/relational_plan_guard.metta; \
	expected=$$(cat tests/prime/relational_plan_guard.expected); \
	canonical=$$(CETTA_PRIME_RELATIONAL_PLAN_REFERENCE=1 \
		$(CETTA_BIN_INVOKE) --lang prime "$$guard" 2>&1); \
	probe=$$($(CETTA_BIN_INVOKE) --lang prime "$$guard" 2>&1); \
	slot_reference=$$(CETTA_PETTA_CLAUSE_SLOT_FRAME_REFERENCE=1 \
		$(CETTA_BIN_INVOKE) --lang prime "$$guard" 2>&1); \
	if [ "$$canonical" != "$$expected" ] || \
	   [ "$$probe" != "$$expected" ] || \
	   [ "$$slot_reference" != "$$expected" ]; then \
		echo "FAIL: Prime guarded relation plan changed demand/effect/fault behavior"; \
		diff <(printf '%s\n' "$$slot_reference") <(printf '%s\n' "$$probe") | head -40; \
		exit 1; \
	fi; \
	if [ "$$(printf '%s\n' "$$probe" | grep -c '^Marker$$' || true)" -ne 1 ] || \
	   printf '%s\n' "$$probe" | grep -q 'SHOULD-NOT-PRINT'; then \
		echo "FAIL: Prime guarded relation plan leaked or duplicated an effect"; \
		exit 1; \
	fi; \
	fuel=tests/prime/relational_plan_fuel.metta; \
	canonical_fuel=$$(CETTA_PRIME_RELATIONAL_PLAN_REFERENCE=1 \
		$(CETTA_BIN_INVOKE) --fuel 3 --lang prime "$$fuel" 2>&1); \
	probe_fuel=$$($(CETTA_BIN_INVOKE) --fuel 3 --lang prime "$$fuel" 2>&1); \
	slot_reference_fuel=$$(CETTA_PETTA_CLAUSE_SLOT_FRAME_REFERENCE=1 \
		$(CETTA_BIN_INVOKE) --fuel 3 --lang prime "$$fuel" 2>&1); \
	if [ "$$canonical_fuel" != "$$probe_fuel" ] || \
	   [ "$$slot_reference_fuel" != "$$probe_fuel" ] || \
	   printf '%s\n' "$$probe_fuel" | grep -q '\[done\]'; then \
		echo "FAIL: Prime guarded relation plan laundered bounded/open execution"; \
		exit 1; \
	fi; \
	dependent=tests/profile_he_prime_dtt_typed_corpus.metta; \
	dependent_expected=$$(cat tests/profile_he_prime_dtt_typed_corpus.expected); \
	dependent_canonical=$$(CETTA_PRIME_RELATIONAL_PLAN_REFERENCE=1 \
		$(CETTA_BIN_INVOKE) --lang prime "$$dependent" 2>&1); \
	dependent_probe=$$($(CETTA_BIN_INVOKE) --lang prime "$$dependent" 2>&1); \
	dependent_slot_reference=$$(CETTA_PETTA_CLAUSE_SLOT_FRAME_REFERENCE=1 \
		$(CETTA_BIN_INVOKE) --lang prime "$$dependent" 2>&1); \
	if [ "$$dependent_canonical" != "$$dependent_expected" ] || \
	   [ "$$dependent_probe" != "$$dependent_expected" ] || \
	   [ "$$dependent_slot_reference" != "$$dependent_expected" ]; then \
		echo "FAIL: Prime relation plan changed dependent telescope dispatch"; \
		diff <(printf '%s\n' "$$dependent_canonical") \
		     <(printf '%s\n' "$$dependent_probe") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: Prime relation plans preserve value, demand, effect, fault, rollback, and fuel boundaries"

.PHONY: test-prime-relational-plan-stats
test-prime-relational-plan-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -eu; \
	stats=$$($(CETTA_BIN_INVOKE) --emit-runtime-stats --lang prime \
		tests/prime/relational_plan_guard.metta 2>&1 >/dev/null); \
	safe=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && \
		 $$2 == "prime-relational-plan-replay-safe-argument" { print $$3 }'); \
	if [ -z "$$safe" ] || [ "$$safe" -lt 1 ]; then \
		echo "FAIL: Prime relation-plan replay-safe positive witness was not admitted"; \
		exit 1; \
	fi; \
	echo "PASS: Prime relation plan admitted a bounded replay-safe argument"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-prime: $(BIN) test-prime-coverage test-prime-budget-monotonicity test-prime-package-validation test-prime-internal-graduality test-prime-nik-core-v1
	@pass=0; fail=0; \
	for f in $(PRIME_FAST_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "FAIL: $$f (missing $$exp)"; fail=$$((fail + 1)); continue; \
		fi; \
		result=$$(timeout $(PRIME_COMPLETION_TIMEOUT) $(CETTA_BIN_INVOKE) --lang prime "$$f" 2>&1); \
		if [ $$? -eq 124 ]; then \
			echo "FAIL: $$f (exceeded PRIME_COMPLETION_TIMEOUT=$(PRIME_COMPLETION_TIMEOUT)s)"; \
			fail=$$((fail + 1)); continue; \
		fi; \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -30; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "Prime fast gate: $$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-prime-all: test-prime test-prime-relational-plan test-prime-need-algebra \
	test-prime-need-he-noninterference \
	test-prime-need-correspondence \
	test-prime-need-gc-lifetime \
	test-prime-need-boundaries \
	test-prime-suspension-rights \
	test-prime-contexts \
	test-prime-context-tutorial \
	test-prime-need-effect-isolation \
	test-prime-need-equation-choice-sharing \
	test-prime-equation-call-sharing-tournament \
	test-prime-evaluation-strategy-contrast \
	test-prime-need-mutations \
	test-prime-crossdialect test-prime-universal-name-surface \
	test-prime-universal-name-resolver \
	test-prime-universal-name-mutation \
	test-prime-syntax-mutation \
	test-prime-universal-name-metadata \
	test-prime-universal-name-metadata-mutation \
	test-prime-universal-name-syntax-gslt \
	test-prime-unbounded-search-mutation \
	test-prime-occurs-check-mutation \
	test-prime-completion-mutation \
	test-prime-delayed-ambiguity-mutation \
	test-prime-variable-mutation \
	test-prime-canonical-binder-mutation \
	test-prime-abt-chain-mutation \
	test-prime-abt-let-mutation \
	test-prime-abt-sealed-mutation \
	test-prime-applicability-capacity-mutation \
	test-prime-type-capacity-mutation
	@echo "PASS: full Prime correctness gate"

test-profiles: $(BIN) test-manifest test-forbidden-availability-errors test-git-module-profiles test-symbolid-guard test-fallback-eval-session test-he-compiled-reader-v1 test-petta-compiled-reader-v1 test-prime-compiled-reader-v1 test-import-modes test-he-prime-search-mutation test-he-prime-scheme-mutation test-prime-crossdialect test-prime-need-he-noninterference
	@pass=0; fail=0; \
	cache_dir="$(GIT_TEST_CACHE_DIR)"; mkdir -p "$$cache_dir"; export CETTA_GIT_MODULE_CACHE_DIR="$$cache_dir"; \
	profiles=$$($(CETTA_BIN_INVOKE) --list-profiles 2>&1); \
	if printf '%s\n' "$$profiles" | grep -Eq '^he[[:space:]]' && \
	   printf '%s\n' "$$profiles" | grep -Eq '^he-compat[[:space:]]' && \
	   printf '%s\n' "$$profiles" | grep -Eq '^he-extended[[:space:]]' && \
	   printf '%s\n' "$$profiles" | grep -Eq '^he-prime[[:space:]]'; then \
		echo "PASS: profile inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: profile inventory"; \
		printf '%s\n' "$$profiles"; \
		fail=$$((fail + 1)); \
	fi; \
	petta_profiles=$$($(CETTA_BIN_INVOKE) --lang petta --list-profiles 2>&1); \
	if printf '%s\n' "$$petta_profiles" | grep -Eq '^extended[[:space:]]'; then \
		echo "PASS: PeTTa extended profile inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: PeTTa extended profile inventory"; \
		printf '%s\n' "$$petta_profiles"; \
		fail=$$((fail + 1)); \
	fi; \
	base_result=$$($(CETTA_BIN_INVOKE) --lang he tests/spec_profile_once_alias_extension.metta 2>&1); \
	if printf '%s\n' "$$base_result" | grep -Fq "(once "; then \
		echo "PASS: he base leaves uninterpreted extension calls inert"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he base leaves uninterpreted extension calls inert"; \
		printf '%s\n' "$$base_result"; \
		fail=$$((fail + 1)); \
	fi; \
	mm2_profiles=$$($(CETTA_BIN_INVOKE) --lang mm2 --list-profiles 2>&1); \
	if [ "$$(printf '%s\n' "$$mm2_profiles" | wc -l)" -eq 1 ] && \
	   printf '%s\n' "$$mm2_profiles" | grep -Eq '^gslt[[:space:]]'; then \
		echo "PASS: mm2 GSLT profile inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2 GSLT profile inventory"; \
		printf '%s\n' "$$mm2_profiles"; \
		fail=$$((fail + 1)); \
	fi; \
	mm2_profile_err=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile he-compat -e '()' 2>&1 || true); \
	if printf '%s\n' "$$mm2_profile_err" | grep -Fq "error: unknown source profile 'he-compat' for language 'mm2'"; then \
		echo "PASS: mm2 rejects foreign profiles"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2 rejects foreign profiles"; \
		printf '%s\n' "$$mm2_profile_err"; \
		fail=$$((fail + 1)); \
	fi; \
	rhocalc_profiles=$$($(CETTA_BIN_INVOKE) --lang rhocalc --list-profiles 2>&1); \
	if printf '%s\n' "$$rhocalc_profiles" | grep -Eq '^strict-core[[:space:]]' && \
	   printf '%s\n' "$$rhocalc_profiles" | grep -Eq '^cost[[:space:]]'; then \
		echo "PASS: rhocalc profile inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc profile inventory"; \
		printf '%s\n' "$$rhocalc_profiles"; \
		fail=$$((fail + 1)); \
	fi; \
	rhocalc_strict=$$($(CETTA_BIN_INVOKE) --lang rhocalc --profile strict-core -e 'rho:nil' 2>&1); \
	if [ "$$rhocalc_strict" = "rho:nil" ]; then \
		echo "PASS: rhocalc strict-core profile"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc strict-core profile"; \
		printf '%s\n' "$$rhocalc_strict"; \
		fail=$$((fail + 1)); \
	fi; \
	rhocalc_cost=$$($(CETTA_BIN_INVOKE) --lang rhocalc --profile cost --syntax rho -e '{for ($$m <- pay) {{0}cont}}alice | {pay!({0}payload)}bob | purse pay {alice : ()} | purse pay {bob : ()}' 2>&1); \
	if [ "$$rhocalc_cost" = "purse pay {()} | purse pay {()} | {0}cont" ]; then \
		echo "PASS: rhocalc cost profile slice"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: rhocalc cost profile slice"; \
		printf '%s\n' "$$rhocalc_cost"; \
		fail=$$((fail + 1)); \
	fi; \
	he_lts=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_lts_he_surface.metta 2>&1); \
	if [ "$$he_lts" = "$$(cat tests/test_lts_he_surface.expected)" ]; then \
		echo "PASS: he-extended lts:he surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended lts:he surface"; \
		diff <(cat tests/test_lts_he_surface.expected) <(echo "$$he_lts") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	formal_eval=$$($(CETTA_BIN_INVOKE) --profile he --lang he tests/test_eval_grounded.metta 2>&1); \
	if [ "$$formal_eval" = "$$(cat tests/test_eval_grounded.expected)" ]; then \
		echo "PASS: formal he eval surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: formal he eval surface"; \
		diff <(cat tests/test_eval_grounded.expected) <(echo "$$formal_eval") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	formal_no_return=$$($(CETTA_BIN_INVOKE) --profile he --lang he tests/test_no_return_error.metta 2>&1); \
	if [ "$$formal_no_return" = "$$(cat tests/test_no_return_error.expected)" ]; then \
		echo "PASS: formal he no-return surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: formal he no-return surface"; \
		diff <(cat tests/test_no_return_error.expected) <(echo "$$formal_no_return") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	formal_docs=$$($(CETTA_BIN_INVOKE) --profile he --lang he tests/he_g1_docs.metta 2>&1); \
	if [ "$$formal_docs" = "$$(cat tests/he_g1_docs.expected)" ]; then \
		echo "PASS: formal he documentation surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: formal he documentation surface"; \
		diff <(cat tests/he_g1_docs.expected) <(echo "$$formal_docs") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	for profile in he-compat he-extended he-prime; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/test_import_modules.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/test_import_modules.expected)" ]; then \
			echo "PASS: $$profile import modules"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile import modules"; \
			diff <(cat tests/test_import_modules.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_count_atoms.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/spec_profile_count_atoms.expected)" ]; then \
		echo "PASS: he-extended count-atoms extension"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended count-atoms extension"; \
		diff <(cat tests/spec_profile_count_atoms.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_count_atoms.metta 2>&1); \
	if printf '%s\n' "$$result" | grep -Fq "(count-atoms "; then \
		echo "PASS: he-compat count-atoms Rust-inert surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat count-atoms Rust-inert surface"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_new_space_kind_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_new_space_kind_compat.expected)" ]; then \
		echo "PASS: he-compat new-space kind overload is hidden"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat new-space kind overload is hidden"; \
		diff <(cat tests/support/profile_new_space_kind_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_bind_error_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_bind_error_compat.expected)" ]; then \
		echo "PASS: he-compat bind! propagates generated initializer errors"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat bind! propagates generated initializer errors"; \
		diff <(cat tests/support/profile_bind_error_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	if $(CETTA_BIN_INVOKE) --profile he --lang he tests/test_deep_tail_if_constructor_regression.metta >/dev/null 2>&1 && \
	   $(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/test_deep_tail_if_constructor_regression.metta >/dev/null 2>&1; then \
		echo "PASS: assertion diagnostics handle large failure terms"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: assertion diagnostics handle large failure terms"; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/support/profile_new_space_kind_extended.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_new_space_kind_extended.expected)" ]; then \
		echo "PASS: he-extended new-space kind overload is visible"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended new-space kind overload is visible"; \
		diff <(cat tests/support/profile_new_space_kind_extended.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_core_surface_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_core_surface_compat.expected)" ]; then \
		echo "PASS: he-compat core-surface extensions are hidden"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat core-surface extensions are hidden"; \
		diff <(cat tests/support/profile_core_surface_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_filter_atom_compat_error.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_filter_atom_compat_error.expected)" ]; then \
		echo "PASS: he-compat filter-atom helper overload is hidden"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat filter-atom helper overload is hidden"; \
		diff <(cat tests/support/profile_filter_atom_compat_error.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_include_space_target_compat_error.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_include_space_target_compat_error.expected)" ]; then \
		echo "PASS: he-compat include target overload is hidden"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat include target overload is hidden"; \
		diff <(cat tests/support/profile_include_space_target_compat_error.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_include_compat_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_include_compat_surface.expected)" ]; then \
		echo "PASS: he-compat include Rust result surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat include Rust result surface"; \
		diff <(cat tests/support/profile_include_compat_surface.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_if_compat_arity.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_if_compat_arity.expected)" ]; then \
		echo "PASS: he-compat if two-argument extension is hidden"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat if two-argument extension is hidden"; \
		diff <(cat tests/support/profile_if_compat_arity.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_numeric_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_numeric_compat.expected)" ]; then \
		echo "PASS: he-compat numeric Rust behavior"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat numeric Rust behavior"; \
		diff <(cat tests/support/profile_numeric_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_math_domain_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_math_domain_compat.expected)" ]; then \
		echo "PASS: he-compat math-domain Rust behavior"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat math-domain Rust behavior"; \
		diff <(cat tests/support/profile_math_domain_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_parse_compat_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_parse_compat_surface.expected)" ]; then \
		echo "PASS: he-compat parse Rust surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat parse Rust surface"; \
		diff <(cat tests/support/profile_parse_compat_surface.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he --lang he tests/support/profile_numeric_formal.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_numeric_formal.expected)" ]; then \
		echo "PASS: formal he numeric surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: formal he numeric surface"; \
		diff <(cat tests/support/profile_numeric_formal.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_get_doc_compat_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_get_doc_compat_surface.expected)" ]; then \
		echo "PASS: he-compat get-doc Rust surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat get-doc Rust surface"; \
		diff <(cat tests/support/profile_get_doc_compat_surface.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he --lang he tests/support/profile_get_doc_formal_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_get_doc_surface.expected)" ]; then \
		echo "PASS: formal he get-doc surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: formal he get-doc surface"; \
		diff <(cat tests/support/profile_get_doc_surface.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/support/profile_get_doc_extended_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/support/profile_get_doc_surface.expected)" ]; then \
		echo "PASS: he-extended get-doc surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended get-doc surface"; \
		diff <(cat tests/support/profile_get_doc_surface.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_size_extension.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/spec_profile_size_extension.expected)" ]; then \
		echo "PASS: he-extended size extension"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended size extension"; \
		diff <(cat tests/spec_profile_size_extension.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_size_extension.metta 2>&1); \
	if printf '%s\n' "$$result" | grep -Fq "(size "; then \
		echo "PASS: he-compat size Rust-inert surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat size Rust-inert surface"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_size_extension.metta >/dev/null 2>&1; then \
		echo "PASS: he-compat size compile leaves extension call inert"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat size compile leaves extension call inert"; \
		fail=$$((fail + 1)); \
	fi; \
	if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_size_extension.metta >/dev/null 2>&1; then \
		echo "PASS: he-extended size compile"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended size compile"; \
		fail=$$((fail + 1)); \
	fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_foldl_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_foldl_extension.expected)" ]; then \
			echo "PASS: he-extended foldl-atom-in-space extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended foldl-atom-in-space extension"; \
		diff <(cat tests/spec_profile_foldl_extension.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_foldl_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(foldl-atom-in-space "; then \
			echo "PASS: he-compat foldl-atom-in-space Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat foldl-atom-in-space Rust-inert surface"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_foldl_public.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/spec_profile_foldl_public.expected)" ]; then \
		echo "PASS: he-compat foldl-atom Rust core surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat foldl-atom Rust core surface"; \
		diff <(cat tests/spec_profile_foldl_public.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_collect_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_collect_extension.expected)" ]; then \
			echo "PASS: he-extended collect extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended collect extension"; \
			diff <(cat tests/spec_profile_collect_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_collect_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(collect "; then \
			echo "PASS: he-compat collect Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat collect Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_select_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_select_extension.expected)" ]; then \
			echo "PASS: he-extended select extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended select extension"; \
			diff <(cat tests/spec_profile_select_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_select_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(select "; then \
			echo "PASS: he-compat select Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat select Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_fold_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_fold_extension.expected)" ]; then \
			echo "PASS: he-extended fold extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended fold extension"; \
			diff <(cat tests/spec_profile_fold_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_fold_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(fold "; then \
			echo "PASS: he-compat fold Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat fold Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_fold_by_key_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_fold_by_key_extension.expected)" ]; then \
			echo "PASS: he-extended fold-by-key extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended fold-by-key extension"; \
			diff <(cat tests/spec_profile_fold_by_key_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_fold_by_key_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(fold-by-key "; then \
			echo "PASS: he-compat fold-by-key Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat fold-by-key Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_reduce_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_reduce_extension.expected)" ]; then \
			echo "PASS: he-extended reduce extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended reduce extension"; \
			diff <(cat tests/spec_profile_reduce_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_reduce_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(reduce "; then \
			echo "PASS: he-compat reduce Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat reduce Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_runtime_stats_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_runtime_stats_extension.expected)" ]; then \
			echo "PASS: he-extended runtime-stats extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended runtime-stats extension"; \
			diff <(cat tests/spec_profile_runtime_stats_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_runtime_stats_runtime.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(runtime-stats!)"; then \
			echo "PASS: he-compat runtime-stats Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat runtime-stats Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_once_alias_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_once_alias_extension.expected)" ]; then \
			echo "PASS: he-extended once alias"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended once alias"; \
			diff <(cat tests/spec_profile_once_alias_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_once_alias_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(once "; then \
			echo "PASS: he-compat once Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat once Rust-inert surface"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_hyperpose_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_hyperpose_extension.expected)" ]; then \
			echo "PASS: he-extended hyperpose extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended hyperpose extension"; \
			diff <(cat tests/spec_profile_hyperpose_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/spec_profile_hyperpose_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_hyperpose_extension.expected)" ]; then \
			echo "PASS: he-prime hyperpose extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-prime hyperpose extension"; \
			diff <(cat tests/spec_profile_hyperpose_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he -e '!(hyperpose (profile-ok))' 2>&1); \
		if [ "$$result" = "[(hyperpose (profile-ok))]" ]; then \
			echo "PASS: he-compat hyperpose non-reduction"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat hyperpose non-reduction"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he \
			-e '!(bind! &profile-hyperpose-inert (new-space))' \
			-e '!(collapse (hyperpose ((add-atom &profile-hyperpose-inert touched) ok)))' \
			-e '!(assertEqual (collapse (match &profile-hyperpose-inert $$x $$x)) ())' 2>&1); \
		expected=$$'[()]\n[((hyperpose ((add-atom &profile-hyperpose-inert touched) ok)))]\n[()]'; \
		if [ "$$result" = "$$expected" ]; then \
			echo "PASS: he-compat inactive hyperpose arguments stay unevaluated"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat inactive hyperpose arguments stay unevaluated"; \
			diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		base_result=$$($(CETTA_BIN_INVOKE) --lang he -e '!(hyperpose (profile-ok))' 2>&1); \
		if [ "$$base_result" = "[(hyperpose (profile-ok))]" ]; then \
			echo "PASS: he base hyperpose non-reduction"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he base hyperpose non-reduction"; \
			printf '%s\n' "$$base_result"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_hyperpose_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile hyperpose pass-through"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile hyperpose pass-through"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_hyperpose_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile hyperpose"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile hyperpose"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he --num-threads 2 tests/support/hyperpose_cli_threads.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/support/hyperpose_cli_threads.expected)" ]; then \
			echo "PASS: he-extended hyperpose CLI threads"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended hyperpose CLI threads"; \
			diff <(cat tests/support/hyperpose_cli_threads.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_search_policy_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_search_policy_extension.expected)" ]; then \
			echo "PASS: he-extended search-policy capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended search-policy capability"; \
			diff <(cat tests/spec_profile_search_policy_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_search_policy_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(search-policy "; then \
			echo "PASS: he-compat search-policy Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat search-policy Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_search_policy_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile search-policy leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile search-policy leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_search_policy_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile search-policy"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile search-policy"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_profile_space_set_match_backend_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_space_set_match_backend_extension.expected)" ]; then \
			echo "PASS: he-extended space-set-match-backend! extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended space-set-match-backend! extension"; \
			diff <(cat tests/spec_profile_space_set_match_backend_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/spec_profile_space_set_match_backend_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "(space-set-"; then \
			echo "PASS: he-compat space-set-match-backend! Rust-inert surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat space-set-match-backend! Rust-inert surface"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_space_set_match_backend_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile space-set-match-backend! leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile space-set-match-backend! leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_space_set_match_backend_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile space-set-match-backend!"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile space-set-match-backend!"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile count-atoms leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile count-atoms leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile extension"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_collect_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile collect leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile collect leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_collect_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile collect"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile collect"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_select_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile select leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile select leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_select_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile select"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile select"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_fold_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile fold leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile fold leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_fold_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile fold"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile fold"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_fold_by_key_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile fold-by-key leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile fold-by-key leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_fold_by_key_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile fold-by-key"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile fold-by-key"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_reduce_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile reduce leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile reduce leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_reduce_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile reduce"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile reduce"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_runtime_stats_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-compat compile runtime-stats leaves extension call inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-compat compile runtime-stats leaves extension call inert"; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_runtime_stats_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he-extended compile runtime-stats"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he-extended compile runtime-stats"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/spec_module_inventory.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_module_inventory.expected)" ]; then \
			echo "PASS: he-extended module-inventory extension"; pass=$$((pass + 1)); \
		else \
		echo "FAIL: he-extended module-inventory extension"; \
		diff <(cat tests/spec_module_inventory.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-compat --lang he tests/support/profile_module_inventory_runtime.metta 2>&1); \
	if printf '%s\n' "$$result" | grep -Fq "(module-inventory!)"; then \
		echo "PASS: he-compat module-inventory Rust-inert surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat module-inventory Rust-inert surface"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	for profile in he-compat he-extended he-prime; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/spec_profile_system_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_system_extension.expected)" ]; then \
			echo "PASS: $$profile system capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile system capability"; \
			diff <(cat tests/spec_profile_system_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile "$$profile" --compile tests/support/profile_compile_system_extension.metta >/dev/null 2>&1; then \
			echo "PASS: $$profile compile system capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile compile system capability"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for profile in he-compat he-extended he-prime; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/spec_profile_fs_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_fs_extension.expected)" ]; then \
			echo "PASS: $$profile fs capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile fs capability"; \
			diff <(cat tests/spec_profile_fs_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile "$$profile" --compile tests/support/profile_compile_fs_extension.metta >/dev/null 2>&1; then \
			echo "PASS: $$profile compile fs capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile compile fs capability"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for profile in he-compat he-extended he-prime; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/spec_profile_str_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_str_extension.expected)" ]; then \
			echo "PASS: $$profile str capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile str capability"; \
			diff <(cat tests/spec_profile_str_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		if $(CETTA_BIN_INVOKE) --profile "$$profile" --compile tests/support/profile_compile_str_extension.metta >/dev/null 2>&1; then \
			echo "PASS: $$profile compile str capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile compile str capability"; \
			fail=$$((fail + 1)); \
		fi; \
		done; \
	for profile in he he-compat he-extended he-prime; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/profile_he_shared_typing_conformance.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/profile_he_shared_typing_conformance.expected)" ]; then \
			echo "PASS: $$profile shared HE typing conformance"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile shared HE typing conformance"; \
			diff <(cat tests/profile_he_shared_typing_conformance.expected) <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for profile in he he-compat he-extended; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/profile_he_shared_type_level_query.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/profile_he_shared_type_level_query_residual.expected)" ]; then \
			echo "PASS: $$profile leaves type-level user computation residual"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile leaves type-level user computation residual"; \
			diff <(cat tests/profile_he_shared_type_level_query_residual.expected) <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
		inert=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/profile_he_prime_extensions_inert.metta 2>&1); \
		if [ "$$(printf '%s\n' "$$inert" | grep -Fc '(check-type-refinements ')" -eq 1 ] && \
		   [ "$$(printf '%s\n' "$$inert" | grep -Fc '(check-type ')" -eq 1 ] && \
		   [ "$$(printf '%s\n' "$$inert" | grep -Fc '(search-first-inhabitant ')" -eq 1 ] && \
		   ! printf '%s\n' "$$inert" | grep -Eq 'he-accept|he-reject|he-unknown|unsupported|Error'; then \
			echo "PASS: $$profile leaves he-prime extensions inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile leaves he-prime extensions inert"; printf '%s\n' "$$inert"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_shared_type_level_query.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_shared_type_level_query_computed.expected)" ]; then \
		echo "PASS: he-prime normal get-type performs checked type-level computation"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime normal get-type performs checked type-level computation"; \
		diff <(cat tests/profile_he_shared_type_level_query_computed.expected) <(echo "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/profile_he_prime_dependent_binders_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dependent_binders_compat.expected)" ]; then \
		echo "PASS: he-extended keeps literal binder-domain behavior"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended keeps literal binder-domain behavior"; \
		diff <(cat tests/profile_he_prime_dependent_binders_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_dependent_binders.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dependent_binders.expected)" ]; then \
		echo "PASS: he-prime dependent binder telescope"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime dependent binder telescope"; \
		diff <(cat tests/profile_he_prime_dependent_binders.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_type_formation.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_type_formation.expected)" ]; then \
		echo "PASS: he-prime declared-constructor type formation"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime declared-constructor type formation"; \
		diff <(cat tests/profile_he_prime_type_formation.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_dtt_typed_corpus.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dtt_typed_corpus.expected)" ]; then \
		echo "PASS: he-prime DTT typed seed corpus"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime DTT typed seed corpus"; \
		diff <(cat tests/profile_he_prime_dtt_typed_corpus.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_dtt_tutorial_ladder.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dtt_tutorial_ladder.expected)" ]; then \
		echo "PASS: he-prime DTT tutorial ladder"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime DTT tutorial ladder"; \
		diff <(cat tests/profile_he_prime_dtt_tutorial_ladder.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_recursive_search.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_recursive_search.expected)" ]; then \
		echo "PASS: he-prime recursive dependent search"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime recursive dependent search"; \
		diff <(cat tests/profile_he_prime_recursive_search.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_structural_eq.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_structural_eq.expected)" ]; then \
		echo "PASS: he-prime structural == policy"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime structural == policy"; \
		diff <(cat tests/profile_he_prime_structural_eq.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_dtt_chainer_showcase.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dtt_chainer_showcase.expected)" ]; then \
		echo "PASS: he-prime DTT-chainer showcase"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime DTT-chainer showcase"; \
		diff <(cat tests/profile_he_prime_dtt_chainer_showcase.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_dtt_adversarial.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dtt_adversarial.expected)" ]; then \
		echo "PASS: he-prime DTT discipline adversarial negatives"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime DTT discipline adversarial negatives"; \
		diff <(cat tests/profile_he_prime_dtt_adversarial.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_search_enumeration.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_search_enumeration.expected)" ]; then \
		echo "PASS: he-prime finite-fixture typed-search enumeration"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime finite-fixture typed-search enumeration"; \
		diff <(cat tests/profile_he_prime_search_enumeration.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_explicit_schemes.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_explicit_schemes.expected)" ]; then \
		echo "PASS: he-prime explicit schemes and elaboration receipts"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime explicit schemes and elaboration receipts"; \
		diff <(cat tests/profile_he_prime_explicit_schemes.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_search_fuel_prefix.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_search_fuel_prefix.expected)" ]; then \
		echo "PASS: he-prime typed-search fuel-prefix preservation"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime typed-search fuel-prefix preservation"; \
		diff <(cat tests/profile_he_prime_search_fuel_prefix.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$($(CETTA_BIN_INVOKE) --profile he-prime --lang he tests/profile_he_prime_search_pln_multi.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_search_pln_multi.expected)" ]; then \
		echo "PASS: he-prime recursive PLN multi-answer prefix"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-prime recursive PLN multi-answer prefix"; \
		diff <(cat tests/profile_he_prime_search_pln_multi.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	for profile in he he-compat he-extended; do \
		result=$$($(CETTA_BIN_INVOKE) --profile "$$profile" --lang he tests/profile_he_prime_search_fuel_prefix.metta 2>&1); \
		count=$$(printf '%s\n' "$$result" | grep -Fc '(search-inhabitants ' || true); \
		if [ "$$count" -eq 2 ] && ! printf '%s\n' "$$result" | grep -Eq 'typing-search|he-reject|Error'; then \
			echo "PASS: $$profile leaves he-prime typed search inert"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile leaves he-prime typed search inert"; printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	if $(CETTA_BIN_INVOKE) --profile he-compat --compile tests/support/profile_compile_module_inventory.metta >/dev/null 2>&1; then \
		echo "PASS: he-compat module-inventory compile leaves extension call inert"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-compat module-inventory compile leaves extension call inert"; \
		fail=$$((fail + 1)); \
	fi; \
	if $(CETTA_BIN_INVOKE) --profile he-extended --compile tests/support/profile_compile_module_inventory.metta >/dev/null 2>&1; then \
		echo "PASS: he-extended compile module-inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he-extended compile module-inventory"; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-fallback-eval-session: $(FALLBACK_EVAL_TEST_BIN)
	@result=$$(./$(FALLBACK_EVAL_TEST_BIN) 2>&1); \
	expected='[(once 1), (once 2)]'; \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: fallback eval session leaves uninterpreted extension call inert"; \
	else \
		echo "FAIL: fallback eval session leaves uninterpreted extension call inert"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		exit 1; \
	fi

.PHONY: test-he-compiled-reader-v1
test-he-compiled-reader-v1: $(HE_COMPILED_READER_TEST_BIN) $(BIN)
	@result=$$(./$(HE_COMPILED_READER_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | \
		grep -Fxc '(HECompiledReaderV1Summary 47 47 0 routed-text 1 routed-file 1)')" -ne 1 ]; then \
		echo "FAIL: compiled HE reader exact integration summary absent or duplicated"; \
		exit 1; \
	fi; \
	if ./$(BIN) --lang he -e '$$' >/dev/null 2>runtime/test-he-compiled-reader-route.err; then \
		echo "FAIL: HE CLI bypassed the compiled reader's bare-variable rejection"; \
		exit 1; \
	fi; \
	if ! grep -Fq 'compiled HE reader' runtime/test-he-compiled-reader-route.err; then \
		echo "FAIL: HE CLI rejection did not originate at the compiled reader"; \
		exit 1; \
	fi; \
	for profile in he he-compat he-extended he-prime; do \
		if ./$(BIN) --lang he --profile "$$profile" -e '$$' \
				>/dev/null 2>runtime/test-he-compiled-reader-route.err; then \
			echo "FAIL: $$profile bypassed compiled bare-variable rejection"; \
			exit 1; \
		fi; \
		if ! grep -Fq 'compiled HE reader' \
				runtime/test-he-compiled-reader-route.err; then \
			echo "FAIL: $$profile rejection did not originate at compiled reader"; \
			exit 1; \
		fi; \
	done; \
	echo "PASS: all four HE profiles select the compiled LanguageDef reader"

.PHONY: test-petta-compiled-reader-v1
test-petta-compiled-reader-v1: test-petta-compiled-reader-direct-generated-v1 $(PETTA_COMPILED_READER_TEST_BIN) $(BIN)
	@result=$$(./$(PETTA_COMPILED_READER_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | \
		grep -Fxc '(PeTTaCompiledReaderV1Summary 24 24 0 source-pass 1 two-stage 1)')" -ne 1 ]; then \
		echo "FAIL: compiled PeTTa reader exact integration summary absent or duplicated"; \
		exit 1; \
	fi; \
	text_result=$$(./$(BIN) --lang petta -e '!(+ 1 2)' 2>&1); \
	if [ "$$text_result" != '3' ]; then \
		echo "FAIL: --lang petta text path did not use the compiled reader"; \
		printf '%s\n' "$$text_result"; \
		exit 1; \
	fi; \
	file_result=$$(./$(BIN) --lang petta \
		tests/support/petta_compiled_reader_cli.metta 2>&1); \
	if [ "$$file_result" != '5' ]; then \
		echo "FAIL: --lang petta file path did not use the compiled reader"; \
		printf '%s\n' "$$file_result"; \
		exit 1; \
	fi; \
	if ./$(BIN) --lang petta -e 'bare-top-token' \
			>/dev/null 2>runtime/test-petta-compiled-reader-route.err; then \
		echo "FAIL: --lang petta bypassed its document grammar"; \
		exit 1; \
	fi; \
	if ! grep -Fq 'compiled PeTTa reader' \
			runtime/test-petta-compiled-reader-route.err; then \
		echo "FAIL: PeTTa rejection did not originate at the compiled reader"; \
		exit 1; \
	fi; \
	if ! ./$(BIN) --lang prime -e 'bare-top-token' >/dev/null 2>&1; then \
		echo "FAIL: PeTTa reader installation leaked into --lang prime"; \
		exit 1; \
	fi; \
	if ./$(BIN) --lang petta --profile he-extended -e '!(+ 1 2)' \
			>/dev/null 2>runtime/test-petta-compiled-reader-profile.err; then \
		echo "FAIL: PeTTa was silently treated as an HE profile"; \
		exit 1; \
	fi; \
	if ! grep -Fq "unknown source profile 'he-extended' for language 'petta'" \
			runtime/test-petta-compiled-reader-profile.err; then \
		echo "FAIL: PeTTa/HE profile boundary diagnostic changed"; \
		exit 1; \
	fi; \
	extended_result=$$(./$(BIN) --lang petta --profile extended \
		-e '!(+ 1 2)' 2>&1); \
	if [ "$$extended_result" != '3' ]; then \
		echo "FAIL: --lang petta --profile extended is not executable"; \
		printf '%s\n' "$$extended_result"; \
		exit 1; \
	fi; \
		echo "PASS: --lang petta owns compiled text/file parsing and remains distinct from HE and Prime"

.PHONY: test-petta-extended-query-algebra
test-petta-extended-query-algebra: $(BIN)
	@actual=runtime/test-petta-extended-query-algebra.out; \
	plain=runtime/test-petta-default-query-algebra.out; \
	./$(BIN) --lang petta --profile extended \
		tests/petta/extended_query_algebra.metta > "$$actual"; \
	diff -u tests/petta/extended_query_algebra.expected "$$actual"; \
	./$(BIN) --lang petta \
		-e '!(add-atom &self (f a))' \
		-e '!(collapse (match &self (| (f $$x) (g $$x)) $$x))' \
		> "$$plain"; \
	if [ "$$(sed -n '2p' "$$plain")" != '()' ]; then \
		echo "FAIL: default PeTTa silently acquired extended query choice"; \
		cat "$$plain"; exit 1; \
	fi; \
	./$(BIN) --lang petta --profile extended \
		tests/petta/as_pattern_capacity.metta > "$$actual"; \
	diff -u tests/petta/as_pattern_capacity.expected "$$actual"; \
	echo "PASS: PeTTa extended query/space algebra is executable and profile-local"

.PHONY: test-petta-capability-ledger
test-petta-capability-ledger: $(BIN)
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 tests/petta/test_capability_ledger.py \
			--cetta ./$(BIN) \
			$(if $(filter 1,$(ENABLE_PYTHON)),,--without-python)

.PHONY: test-petta-prepared-register-loop
test-petta-prepared-register-loop: $(BIN)
	@native=$$(mktemp runtime/petta-prepared-register-native.XXXXXX); \
	canonical=$$(mktemp runtime/petta-prepared-register-canonical.XXXXXX); \
	native_comparable=$$(mktemp runtime/petta-prepared-register-native-comparable.XXXXXX); \
	canonical_comparable=$$(mktemp runtime/petta-prepared-register-canonical-comparable.XXXXXX); \
	trap 'rm -f "$$native" "$$canonical" "$$native_comparable" "$$canonical_comparable"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_prepared_register_loop.metta \
		>"$$native"; \
	CETTA_PETTA_SEARCH_MACHINE=0 ./$(BIN) --lang petta \
		tests/petta/search_machine_prepared_register_loop.metta \
		>"$$canonical"; \
	diff -u tests/petta/search_machine_prepared_register_loop.expected \
		"$$native"; \
	awk 'NR != 4' "$$native" >"$$native_comparable"; \
	awk 'NR != 4' "$$canonical" >"$$canonical_comparable"; \
	diff -u "$$canonical_comparable" "$$native_comparable"; \
	echo "PASS: PeTTa prepared register loop preserves aliasing, ambiguity, table, and count boundaries"

.PHONY: test-petta-specialized-pure-call
test-petta-specialized-pure-call: $(BIN)
	@set -e; \
	actual=$$(mktemp runtime/petta-specialized-pure-call.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_specialized_pure_call.metta \
		>"$$actual"; \
	diff -u tests/petta/search_machine_specialized_pure_call.expected \
		"$$actual"; \
	echo "PASS: PeTTa specialized recursion preserves opaque values and evaluates active constructor fields"

.PHONY: test-petta-specialized-pure-call-stats
test-petta-specialized-pure-call-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	actual=$$(mktemp runtime/petta-specialized-pure-call-stats.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	stats=$$(CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --emit-runtime-stats --lang petta \
		tests/petta/search_machine_specialized_pure_call.metta \
		2>&1 >"$$actual"); \
	diff -u tests/petta/search_machine_specialized_pure_call.expected \
		"$$actual"; \
	first=$$(printf '%s\n' "$$stats" | \
		grep '^PETTA_MACHINE_STATS ' | head -1); \
	field() { \
		printf '%s\n' "$$first" | awk -v key="$$1" \
			'{ for (i = 1; i <= NF; i++) { split($$i, pair, "="); if (pair[1] == key) { print pair[2]; exit } } }'; \
	}; \
	transitions=$$(field transitions); \
	snapshots=$$(field clause_snapshot_calls); \
	matches=$$(field clause_match_attempts); \
	rewrites=$$(field specializer_prepare_rewritten); \
	admissions=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-admission" { print $$3 }'); \
	commits=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-commit" { print $$3 }'); \
	cancel_requests=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "hyperpose-cancel-request" { print $$3 }'); \
	cancel_observed=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "hyperpose-cancel-observed" { print $$3 }'); \
	if [ -z "$$first" ] || [ "$${transitions:-999999}" -gt 20 ] || \
	   [ "$${snapshots:-999999}" -ne 0 ] || \
	   [ "$${matches:-999999}" -ne 0 ] || \
	   [ "$${rewrites:-0}" -ne 1 ] || \
	   [ "$${admissions:-0}" -lt 1 ] || [ "$${commits:-0}" -lt 1 ] || \
	   [ "$${cancel_requests:-0}" -lt 1 ] || \
	   [ "$${cancel_observed:-0}" -lt 1 ]; then \
		echo "FAIL: specialized pure-call structural bound transitions=$$transitions snapshots=$$snapshots matches=$$matches rewrites=$$rewrites admissions=$$admissions commits=$$commits cancel_requests=$$cancel_requests cancel_observed=$$cancel_observed"; \
		exit 1; \
	fi; \
	echo "PASS: PeTTa prepared recursion crosses one specialization and observes enclosing cancellation (transitions=$$transitions snapshots=$$snapshots matches=$$matches cancel_observed=$$cancel_observed)"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

.PHONY: test-prime-prepared-match-decision-stats
test-prime-prepared-match-decision-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	actual=$$(mktemp runtime/prime-prepared-match-decision.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	stats=$$(./$(BIN) --emit-runtime-stats --lang prime \
		tests/prime/prepared_match_decision.metta \
		2>&1 >"$$actual"); \
	diff -u tests/prime/prepared_match_decision.expected "$$actual"; \
	inputs=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-decision-clause-input" { print $$3 }'); \
	survivors=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-decision-clause-survivor" { print $$3 }'); \
	full=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-decision-full-match" { print $$3 }'); \
	demands=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-decision-direct-demand" { print $$3 }'); \
	unavailable=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "match-decision-unavailable-path" { print $$3 }'); \
	if [ "$${inputs:-0}" -lt 25 ] || \
	   [ "$${survivors:-999999}" -gt 7 ] || \
	   [ "$${full:-999999}" -gt 15 ] || \
	   [ "$${demands:-0}" -ne 2 ] || \
	   [ "$${unavailable:-0}" -lt 1 ]; then \
		echo "FAIL: shared match decision input=$$inputs survivors=$$survivors full=$$full demands=$$demands unavailable=$$unavailable"; \
		exit 1; \
	fi; \
	echo "PASS: shared MatchDecision prunes prepared candidates, requests common Need demand, and preserves wildcard non-demand (input=$$inputs survivors=$$survivors full=$$full demands=$$demands unavailable=$$unavailable)"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

.PHONY: test-prime-prepared-control-program-cache-stats
test-prime-prepared-control-program-cache-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	actual=$$(mktemp runtime/prime-prepared-control-cache.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	stats=$$(./$(BIN) --emit-runtime-stats --lang prime \
		tests/prime/prepared_pure_control_cache.metta \
		2>&1 >"$$actual"); \
	diff -u tests/prime/prepared_pure_control_cache.expected "$$actual"; \
	hits=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-program-cache-hit" { print $$3 }'); \
	stores=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-program-cache-store" { print $$3 }'); \
	compiles=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-compile-attempt" { print $$3 }'); \
	declines=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-compile-decline" { print $$3 }'); \
	if [ "$${hits:-0}" -lt 100 ] || [ "$${stores:-999999}" -gt 8 ] || \
	   [ "$${compiles:-999999}" -gt 8 ] || [ "$${declines:-999999}" -ne 0 ]; then \
		echo "FAIL: Prime generated-control cache hits=$$hits stores=$$stores compiles=$$compiles declines=$$declines"; \
		exit 1; \
	fi; \
	echo "PASS: Prime automatically reuses LanguageDef-derived control programs and preserves unselected branches"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

.PHONY: test-petta-prepared-program-cache-stats
test-petta-prepared-program-cache-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	actual=$$(mktemp runtime/petta-prepared-program-cache.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	stats=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		./$(BIN) --emit-runtime-stats --lang petta \
		tests/petta/search_machine_prepared_program_cache.metta \
		2>&1 >"$$actual"); \
	diff -u tests/petta/search_machine_prepared_program_cache.expected \
		"$$actual"; \
	hits=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-program-cache-hit" { print $$3 }'); \
	stores=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-program-cache-store" { print $$3 }'); \
	if [ "$${hits:-0}" -ne 1 ] || [ "$${stores:-0}" -ne 2 ]; then \
		echo "FAIL: PeTTa prepared-program revision cache hits=$$hits stores=$$stores"; \
		exit 1; \
	fi; \
	echo "PASS: PeTTa reuses one prepared program and recompiles after semantic mutation"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

.PHONY: test-he-prepared-program-cache-stats
test-he-prepared-program-cache-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	actual=$$(mktemp runtime/he-prepared-program-cache.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	stats=$$(./$(BIN) --emit-runtime-stats --lang he \
		--profile he-extended \
		tests/prepared_pure_program_cache_he.metta \
		2>&1 >"$$actual"); \
	diff -u tests/prepared_pure_program_cache_he.expected \
		"$$actual"; \
	hits=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-program-cache-hit" { print $$3 }'); \
	stores=$$(printf '%s\n' "$$stats" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-program-cache-store" { print $$3 }'); \
	if [ "$${hits:-0}" -ne 1 ] || [ "$${stores:-0}" -ne 1 ]; then \
		echo "FAIL: HE prepared-program revision cache hits=$$hits stores=$$stores"; \
		exit 1; \
	fi; \
	echo "PASS: HE reuses one prepared program and rejects it after semantic mutation"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

.PHONY: test-petta-prepared-collection-pull
test-petta-prepared-collection-pull: $(BIN)
	@set -e; \
	actual=$$(mktemp runtime/petta-prepared-collection-pull.XXXXXX); \
	oracle=$$(mktemp runtime/petta-prepared-collection-pull-oracle.XXXXXX); \
	trap 'rm -f "$$actual" "$$oracle"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_prepared_collection_pull.metta \
		>"$$actual"; \
	CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_LET_COUNT_FUSION=0 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_prepared_collection_pull.metta \
		>"$$oracle"; \
	diff -u tests/petta/search_machine_prepared_collection_pull.expected \
		"$$actual"; \
	diff -u "$$oracle" "$$actual"; \
	echo "PASS: PeTTa producer pull preserves pure, materialized, nondeterminate, effect, and fault boundaries"

.PHONY: test-petta-prepared-collection-pull-stats
test-petta-prepared-collection-pull-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	actual=$$(mktemp runtime/petta-prepared-collection-pull-stats.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	stats=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		./$(BIN) --emit-runtime-stats --lang petta \
		tests/petta/search_machine_prepared_collection_pull.metta \
		2>&1 >"$$actual"); \
	diff -u tests/petta/search_machine_prepared_collection_pull.expected \
		"$$actual"; \
	field() { \
		printf '%s\n' "$$stats" | awk -v name="$$1" \
			'$$1 == "runtime-counter" && $$2 == name { print $$3 }'; \
	}; \
	admissions=$$(field prepared-collection-pull-admission); \
	items=$$(field prepared-collection-pull-item); \
	commits=$$(field prepared-collection-pull-commit); \
	declines=$$(field prepared-collection-pull-decline); \
	if [ "$${admissions:-0}" -lt 7 ] || \
	   [ "$${items:-0}" -lt 11 ] || \
	   [ "$${commits:-0}" -ne 4 ] || \
	   [ "$${declines:-0}" -lt 3 ] || \
	   [ "$$admissions" -ne $$((commits + declines)) ]; then \
		echo "FAIL: prepared collection pull admissions=$$admissions items=$$items commits=$$commits declines=$$declines"; \
		exit 1; \
	fi; \
	echo "PASS: prepared collection pull commits four certified folds and declines every unsafe boundary"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

.PHONY: test-petta-search-machine
test-lib-prolog: $(BIN)
	@set -e; \
	he_actual=$$(mktemp runtime/lib-prolog-he.XXXXXX); \
	prime_actual=$$(mktemp runtime/lib-prolog-prime.XXXXXX); \
	petta_actual=$$(mktemp runtime/lib-prolog-petta.XXXXXX); \
	trap 'rm -f "$$he_actual" "$$prime_actual" "$$petta_actual"' \
		EXIT INT TERM; \
	if [ "$(LIB_PROLOG_ENABLED)" = 1 ]; then \
		input=tests/lib_prolog_surface.metta; \
		he_prime_expected=tests/lib_prolog_surface.he-prime.expected; \
		petta_expected=tests/lib_prolog_surface.petta.expected; \
	else \
		input=tests/lib_prolog_surface_disabled.metta; \
		he_prime_expected=tests/lib_prolog_surface_disabled.he-prime.expected; \
		petta_expected=tests/lib_prolog_surface_disabled.petta.expected; \
	fi; \
	./$(BIN) --lang he "$$input" > "$$he_actual"; \
	./$(BIN) --lang prime "$$input" > "$$prime_actual"; \
	./$(BIN) --lang petta "$$input" > "$$petta_actual"; \
	diff -u "$$he_prime_expected" "$$he_actual"; \
	diff -u "$$he_prime_expected" "$$prime_actual"; \
	diff -u "$$petta_expected" "$$petta_actual"; \
	echo "PASS: shared lib_prolog surface in HE, Prime, and PeTTa"
.PHONY: test-lib-prolog

test-petta-libpl: $(BIN)
	@actual=$$(mktemp runtime/petta-libpl.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/foreign_predicate_bridge.metta > "$$actual"; \
	if [ "$(LIB_PROLOG_ENABLED)" = 1 ]; then \
		expected=tests/petta/foreign_predicate_bridge.expected; \
	else \
		expected=tests/petta/foreign_predicate_bridge.disabled.expected; \
	fi; \
	if ! diff -u "$$expected" "$$actual"; then \
		echo "FAIL: optional PeTTa libpl boundary"; \
		exit 1; \
	fi; \
	if [ "$(LIB_PROLOG_ENABLED)" = 1 ]; then \
		CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
			tests/petta/standard_comparison_bridge.metta \
			> "$$actual"; \
		if ! diff -u tests/petta/standard_comparison_bridge.expected \
				"$$actual"; then \
			echo "FAIL: PeTTa standard comparison libpl bridge"; \
			exit 1; \
		fi; \
		CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
			tests/petta/libpl_call_plan.metta \
			> "$$actual"; \
		if ! diff -u tests/petta/libpl_call_plan.expected \
				"$$actual"; then \
			echo "FAIL: PeTTa/libpl source-plan and accelerator contract"; \
			exit 1; \
		fi; \
		CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
			--num-threads 2 \
			tests/petta/foreign_predicate_hyperpose.metta \
			> "$$actual"; \
		if ! diff -u tests/petta/foreign_predicate_hyperpose.expected \
				"$$actual"; then \
			echo "FAIL: PeTTa pooled libpl worker boundary"; \
				exit 1; \
			fi; \
		for fixture in libpl_clause_ref_lifetime \
				token_space_clause_ref_lifetime \
				logical_list_capacity \
				generic_goal_revision; do \
			CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
				--profile extended "tests/petta/$$fixture.metta" \
				> "$$actual"; \
			if ! diff -u "tests/petta/$$fixture.expected" \
					"$$actual"; then \
				echo "FAIL: PeTTa/libpl boundary fixture ($$fixture)"; \
				exit 1; \
			fi; \
		done; \
		expected_dir=$$(realpath tests/petta); \
		(cd runtime && CETTA_PETTA_SEARCH_MACHINE=1 ../$(BIN) \
			--lang petta --profile extended \
			../tests/petta/working_dir_source.metta) > "$$actual"; \
		if [ "$$(cat "$$actual")" != "$$(printf 'true\n%s' "$$expected_dir")" ]; then \
			echo "FAIL: PeTTa working_dir is not the source directory"; \
			exit 1; \
		fi; \
	fi; \
	echo "PASS: optional PeTTa/libpl boundary invariants"
.PHONY: test-petta-libpl

test-petta-process-text: $(BIN)
	@actual=$$(mktemp runtime/petta-process-text.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/process_metta_string.metta > "$$actual"; \
	if ! diff -u tests/petta/process_metta_string.expected "$$actual"; then \
		echo "FAIL: native PeTTa process_metta_string"; \
		exit 1; \
	fi; \
	echo "PASS: native PeTTa process_metta_string"
.PHONY: test-petta-process-text

test-petta-specializer-relevance-filter: $(BIN) test-petta-specializer-prepare
	@off_out=$$(mktemp runtime/petta-specializer-filter-off.out.XXXXXX); \
	on_out=$$(mktemp runtime/petta-specializer-filter-on.out.XXXXXX); \
	higher_out=$$(mktemp runtime/petta-specializer-filter-higher.out.XXXXXX); \
	higher_stats=$$(mktemp runtime/petta-specializer-filter-higher.stats.XXXXXX); \
	trap 'rm -f "$$off_out" "$$on_out" "$$higher_out" "$$higher_stats"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER=0 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_relevance_filter.metta \
		>"$$off_out"; \
	CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_relevance_filter.metta \
		>"$$on_out"; \
	diff -u tests/petta/search_machine_specializer_relevance_filter.expected "$$off_out"; \
	diff -u "$$off_out" "$$on_out"; \
	CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
		CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_route_cache.metta \
		>"$$higher_out" 2>"$$higher_stats"; \
	diff -u tests/petta/search_machine_specializer_route_cache.expected "$$higher_out"; \
	rewritten=$$(sed -n 's/.*specializer_prepare_rewritten=\([0-9][0-9]*\).*/\1/p' "$$higher_stats" | awk '{n += $$1} END {print n + 0}'); \
	if [ "$$rewritten" -lt 1 ]; then \
		echo "FAIL: PeTTa specialization relevance filter suppressed a higher-order call"; \
		exit 1; \
	fi; \
	echo "PASS: PeTTa specializer boundary and route cache preserve exact answers"
.PHONY: test-petta-specializer-relevance-filter

test-petta-mam-contender-mutations: $(BIN)
	@set -e; \
	mutation_dir=runtime/petta-mam-contender-mutations; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_petta_mam_contender.py \
		specializer-reject-callable src/petta_specializer.c \
		"$$mutation_dir/petta_specializer.c"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -c \
		"$$mutation_dir/petta_specializer.c" \
		-o "$$mutation_dir/petta_specializer.o"; \
	$(CC) \
		$(filter-out src/petta_specializer.$(BUILD_OBJ_TAG).o \
			src/petta_specializer.$(BUILD_OBJ_TAG).runtime-stats.o,$(OBJ)) \
		"$$mutation_dir/petta_specializer.o" \
		-o "$$mutation_dir/cetta-specializer-reject-callable" \
		$(LDFLAGS); \
	specializer_out=$$(mktemp runtime/petta-specializer-mutation.out.XXXXXX); \
	specializer_err=$$(mktemp runtime/petta-specializer-mutation.err.XXXXXX); \
	trap 'rm -f "$$specializer_out" "$$specializer_err"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
		CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER=1 \
		"$$mutation_dir/cetta-specializer-reject-callable" --lang petta \
		tests/petta/search_machine_specializer_route_cache.metta \
		>"$$specializer_out" 2>"$$specializer_err"; \
	specializer_rc=$$?; \
	if [ "$$specializer_rc" -eq 0 ] && diff -q \
			tests/petta/search_machine_specializer_route_cache.expected \
			"$$specializer_out" >/dev/null; then \
		rewritten=$$(sed -n 's/.*specializer_prepare_rewritten=\([0-9][0-9]*\).*/\1/p' "$$specializer_err" | awk '{n += $$1} END {print n + 0}'); \
		if [ "$$rewritten" -gt 0 ]; then \
			echo "FAIL: specialization callable-rejection mutation survived its coverage gate"; \
			exit 1; \
		fi; \
	fi; \
	$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 \
		test-petta-source-memo-reset-mutation; \
	echo "PASS: PeTTa MAM contender mutations are killed"
.PHONY: test-petta-mam-contender-mutations

test-petta-source-memo-reset-mutation: $(BUILD_CONFIG_HEADER)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@set -e; \
	mutation_dir=runtime/petta-mam-contender-mutations; \
	mkdir -p "$$mutation_dir"; \
	python3 scripts/mutate_petta_mam_contender.py \
		source-memo-ignore-arena-reset src/term_universe.c \
		"$$mutation_dir/term_universe.c"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-DCETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS=1 \
		-o "$$mutation_dir/test-source-memo-ignore-arena-reset" \
		tests/test_term_universe_store_abi.c src/symbol.c src/atom.c \
		$(MATCH_STANDALONE_SRC) src/subst_tree.c src/term_canon.c \
		src/variant_shape.c src/variant_instance.c \
		"$$mutation_dir/term_universe.c" $(GROUNDED_STANDALONE_SRC) \
		src/native_sha256.c src/search_machine.c src/space.c \
		$(PARSER_STANDALONE_SRC) src/cetta_stdlib.c $(LDFLAGS); \
	if "$$mutation_dir/test-source-memo-ignore-arena-reset" \
		>/dev/null 2>&1; then \
		echo "FAIL: source-AtomId memo arena-reset mutation survived"; \
		exit 1; \
	fi; \
	echo "PASS: source-AtomId memo rejects stale entries after arena reset"
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif
.PHONY: test-petta-source-memo-reset-mutation

.PHONY: test-match-decision test-match-decision-gslt-v1 \
	test-match-decision-lanes \
	test-match-decision-stats test-match-decision-stats-body
test-match-decision: test-match-decision-gslt-v1 $(MATCH_DECISION_TEST_BIN)
	@./$(MATCH_DECISION_TEST_BIN)

test-match-decision-gslt-v1: $(MATCH_DECISION_POLICY_GENERATED_V1)
	@python3 tools/test_match_decision_policy_v1.py --root "$(CURDIR)" \
		--cc "$(CC)"

test-match-decision-lanes: $(BIN) test-prime-equation-call-sharing-tournament
	@$(CETTA_SCRIPT_RUN_ENV) python3 tools/test_match_decision_lanes.py \
		--cetta $(CETTA_SCRIPT_BIN) \
		--timeout $(MATCH_DECISION_LANE_TIMEOUT)

test-match-decision-stats:
ifeq ($(ENABLE_RUNTIME_STATS),0)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 \
		test-match-decision-stats-body
else
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 \
		test-match-decision-stats-body
endif

test-match-decision-stats-body: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 tools/test_match_decision_stats.py \
		--cetta $(CETTA_SCRIPT_BIN) \
		--timeout $(MATCH_DECISION_LANE_TIMEOUT)

.PHONY: test-petta-memoization
test-petta-memoization: $(BIN)
	@for stem in memo_control memo_policy_limits memo_answers \
			memo_effect_boundary memo_inert memo_library; do \
		actual=runtime/test-petta-search-machine-$$stem.out; \
		CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
			tests/petta/search_machine_$$stem.metta \
			>"$$actual" 2>&1; \
		if ! diff -u tests/petta/search_machine_$$stem.expected \
				"$$actual"; then \
			echo "FAIL: PeTTa memoization contract $$stem"; \
			exit 1; \
		fi; \
	done
	@echo "PASS: PeTTa opt-in memoization policy and effect boundaries"

.PHONY: test-petta-match-existence-fusion
test-petta-match-existence-fusion: $(BIN)
	@set -e; \
	for engine in native $(if $(filter 1,$(ENABLE_PATHMAP_SPACE)),pathmap,); do \
		positive_on=$$(mktemp runtime/petta-existence-$$engine-on.XXXXXX); \
		positive_off=$$(mktemp runtime/petta-existence-$$engine-off.XXXXXX); \
		positive_stats=$$(mktemp runtime/petta-existence-$$engine-stats.XXXXXX); \
		decline_on=$$(mktemp runtime/petta-existence-$$engine-decline-on.XXXXXX); \
		decline_off=$$(mktemp runtime/petta-existence-$$engine-decline-off.XXXXXX); \
		decline_stats=$$(mktemp runtime/petta-existence-$$engine-decline-stats.XXXXXX); \
		trap 'rm -f "$$positive_on" "$$positive_off" "$$positive_stats" "$$decline_on" "$$decline_off" "$$decline_stats"' EXIT INT TERM; \
		CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
			./$(BIN) --space-engine "$$engine" --lang petta \
			tests/petta/search_machine_match_existence.metta \
			>"$$positive_on" 2>"$$positive_stats"; \
		CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MATCH_EXISTENCE_FUSION=0 \
			./$(BIN) --space-engine "$$engine" --lang petta \
			tests/petta/search_machine_match_existence.metta \
			>"$$positive_off"; \
		diff -u tests/petta/search_machine_match_existence.expected \
			"$$positive_on"; \
		diff -u "$$positive_off" "$$positive_on"; \
		folds=$$(awk '{ for (i = 1; i <= NF; i++) if ($$i ~ /^match_existence_observer_folds=/) { split($$i, pair, "="); n += pair[2] } } END { print n + 0 }' "$$positive_stats"); \
		if [ "$$folds" -ne 4 ]; then \
			echo "FAIL: PeTTa $$engine existence observer folded $$folds/4 cases"; \
			exit 1; \
		fi; \
		CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
			./$(BIN) --space-engine "$$engine" --lang petta \
			tests/petta/search_machine_match_existence_decline.metta \
			>"$$decline_on" 2>"$$decline_stats"; \
		CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MATCH_EXISTENCE_FUSION=0 \
			./$(BIN) --space-engine "$$engine" --lang petta \
			tests/petta/search_machine_match_existence_decline.metta \
			>"$$decline_off"; \
		diff -u tests/petta/search_machine_match_existence_decline.expected \
			"$$decline_on"; \
		diff -u "$$decline_off" "$$decline_on"; \
		folds=$$(awk '{ for (i = 1; i <= NF; i++) if ($$i ~ /^match_existence_observer_folds=/) { split($$i, pair, "="); n += pair[2] } } END { print n + 0 }' "$$decline_stats"); \
		if [ "$$folds" -ne 0 ]; then \
			echo "FAIL: PeTTa $$engine existence observer crossed a decline boundary"; \
			exit 1; \
		fi; \
		rm -f "$$positive_on" "$$positive_off" "$$positive_stats" \
			"$$decline_on" "$$decline_off" "$$decline_stats"; \
		trap - EXIT INT TERM; \
	done
	@echo "PASS: PeTTa ground exact existence fusion and decline boundaries"

.PHONY: test-petta-type-langdef-source-binding-v1
test-petta-type-langdef-source-binding-v1:
	@python3 tools/gslt2parse_schema_v1.py \
		langdef/petta/generated/typecheck_v2_guard_v1.metta >/dev/null
	@mkdir -p $(BOOTSTRAP_TMPDIR); \
	tmpdir=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/petta-type-binding.XXXXXX"); \
	trap 'rm -f "$$tmpdir/binding.h" "$$tmpdir/binding.c" "$$tmpdir/binding-normalized.c"; rmdir "$$tmpdir"' EXIT INT TERM; \
	python3 tools/generate_nik_direct_source_binding_v1.py \
		--presentation langdef/petta/generated/typecheck_v2_guard_v1.metta \
		--symbol-prefix petta_typecheck_v2 \
		--authority-symbol petta_typecheck_v2_direct_authority_v1 \
		--authority-header petta_typecheck.h \
		--semantic-scope petta.typecheck-v2.guard-core \
		--coverage fragment \
		--header-output "$$tmpdir/binding.h" \
		--source-output "$$tmpdir/binding.c"; \
	sed 's#generated/binding.h#generated/petta_typecheck_v2_source_binding_v1.generated.h#' \
		"$$tmpdir/binding.c" > "$$tmpdir/binding-normalized.c"; \
	cmp -s "$$tmpdir/binding.h" \
		src/generated/petta_typecheck_v2_source_binding_v1.generated.h; \
	cmp -s "$$tmpdir/binding-normalized.c" \
		src/generated/petta_typecheck_v2_source_binding_v1.generated.c; \
	rm -f "$$tmpdir/binding-normalized.c"; \
	echo "PASS: PeTTa type langdef source binding is valid and deterministic"

test-petta-search-machine: $(PETTA_SEARCH_MACHINE_TEST_BIN) $(BIN) test-petta-type-langdef-source-binding-v1 test-petta-capability-ledger test-petta-specializer-relevance-filter test-petta-mam-contender-mutations test-petta-extended-query-algebra test-petta-prepared-register-loop test-petta-specialized-pure-call test-petta-memoization test-petta-match-existence-fusion
	@./$(PETTA_SEARCH_MACHINE_TEST_BIN)
	@machine_stats=$$(CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --lang petta -e '!(+ 1 2)' \
		2>&1 >/dev/null); \
	if ! printf '%s\n' "$$machine_stats" | \
			grep -q '^PETTA_MACHINE_STATS '; then \
		echo "FAIL: --lang petta did not select its search machine by default"; \
		exit 1; \
	fi; \
	legacy_stats=$$(CETTA_PETTA_SEARCH_MACHINE=0 \
		CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --lang petta -e '!(+ 1 2)' \
		2>&1 >/dev/null); \
	if printf '%s\n' "$$legacy_stats" | \
			grep -q '^PETTA_MACHINE_STATS '; then \
		echo "FAIL: explicit PeTTa legacy-oracle selection was ignored"; \
		exit 1; \
	fi
	@result=$$(printf 'q\n' | CETTA_PETTA_SEARCH_MACHINE=1 \
		./$(BIN) --lang petta \
		tests/petta/interactive_readln.metta 2>&1); \
	expected=$$(printf 'q\ntrue'); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa scripted readln! value"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		-e '!(flush-output!)' 2>&1); \
	if [ "$$result" != "true" ]; then \
		echo "FAIL: native PeTTa zero-argument effect root"; \
		diff <(printf 'true\n') \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		-e '!(< 0 inf)' -e '!(> 0 -inf)' 2>&1); \
	expected=$$(printf 'true\ntrue'); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa SWI infinite arithmetic bounds"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/python_bridge.metta 2>&1); \
	expected=$$(printf 'true\n42'); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa source-relative Python namespace"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		-e '!(py-call (cetta_missing_python_module.answer))' 2>&1); \
	if [[ "$$result" != \
			"python path resolution failed: No module named 'cetta_missing_python_module'" ]]; then \
		echo "FAIL: native PeTTa missing Python namespace result"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/recursive_relational_search.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_recursive.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine recursive relational search"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/equation_head_pattern.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_equation_head.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine relational cons head"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_structural_quote_head.metta 2>&1); \
	expected=$$(cat \
		tests/petta/search_machine_structural_quote_head.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa structural quote in equation head"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/inverse_relation.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_inverse.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine inverse append relation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_constructor_guided.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_constructor_guided.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa constructor-guided inverse search"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_occurs_check.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_occurs_check.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine acyclic unification"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_foldl.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_foldl.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa element-first relational foldl"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_atom_observers.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_atom_observers.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa pure atom observers"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_observer_callability.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_observer_callability.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa observers preserve callable failure"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		-e '!(car-atom ())' 2>&1); \
	if [ -n "$$result" ]; then \
		echo "FAIL: native PeTTa car-atom negative case"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		-e '!(cdr-atom atom)' 2>&1); \
	if [ -n "$$result" ]; then \
		echo "FAIL: native PeTTa cdr-atom negative case"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_sread.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_sread.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa relational sread"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/committed_choice.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_committed.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine committed choice"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/dynamic_definition_dispatch.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_dynamic.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine dynamic definition dispatch"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_dynamic_negative.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_dynamic_negative.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine call arity guard"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/type_directed_demand.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_type_demand.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine type-directed demand"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_current_type_roles.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_current_type_roles.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa current Atom/Expression evaluation roles"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_function_space.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_function_space.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa strict space and inert payload roles"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_type_demand_negative.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_type_demand_negative.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine type-declaration bag and arity"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_type_relation.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_type_relation.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa strict and extensible get-type relation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_catch_cons.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_catch_cons.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa catch and open-cons equality"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_relational_boolean_lists.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_relational_boolean_lists.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa Boolean relations and open-list search"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/typecheck_v2_profile_isolation.metta 2>&1); \
	expected=$$(cat tests/petta/typecheck_v2_profile_isolation.default.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: typecheck-v2 operations leaked into default PeTTa"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		--profile extended tests/petta/typecheck_v2_profile_isolation.metta 2>&1); \
	expected=$$(cat tests/petta/typecheck_v2_profile_isolation.extended.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: typecheck-v2 operations leaked into extended PeTTa"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		--profile typecheck-v2 tests/petta/typecheck_v2_profile_isolation.metta 2>&1); \
	expected=$$(cat tests/petta/typecheck_v2_profile_isolation.typecheck-v2.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: typecheck-v2 profile operations are inactive"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/list_set_exact.metta 2>&1); \
	expected=$$(cat tests/petta/list_set_exact.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa exact-identity list set operations"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/higher_order_specialization.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_higher_order.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine higher-order specialization"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_returned_callable.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_returned_callable.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: nullary relation returning an applied callable"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_relational_head_collapse.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_relational_head_collapse.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: relational equation head under nested collapse"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_higher_order_negative.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_higher_order_negative.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine higher-order false branch"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_provenance.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_specializer_provenance.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa specialization pattern provenance"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_invalidation.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_specializer_invalidation.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa specialization revision invalidation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_route_cache.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_specializer_route_cache.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa specialization route cache"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	oracle=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_SPECIALIZER_ROUTE_CACHE=0 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_route_cache.metta 2>&1); \
	if [ "$$result" != "$$oracle" ]; then \
		echo "FAIL: PeTTa specialization route cache differs from oracle"; \
		diff <(printf '%s\n' "$$oracle") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	specializer_plan_out=$$(mktemp runtime/petta-specializer-plan.out.XXXXXX); \
	specializer_plan_stats=$$(mktemp runtime/petta-specializer-plan.stats.XXXXXX); \
	gc_scaling_out=$$(mktemp runtime/petta-gc-scaling.out.XXXXXX); \
	gc_scaling_stats=$$(mktemp runtime/petta-gc-scaling.stats.XXXXXX); \
	trap 'rm -f "$$specializer_plan_out" "$$specializer_plan_stats" "$$gc_scaling_out" "$$gc_scaling_stats"' EXIT INT TERM; \
	CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_specializer_plan.metta \
		>"$$specializer_plan_out" 2>"$$specializer_plan_stats"; \
	if ! diff -u tests/petta/search_machine_specializer_plan.expected \
			"$$specializer_plan_out"; then \
		echo "FAIL: specialized clauses preserve occurrence plans"; \
		exit 1; \
	fi; \
	mapfile -t specializer_plan_steps < <( \
		sed -n 's/.*transitions=\([0-9][0-9]*\).*/\1/p' \
			"$$specializer_plan_stats"); \
	mapfile -t specializer_plan_heap < <( \
		sed -n 's/.*max_heap_live_bytes=\([0-9][0-9]*\).*/\1/p' \
			"$$specializer_plan_stats"); \
	if [ "$${#specializer_plan_steps[@]}" -ne 3 ] || \
	   [ "$${specializer_plan_steps[0]}" -le 0 ] || \
	   [ "$${specializer_plan_steps[1]}" -gt \
			"$$((3 * $${specializer_plan_steps[0]}))" ]; then \
		echo "FAIL: specialized recursive list work is not linear"; \
		cat "$$specializer_plan_stats"; \
		exit 1; \
	fi; \
	if [ "$${#specializer_plan_heap[@]}" -ne 3 ] || \
	   [ "$${specializer_plan_heap[0]}" -le 0 ] || \
	   [ "$${specializer_plan_heap[1]}" -gt \
			"$$((3 * $${specializer_plan_heap[0]}))" ]; then \
		echo "FAIL: specialized recursive list heap is not linear"; \
		cat "$$specializer_plan_stats"; \
		exit 1; \
	fi; \
	CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
		CETTA_PETTA_LET_COUNT_FUSION=0 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_gc_scaling.metta \
		>"$$gc_scaling_out" 2>"$$gc_scaling_stats"; \
	if ! diff -u tests/petta/search_machine_gc_scaling.expected \
			"$$gc_scaling_out"; then \
		echo "FAIL: generational collection changed exact list answers"; \
		exit 1; \
	fi; \
	mapfile -t gc_steps < <( \
		sed -n 's/.*transitions=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_roots < <( \
		sed -n 's/.*heap_goal_roots_scanned=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_promoted < <( \
		sed -n 's/.*heap_bytes_promoted=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_collections < <( \
		sed -n 's/.*heap_collections=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_heap < <( \
		sed -n 's/.*max_heap_live_bytes=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_major < <( \
		sed -n 's/.*heap_major_collections=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_continuation_copies < <( \
		sed -n 's/.*choice_continuation_items_copied=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	mapfile -t gc_binding_apply_calls < <( \
		sed -n 's/.*binding_apply_calls=\([0-9][0-9]*\).*/\1/p' \
			"$$gc_scaling_stats"); \
	gc_collection_accounting_ok=1; \
	if [ "$${#gc_collections[@]}" -eq 3 ] && \
	   [ "$${#gc_roots[@]}" -eq 3 ] && \
	   [ "$${#gc_promoted[@]}" -eq 3 ] && \
	   [ "$${#gc_major[@]}" -eq 3 ]; then \
		for i in 0 1 2; do \
			if { [ "$${gc_collections[$$i]}" -eq 0 ] && \
			     { [ "$${gc_roots[$$i]}" -ne 0 ] || \
			       [ "$${gc_promoted[$$i]}" -ne 0 ]; }; } || \
			   { [ "$${gc_collections[$$i]}" -gt 0 ] && \
			     { [ "$${gc_roots[$$i]}" -le 0 ] || \
			       [ "$${gc_promoted[$$i]}" -le 0 ]; }; } || \
			   [ "$${gc_major[$$i]}" -gt \
			     "$${gc_collections[$$i]}" ]; then \
				gc_collection_accounting_ok=0; \
			fi; \
		done; \
	else \
		gc_collection_accounting_ok=0; \
	fi; \
	if [ "$${#gc_steps[@]}" -ne 3 ] || \
	   [ "$${#gc_roots[@]}" -ne 3 ] || \
	   [ "$${#gc_promoted[@]}" -ne 3 ] || \
	   [ "$${#gc_collections[@]}" -ne 3 ] || \
	   [ "$${#gc_heap[@]}" -ne 3 ] || \
	   [ "$${#gc_major[@]}" -ne 3 ] || \
	   [ "$${#gc_continuation_copies[@]}" -ne 3 ] || \
	   [ "$${#gc_binding_apply_calls[@]}" -ne 3 ] || \
	   [ "$${gc_steps[0]}" -le 0 ] || \
	   [ "$${gc_heap[0]}" -le 0 ] || \
	   [ "$${gc_collections[2]}" -le 0 ] || \
	   [ "$$gc_collection_accounting_ok" -ne 1 ] || \
	   [ "$${gc_continuation_copies[0]}" -ne 0 ] || \
	   [ "$${gc_continuation_copies[1]}" -ne 0 ] || \
	   [ "$${gc_continuation_copies[2]}" -ne 0 ] || \
	   [ "$${gc_binding_apply_calls[0]}" -gt 256 ] || \
	   [ "$${gc_binding_apply_calls[1]}" -gt 256 ] || \
	   [ "$${gc_binding_apply_calls[2]}" -gt 256 ] || \
	   [ "$${gc_steps[1]}" -gt "$$((3 * $${gc_steps[0]}))" ] || \
	   [ "$${gc_steps[2]}" -gt "$$((3 * $${gc_steps[1]}))" ] || \
	   { [ "$${gc_roots[0]}" -gt 0 ] && \
	     [ "$${gc_roots[1]}" -gt "$$((3 * $${gc_roots[0]}))" ]; } || \
	   { [ "$${gc_roots[1]}" -gt 0 ] && \
	     [ "$${gc_roots[2]}" -gt "$$((3 * $${gc_roots[1]}))" ]; } || \
	   { [ "$${gc_promoted[0]}" -gt 0 ] && \
	     [ "$${gc_promoted[1]}" -gt "$$((3 * $${gc_promoted[0]}))" ]; } || \
	   { [ "$${gc_promoted[1]}" -gt 0 ] && \
	     [ "$${gc_promoted[2]}" -gt "$$((3 * $${gc_promoted[1]}))" ]; } || \
	   [ "$${gc_heap[1]}" -gt "$$((3 * $${gc_heap[0]}))" ] || \
	   [ "$${gc_heap[2]}" -gt "$$((3 * $${gc_heap[1]}))" ]; then \
		echo "FAIL: PeTTa generational collection work or memory is not linear"; \
		cat "$$gc_scaling_stats"; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_pair_selectors.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_pair_selectors.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa relational pair selectors"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_if.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_if.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa conditional control"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_assert.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_assert.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa assertion continuation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_exact_relation_precedence.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_exact_relation_precedence.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: exact PeTTa relation versus host fallback precedence"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_eval_role.metta 2>&1); \
	reference=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_CLAUSE_SLOT_FRAME_REFERENCE=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_eval_role.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_eval_role.expected); \
	if [ "$$result" != "$$expected" ] || \
	   [ "$$reference" != "$$expected" ]; then \
		echo "FAIL: PeTTa activation slots changed value provenance or explicit forcing"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_temporal_compile.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_temporal_compile.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: PeTTa source-ordered occurrence compilation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_import_forward.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_import_forward.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: PeTTa imported forward relation predeclaration"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_observable_print.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_observable_print.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: PeTTa observable string and float rendering"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	status=0; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_assert_failure.metta \
		>runtime/test-petta-machine-assert-failure.out 2>&1 || status=$$?; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL: native PeTTa failed assertion exit (got $$status)"; \
		exit 1; \
	fi; \
	if ! cmp -s tests/petta/search_machine_assert_failure.expected \
			runtime/test-petta-machine-assert-failure.out; then \
		echo "FAIL: native PeTTa failed assertion diagnostic"; \
		diff -u tests/petta/search_machine_assert_failure.expected \
			runtime/test-petta-machine-assert-failure.out | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/world_refined_space_control.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_world_refined.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine world-refined space control"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_world_refined_negative.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_world_refined_negative.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine empty-case fallback"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_logical_update.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_logical_update.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa logical-update view"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_conjunctive_match.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_conjunctive_match.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa conjunctive match fallback"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_collapse.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_collapse.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa delimited collapse"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_nested_length.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_nested_length.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa nested relational length"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_count_collapse.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_count_collapse.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa counted collapse bag fold"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	count_stats=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_count_collapse.metta \
		2>&1 >/dev/null); \
	if ! printf '%s\n' "$$count_stats" | \
			grep -Eq 'count_aggregate_let_fusions=[1-9][0-9]*'; then \
		echo "FAIL: source-planned PeTTa let/count fusion did not fire"; \
		exit 1; \
	fi; \
	oracle_result=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_LET_COUNT_FUSION=0 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_count_collapse.metta 2>&1); \
	if [ "$$oracle_result" != "$$expected" ]; then \
		echo "FAIL: PeTTa let/count fusion OFF oracle differs"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$oracle_result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_transaction.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_transaction.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine transactional commit/once/nesting"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/unsupported/transaction_rollback.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_transaction_rollback.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine transactional rollback"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_mutex_transaction.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_mutex_transaction.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine mutex and transactional effects"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_inert.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_inert.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine inert-head congruence"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_map_atom.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_map_atom.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine map-atom relation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_foldl_atom.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_foldl_atom.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine foldl-atom relation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_filter_atom.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_filter_atom.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine filter-atom relation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_translator.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_translator.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa translator registration and generated-code execution"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_translate_predicate.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_translate_predicate.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa predicate bridge and inert extension boundary"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_space_predicate.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_space_predicate.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa translated space predicates"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_tabled_ground.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_tabled_ground.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa ground SLG tables, SCC completion, and answer bags"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	table_stats=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_tabled_ground.metta \
		2>&1 >/dev/null); \
	if ! printf '%s\n' "$$table_stats" | \
			grep -Eq 'table_lookups=[1-9][0-9]*'; then \
		echo "FAIL: pure PeTTa table declaration was not admitted"; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_tabled_effect_boundary.metta 2>&1); \
	expected=$$(cat \
		tests/petta/search_machine_tabled_effect_boundary.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: effectful PeTTa table fallback or revision cache"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	effect_table_stats=$$(CETTA_PETTA_SEARCH_MACHINE=1 \
		CETTA_PETTA_MACHINE_STATS=1 \
		./$(BIN) --lang petta \
		tests/petta/search_machine_tabled_effect_boundary.metta \
		2>&1 >/dev/null); \
	if [ "$$(printf '%s\n' "$$effect_table_stats" | \
			grep -Ec 'table_lookups=[1-9][0-9]*')" -ne 1 ]; then \
		echo "FAIL: effectful relation entered the PeTTa table"; \
		printf '%s\n' "$$effect_table_stats"; \
		exit 1; \
	fi; \
	for stem in library_descriptor translator_rule tabled_call \
			intensional_provenance; do \
		result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
			tests/petta/unsupported/$$stem.metta 2>&1); \
		expected=$$(cat tests/petta/unsupported/$$stem.expected); \
		if [ "$$result" != "$$expected" ]; then \
			echo "FAIL: closed PeTTa capability witness $$stem"; \
			diff <(printf '%s\n' "$$expected") \
				<(printf '%s\n' "$$result") | head -40; \
			exit 1; \
		fi; \
	done; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_tail_prime.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_tail_prime.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa deterministic-tail heap reclamation"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
		tests/petta/search_machine_hyperpose.metta 2>&1); \
	expected=$$(cat tests/petta/search_machine_hyperpose.expected); \
	if [ "$$result" != "$$expected" ]; then \
		echo "FAIL: native PeTTa machine hyperpose answer bag"; \
		diff <(printf '%s\n' "$$expected") \
			<(printf '%s\n' "$$result") | head -40; \
		exit 1; \
	fi; \
	echo "PASS: native PeTTa machine evaluator integration"

.PHONY: test-petta-multifile
test-petta-multifile: $(BIN)
	@actual=$$(mktemp runtime/petta-multifile.XXXXXX); \
	trap 'rm -f "$$actual"' EXIT INT TERM; \
	./$(BIN) --lang petta \
		tests/petta/multifile/01_scope.metta \
		tests/petta/multifile/02_scope.metta > "$$actual"; \
	diff -u tests/petta/multifile/scope.expected "$$actual"; \
	./$(BIN) --lang petta \
		tests/petta/multifile/dir_a/main.metta \
		tests/petta/multifile/dir_b/main.metta > "$$actual"; \
	diff -u tests/petta/multifile/directories.expected "$$actual"; \
	./$(BIN) --lang he \
		tests/petta/multifile/argv_program.metta \
		tests/petta/multifile/argv_payload.metta > "$$actual"; \
	diff -u tests/petta/multifile/argv.expected "$$actual"; \
	echo "PASS: PeTTa ordered files preserve per-file scope and working directories; other dialect argv is unchanged"

.PHONY: test-petta-semantics
test-petta-semantics: $(BIN) test-petta-multifile
	@for stem in relational_control term_order numeric_semantics atom_operation_failure alpha_unique named_state implicit_space stream_ops list_length parse_data metatype_intrinsics type_failure_is_empty println_string unknown_head_quote library_descriptor library_descriptor_unsafe library_rooted_descriptor_unsafe git_import_surface; do \
		result=$$(CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta \
			tests/petta/$$stem.metta 2>&1); \
		expected=$$(cat tests/petta/$$stem.expected); \
		if [ "$$result" != "$$expected" ]; then \
			echo "FAIL: PeTTa semantic discriminator $$stem"; \
			diff <(printf '%s\n' "$$expected") \
				<(printf '%s\n' "$$result") | head -40; \
			exit 1; \
		fi; \
	done; \
	status=0; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta tests/petta/test_failure_exit.metta \
		>runtime/test-petta-failure-exit.out 2>&1 || status=$$?; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL: PeTTa failed test must request process exit 1 (got $$status)"; \
		exit 1; \
	fi; \
	if ! cmp -s tests/petta/test_failure_exit.expected \
			runtime/test-petta-failure-exit.out; then \
		echo "FAIL: PeTTa failed test diagnostics/control"; \
		diff -u tests/petta/test_failure_exit.expected \
			runtime/test-petta-failure-exit.out | head -40; \
			exit 1; \
	fi; \
	status=0; \
	CETTA_PETTA_SEARCH_MACHINE=1 ./$(BIN) --lang petta tests/petta/assert_failure_exit.metta \
		>runtime/test-petta-assert-failure-exit.out 2>&1 || status=$$?; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL: PeTTa failed assertion must request process exit 1 (got $$status)"; \
		exit 1; \
	fi; \
	if ! cmp -s tests/petta/assert_failure_exit.expected \
			runtime/test-petta-assert-failure-exit.out; then \
		echo "FAIL: PeTTa failed assertion diagnostics/control"; \
		diff -u tests/petta/assert_failure_exit.expected \
			runtime/test-petta-assert-failure-exit.out | head -40; \
		exit 1; \
	fi; \
	echo "PASS: PeTTa relational control, stream bags, list length, parse-as-data, implicit spaces, shared sequencing, named state, alpha uniqueness, metatype and typed-failure policy, library descriptors, and stable term order"

.PHONY: test-petta-corpus-manifest-unit probe-petta-corpus-manifest test-petta-corpus-manifest probe-petta-corpus-differential test-petta-corpus-differential test-petta-corpus-native-core test-petta-native-core-no-libpl
.PHONY: test-petta-typecheck-v2 test-petta-typecheck-v2-manifest test-petta-typecheck-v2-isolation-stats test-petta-typecheck-v2-omission
test-petta-typecheck-v2-manifest:
	@test -n "$(PETTA_TYPECHECK_REFERENCE_ROOT)" || \
		(echo 'set PETTA_TYPECHECK_REFERENCE_ROOT to the pinned Roman checkout' >&2; exit 2)
	@python3 scripts/petta_typecheck_v2_corpus.py \
		--manifest "$(PETTA_TYPECHECK_V2_MANIFEST)" \
		--reference-root "$(PETTA_TYPECHECK_REFERENCE_ROOT)" \
		--validate-only

test-petta-typecheck-v2: $(BIN) $(PETTA_SEARCH_MACHINE_TEST_BIN) test-petta-typecheck-v2-manifest test-petta-typecheck-v2-isolation-stats test-petta-typecheck-v2-omission
	@./$(PETTA_SEARCH_MACHINE_TEST_BIN)
	@CETTA_BIN=./$(BIN) scripts/test_petta_typecheck_v2.sh
	@receipt=$$(mktemp -d runtime/typecheck-v2-acceptance.XXXXXX); \
		python3 scripts/petta_typecheck_v2_corpus.py \
			--cetta ./$(BIN) \
			--manifest "$(PETTA_TYPECHECK_V2_MANIFEST)" \
			--reference-root "$(PETTA_TYPECHECK_REFERENCE_ROOT)" \
			--output-dir "$$receipt"; \
		status=$$?; \
		echo "typecheck-v2 acceptance receipt: $$receipt"; \
		exit $$status

test-petta-typecheck-v2-isolation-stats:
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@set -e; \
	ordinary_out=runtime/typecheck-v2-isolation-ordinary.out; \
	ordinary_err=runtime/typecheck-v2-isolation-ordinary.err; \
	typed_out=runtime/typecheck-v2-isolation-typed.out; \
	typed_err=runtime/typecheck-v2-isolation-typed.err; \
	authority_out=runtime/typecheck-v2-authority-reuse.out; \
	authority_err=runtime/typecheck-v2-authority-reuse.err; \
	relocation_out=runtime/typecheck-v2-obligation-relocation.out; \
	relocation_err=runtime/typecheck-v2-obligation-relocation.err; \
	classifier_out=runtime/typecheck-v2-classifier-reestablish.out; \
	classifier_err=runtime/typecheck-v2-classifier-reestablish.err; \
	classifier_invalidation_out=runtime/typecheck-v2-classifier-invalidation.out; \
	classifier_invalidation_err=runtime/typecheck-v2-classifier-invalidation.err; \
	boundary_plan_out=runtime/typecheck-v2-boundary-plan.out; \
	boundary_plan_err=runtime/typecheck-v2-boundary-plan.err; \
	$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		tests/petta/search_machine_catch_cons.metta \
		>"$$ordinary_out" 2>"$$ordinary_err"; \
	cmp -s tests/petta/search_machine_catch_cons.expected "$$ordinary_out"; \
	grep -Fqx 'runtime-counter petta-typecheck-boundary-entry 0' \
		"$$ordinary_err"; \
	$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		--profile typecheck-v2 \
		-e '(: f (-> Number Number)) (= (f $$x) $$x) !(f 1)' \
		>"$$typed_out" 2>"$$typed_err"; \
	grep -Fqx '1' "$$typed_out"; \
	grep -Eq '^runtime-counter petta-typecheck-boundary-entry [1-9][0-9]*$$' \
		"$$typed_err"; \
	$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		--profile typecheck-v2 \
		tests/petta/typecheck_v2_repros/11_stale_obligation_nonconflicting_addition.metta \
		>"$$authority_out" 2>"$$authority_err"; \
	cmp -s \
		tests/petta/typecheck_v2_repros/11_stale_obligation_nonconflicting_addition.expected \
		"$$authority_out"; \
	grep -Eq '^runtime-counter petta-type-obligation-cache-hit [1-9][0-9]*$$' \
		"$$authority_err"; \
	CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		--profile typecheck-v2 \
		tests/petta/typecheck_v2_repros/18_obligation_deterministic_heap_relocation.metta \
		>"$$relocation_out" 2>"$$relocation_err"; \
	cmp -s \
		tests/petta/typecheck_v2_repros/18_obligation_deterministic_heap_relocation.expected \
		"$$relocation_out"; \
	grep -Eq '^PETTA_MACHINE_STATS .* heap_collections=[1-9][0-9]* ' \
		"$$relocation_err"; \
	grep -Eq '^runtime-counter petta-type-obligation-cache-hit [1-9][0-9]*$$' \
		"$$relocation_err"; \
	$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		--profile typecheck-v2 \
		tests/petta/typecheck_v2_repros/38_deferred_runtime_classifier_reestablish.metta \
		>"$$classifier_out" 2>"$$classifier_err"; \
	cmp -s \
		tests/petta/typecheck_v2_repros/38_deferred_runtime_classifier_reestablish.expected \
		"$$classifier_out"; \
	grep -Eq '^runtime-counter petta-type-obligation-guard-scheduled ([2-9]|[1-9][0-9]+)$$' \
		"$$classifier_err"; \
	grep -Eq '^runtime-counter petta-type-obligation-guard-established ([2-9]|[1-9][0-9]+)$$' \
		"$$classifier_err"; \
	status=0; \
	$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		--profile typecheck-v2 \
		tests/petta/typecheck_v2_repros/37_deferred_runtime_classifier_space_mutation.metta \
		>"$$classifier_invalidation_out" 2>"$$classifier_invalidation_err" \
		|| status=$$?; \
	test "$$status" -eq 2; \
	test ! -s "$$classifier_invalidation_out"; \
	grep -Fq -f \
		tests/petta/typecheck_v2_repros/37_deferred_runtime_classifier_space_mutation.expected \
		"$$classifier_invalidation_err"; \
	$(CETTA_BIN_INVOKE) --emit-runtime-stats --lang petta \
		--profile typecheck-v2 \
		-e '(: f (-> Number Number)) (= (f $$x) $$x) (: twice (-> Number Number)) (= (twice $$x) (+ (f $$x) (f $$x))) !(twice 1)' \
		>"$$boundary_plan_out" 2>"$$boundary_plan_err"; \
	grep -Fqx '2' "$$boundary_plan_out"; \
	grep -Eq '^runtime-counter petta-typecheck-boundary-plan-cache-hit [1-9][0-9]*$$' \
		"$$boundary_plan_err"; \
	grep -Eq '^runtime-counter petta-typecheck-boundary-plan-cache-miss [1-9][0-9]*$$' \
		"$$boundary_plan_err"; \
	grep -Eq '^runtime-counter petta-typecheck-boundary-plan-all-none [1-9][0-9]*$$' \
		"$$boundary_plan_err"; \
	echo 'PASS: typecheck-v2 boundary isolation and relational authority reuse are counter-proven'
else
	@echo 'INFO: typecheck-v2 isolation counter requires runtime stats; re-running with ENABLE_RUNTIME_STATS=1'
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-petta-typecheck-v2-omission:
	@$(MAKE) --no-print-directory \
		BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=0 ENABLE_SANITIZERS=0 \
		ENABLE_PETTA_TYPECHECK_V2=0 \
		BIN="$(PETTA_TYPECHECK_V2_OMISSION_BIN)" all
	@set -e; \
		binary="$(PETTA_TYPECHECK_V2_OMISSION_BIN)"; \
		if nm -g "$$binary" | \
			grep -Eiq 'petta_typecheck|typecheck_v2_analysis|profile_petta_typecheck'; then \
			echo 'FAIL: disabled build still exports typecheck-v2 symbols'; \
			exit 1; \
		fi; \
		profiles=$$("$$binary" --lang petta --list-profiles 2>&1); \
		if printf '%s\n' "$$profiles" | grep -Fq 'typecheck-v2'; then \
			echo 'FAIL: disabled build still advertises typecheck-v2'; \
			exit 1; \
		fi; \
		if "$$binary" --help 2>&1 | grep -Fq 'typecheck-v2'; then \
			echo 'FAIL: disabled build help still exposes typecheck-v2'; \
			exit 1; \
		fi; \
		ordinary=$$(printf '!(+ 20 22)\n' | \
			"$$binary" --lang petta /dev/stdin); \
		test "$$ordinary" = 42; \
		status=0; \
		"$$binary" --lang petta --profile typecheck-v2 \
			tests/petta/typecheck_v2_profile_isolation.metta \
			>runtime/typecheck-v2-omission.out \
			2>runtime/typecheck-v2-omission.err || status=$$?; \
		test "$$status" -eq 2; \
		grep -Fq "unknown source profile 'typecheck-v2'" \
			runtime/typecheck-v2-omission.err; \
		echo 'PASS: typecheck-v2 is physically absent when disabled'

test-petta-corpus-manifest-unit:
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 tests/petta/test_corpus_manifest.py

.PHONY: test-petta-chainer-manifest-unit test-petta-chainer-compat test-petta-typecheck-v2-chainer
test-petta-chainer-manifest-unit:
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 tests/petta/test_chainer_compat_manifest.py

test-petta-chainer-compat: $(BIN) test-petta-chainer-manifest-unit
	@if [[ -z "$(strip $(PETTA_CHAINER_ROOT))" ]]; then \
		echo 'set PETTA_CHAINER_ROOT to a PeTTaChainer Git checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(PETTA_ORACLE_ROOT))" ]]; then \
		echo 'set PETTA_ORACLE_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@CETTA_PETTA_SEARCH_MACHINE=1 PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_chainer_compat.py \
		--cetta "./$(BIN)" \
		--chainer-repo "$(PETTA_CHAINER_ROOT)" \
		--petta-root "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CHAINER_COMPAT_MANIFEST)" \
		--out "$(PETTA_CHAINER_COMPAT_RESULTS)" \
		--reference

test-petta-typecheck-v2-chainer: $(BIN) test-petta-chainer-manifest-unit
	@if [[ -z "$(strip $(PETTA_CHAINER_ROOT))" ]]; then \
		echo 'set PETTA_CHAINER_ROOT to a PeTTaChainer Git checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(PETTA_ORACLE_ROOT))" ]]; then \
		echo 'set PETTA_ORACLE_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@CETTA_PETTA_SEARCH_MACHINE=1 PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_chainer_compat.py \
		--cetta "./$(BIN)" \
		--chainer-repo "$(PETTA_CHAINER_ROOT)" \
		--petta-root "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CHAINER_COMPAT_MANIFEST)" \
		--out "$(PETTA_CHAINER_COMPAT_RESULTS)-typecheck-v2" \
		--profile typecheck-v2

probe-petta-corpus-manifest: test-petta-corpus-manifest-unit
	@if [[ -z "$(strip $(PETTA_ORACLE_ROOT))" ]]; then \
		echo 'set PETTA_ORACLE_ROOT to the pinned PeTTa oracle checkout'; \
		exit 1; \
	fi
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py verify \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)"

test-petta-corpus-manifest: test-petta-corpus-manifest-unit
	@if [[ -z "$(strip $(PETTA_ORACLE_ROOT))" ]]; then \
		echo 'set PETTA_ORACLE_ROOT to the pinned PeTTa oracle checkout'; \
		exit 1; \
	fi
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py verify \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)" \
		--require-complete

probe-petta-corpus-differential: $(BIN) probe-petta-corpus-manifest
	@CETTA_PETTA_SEARCH_MACHINE=1 PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py compare \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)" \
		--cetta "./$(BIN)" \
		--out "$(PETTA_CORPUS_RESULTS)" \
		--timeout "$(PETTA_CORPUS_TIMEOUT)"

test-petta-corpus-differential: $(BIN) test-petta-corpus-manifest
	@CETTA_PETTA_SEARCH_MACHINE=1 PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py compare \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)" \
		--cetta "./$(BIN)" \
		--out "$(PETTA_CORPUS_RESULTS)" \
		--timeout "$(PETTA_CORPUS_TIMEOUT)" \
		--require-complete \
		--require-match

test-petta-corpus-native-core: $(BIN) test-petta-corpus-manifest
	@CETTA_PETTA_SEARCH_MACHINE=1 PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py compare \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)" \
		--cetta "./$(BIN)" \
		--out "$(PETTA_NATIVE_CORE_RESULTS)" \
		--timeout "$(PETTA_CORPUS_TIMEOUT)" \
		--exclude-capability lib-prolog \
		--require-complete \
		--require-match

test-petta-native-core-no-libpl:
	@$(MAKE) --no-print-directory BUILD=$(BUILD_CANON) \
		ENABLE_LIB_PROLOG=0 \
		test-petta-memoization test-petta-corpus-native-core

.PHONY: test-prime-compiled-reader-v1
test-prime-compiled-reader-v1: test-prime-compiled-reader-direct-generated-v1 test-gslt-prefix-reader-compiler-v1 $(PRIME_COMPILED_READER_TEST_BIN) $(BIN)
	@result=$$(./$(PRIME_COMPILED_READER_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | \
		grep -Fxc '(PrimeCompiledReaderV1Summary 89 89 0 cases 39)')" -ne 1 ]; then \
		echo "FAIL: compiled Prime reader exact differential summary absent or duplicated"; \
		exit 1; \
	fi; \
	text_result=$$(./$(BIN) --lang prime -e '!(+ 1 2)' 2>&1); \
	if [ "$$text_result" != '[3]' ]; then \
		echo "FAIL: --lang prime text path did not use the compiled reader"; \
		printf '%s\n' "$$text_result"; \
		exit 1; \
	fi; \
	file_result=$$(./$(BIN) --lang prime \
		tests/support/prime_compiled_reader_cli.metta 2>&1); \
	if [ "$$file_result" != '[5]' ]; then \
		echo "FAIL: --lang prime file path did not use the compiled reader"; \
		printf '%s\n' "$$file_result"; \
		exit 1; \
	fi; \
	if ./$(BIN) --lang prime -e '@' \
			>/dev/null 2>runtime/test-prime-compiled-reader-route.err; then \
		echo "FAIL: --lang prime bypassed prefix-payload rejection"; \
		exit 1; \
	fi; \
	if ! grep -Fq 'compiled Prime reader' \
			runtime/test-prime-compiled-reader-route.err; then \
		echo "FAIL: Prime rejection did not originate at the compiled reader"; \
		exit 1; \
	fi; \
	if ! ./$(BIN) --lang he --profile he-extended -e '@' \
			>/dev/null 2>&1; then \
		echo "FAIL: Prime prefix policy leaked into the HE reader"; \
		exit 1; \
	fi; \
	if ./$(BIN) --lang prime --profile he-extended -e '!(+ 1 2)' \
			>/dev/null 2>runtime/test-prime-compiled-reader-profile.err; then \
		echo "FAIL: Prime was silently treated as he-extended"; \
		exit 1; \
	fi; \
	if ! grep -Fq "language 'prime' has no named profiles" \
			runtime/test-prime-compiled-reader-profile.err; then \
		echo "FAIL: Prime/HE profile boundary diagnostic changed"; \
		exit 1; \
	fi; \
	echo "PASS: --lang prime owns its compiled reader and remains distinct from HE profiles"

test-rhometta-payload-map-capacity-c: $(PAYLOAD_MAP_CAPACITY_TEST_BIN)
	@./$(PAYLOAD_MAP_CAPACITY_TEST_BIN)

test-rhocalc-abt-substitution: $(RHOCALC_ABT_SUBSTITUTION_TEST_BIN)
	@result=$$(./$(RHOCALC_ABT_SUBSTITUTION_TEST_BIN) 2>&1); \
	printf '%s\n' "$$result"; \
	if [ "$$(printf '%s\n' "$$result" | grep -Fxc '(RhoABTSubstitutionSummary 1 1 0)')" -ne 1 ] || \
	   [ "$$(printf '%s\n' "$$result" | grep -Fxc 'PASS: nameful rho substitution agrees with canonical ABT substitution')" -ne 1 ]; then \
		echo "FAIL: rho/ABT substitution correspondence exact summary absent or duplicated"; \
		exit 1; \
	fi

.PHONY: bench-he-compiled-reader-v1
bench-he-compiled-reader-v1: $(HE_COMPILED_READER_BENCH_BIN)
	@./$(HE_COMPILED_READER_BENCH_BIN) \
		"$(HE_COMPILED_READER_BENCH_INPUT)" \
		"$(HE_COMPILED_READER_BENCH_ITERATIONS)"

test-import-modes: $(BIN)
	@default_result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/support/import_mode/nested/use_parent_helper.metta 2>&1); \
	if printf '%s\n' "$$default_result" | grep -Fq "Failed to resolve module Helper"; then \
		echo "PASS: default relative import mode stays local"; \
	else \
		echo "FAIL: default relative import mode stays local"; \
		printf '%s\n' "$$default_result"; \
		exit 1; \
	fi; \
	ancestor_result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he --import-mode ancestor-walk tests/support/import_mode/nested/use_parent_helper.metta 2>&1); \
	if [ "$$ancestor_result" = "$$(cat tests/support/import_mode/nested/use_parent_helper.expected)" ]; then \
		echo "PASS: ancestor-walk import mode finds parent helper"; \
	else \
		echo "FAIL: ancestor-walk import mode finds parent helper"; \
		diff <(cat tests/support/import_mode/nested/use_parent_helper.expected) <(printf '%s\n' "$$ancestor_result") | head -20; \
		exit 1; \
	fi; \
	expected_inventory=$$'[()]\n[()]'; \
	inventory_result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he --import-mode ancestor-walk \
		-e '!(bind! &mods (module-inventory!))' \
		-e '!(assertEqualToResult (match &mods (module-import-mode ancestor-walk) ok) (ok))' 2>&1); \
	if [ "$$inventory_result" = "$$expected_inventory" ]; then \
		echo "PASS: module inventory reports import mode"; \
	else \
		echo "FAIL: module inventory reports import mode"; \
		diff <(printf '%s\n' "$$expected_inventory") <(printf '%s\n' "$$inventory_result") | head -20; \
		exit 1; \
	fi

test-backends: $(BIN)
	@cache_dir="$(GIT_TEST_CACHE_DIR)"; mkdir -p "$$cache_dir"; \
	CETTA_GIT_MODULE_CACHE_DIR="$$cache_dir" python3 scripts/run_backend_parametric_tests.py \
		--cetta $(CETTA_BIN_INVOKE) \
		--lang he \
		--profile he-extended \
		--backends "$(BACKEND_PARAMETRIC_BACKENDS)" \
		--skip-tests "$(BACKEND_PARAMETRIC_SKIP_TESTS)" \
		--timeout "$(BACKEND_PARAMETRIC_TIMEOUT)" \
		--diff-lines "$(BACKEND_PARAMETRIC_DIFF_LINES)" \
		$(BACKEND_PARAMETRIC_TEST_PATTERNS)

test-backends-lanes: test-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lane
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-lane

refresh-he-contract-tests:
	@python3 scripts/sync_he_contract_tests.py

refresh-he-compat-catalog:
	@python3 scripts/build_he_compat_catalog.py --out $(HE_COMPAT_CATALOG)

refresh-he-native-contracts: refresh-he-compat-catalog
	@python3 scripts/build_he_native_contracts.py \
		--catalog $(HE_COMPAT_CATALOG) \
		--out $(HE_NATIVE_CONTRACTS)

test-he-compat-catalog-guards:
	@python3 scripts/test_he_compat_catalog_guards.py

.PHONY: test-he-type-langdef-source-binding-v1
test-he-type-langdef-source-binding-v1:
	@python3 tools/gslt2parse_schema_v1.py \
		langdef/he/generated/typing_consistency_core_v1.metta >/dev/null
	@mkdir -p $(BOOTSTRAP_TMPDIR); \
	tmpdir=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/he-type-binding.XXXXXX"); \
	trap 'rm -f "$$tmpdir/binding.h" "$$tmpdir/binding.c" "$$tmpdir/binding-normalized.c"; rmdir "$$tmpdir"' EXIT INT TERM; \
	python3 tools/generate_nik_direct_source_binding_v1.py \
		--presentation langdef/he/generated/typing_consistency_core_v1.metta \
		--symbol-prefix he_typing_consistency_core \
		--authority-symbol cetta_he_typing_core_direct_authority_v1 \
		--authority-header he_typing_authority.h \
		--semantic-scope he.typing.consistency-core \
		--coverage fragment \
		--header-output "$$tmpdir/binding.h" \
		--source-output "$$tmpdir/binding.c"; \
	sed 's#generated/binding.h#generated/he_typing_consistency_core_source_binding_v1.generated.h#' \
		"$$tmpdir/binding.c" > "$$tmpdir/binding-normalized.c"; \
	cmp -s "$$tmpdir/binding.h" \
		src/generated/he_typing_consistency_core_source_binding_v1.generated.h; \
	cmp -s "$$tmpdir/binding-normalized.c" \
		src/generated/he_typing_consistency_core_source_binding_v1.generated.c; \
	rm -f "$$tmpdir/binding-normalized.c"; \
	echo "PASS: HE type langdef source binding is valid and deterministic"

.PHONY: test-he-typing-direct-authority
test-he-typing-direct-authority: $(HE_TYPING_DIRECT_TEST_BIN) test-he-type-langdef-source-binding-v1
	@./$(HE_TYPING_DIRECT_TEST_BIN)

test-he-contract-suite: $(BIN) test-he-compat-catalog-guards test-he-typing-direct-authority
	@pass=0; fail=0; \
	files=($(HE_CONTRACT_GENERATED_DIR)/*.metta); \
	if [ ! -e "$${files[0]}" ]; then \
		echo "FAIL: no generated HE contract tests found in $(HE_CONTRACT_GENERATED_DIR)"; \
		echo "Run 'make refresh-he-contract-tests' to sync them from Mettapedia."; \
		exit 1; \
	fi; \
	for f in "$${files[@]}"; do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "FAIL: $$f (missing $$exp)"; \
			fail=$$((fail + 1)); \
			continue; \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat "$$exp")" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

.PHONY: test-he-return-contract-correlation
test-he-return-contract-correlation: $(BIN)
	@expected=$$(cat tests/test_he_return_contract_correlation.expected); \
	pass=0; fail=0; \
	for profile in he he-compat he-extended he-prime; do \
		result=$$($(CETTA_BIN_INVOKE) --lang he --profile $$profile \
			tests/test_he_return_contract_correlation.metta 2>&1); \
		if [ "$$result" = "$$expected" ]; then \
			echo "PASS: HE return-contract correlation ($$profile)"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: HE return-contract correlation ($$profile)"; \
			diff <(echo "$$expected") <(echo "$$result") | head -40; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$(CETTA_GC_BUDGET_MB=1 $(CETTA_BIN_INVOKE) --lang he --profile he \
		tests/test_he_return_contract_correlation.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: HE return-contract correlation (GC relocation)"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: HE return-contract correlation (GC relocation)"; \
		diff <(echo "$$expected") <(echo "$$result") | head -40; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-he-compat-semantic-suite: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/check_he_compat_semantic_suite.py \
		--catalog $(HE_COMPAT_CATALOG)

probe-he-compat-tier2: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/probe_he_compat_tier2.py \
		--catalog $(HE_COMPAT_CATALOG)

probe-he-compat-runnable-corpus: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/probe_he_compat_runnable_corpus.py

test-mork-lane: test-mork-lane-core

test-mork-lane-core:
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" = "1" ] || [ -n "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lane-core-body; \
	else \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
			bridge_build=mork; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
			echo "INFO: mork lane regression suite requires the MORK bridge; re-running with BUILD=$$bridge_build"; \
			$(MAKE) BUILD=$$bridge_build test-mork-lane-core-body; \
		else \
			if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
				echo "SKIP: mork lane regression suite (no MORK bridge manifest configured)"; \
			else \
				echo "SKIP: mork lane regression suite (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
			fi; \
		fi; \
	fi

test-mork-lane-core-body: $(BIN)
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-deprecated-space-engine-mork-guard
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-mork-program-space
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-exec-basic
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-import-mm2-mork-session-lowering
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-conformance-var-binding
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-var-scope-across-exprs
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-conformance-lean-suite
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-kiss-suite
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-basic-pathmap-guard

test-mork-runtime-stats-lane:
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" = "1" ] || [ -n "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		if [ "$(ENABLE_RUNTIME_STATS)" != "1" ]; then \
			echo "INFO: mork runtime-stats lane requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"; \
			$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-lane-body; \
		else \
			$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-lane-body; \
		fi; \
	else \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
			bridge_build=mork; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
			echo "INFO: mork runtime-stats lane requires the MORK bridge; re-running with BUILD=$$bridge_build and ENABLE_RUNTIME_STATS=1"; \
			$(MAKE) BUILD=$$bridge_build ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-lane-body; \
		else \
			if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
				echo "SKIP: mork runtime-stats lane (no MORK bridge manifest configured)"; \
			else \
				echo "SKIP: mork runtime-stats lane (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
			fi; \
		fi; \
	fi

test-mork-runtime-stats-lane-body:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-isolation-body
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-add-atoms-runtime-stats-body

test-mork-add-atoms-runtime-stats-body: $(BIN)
	@result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_mork_add_atoms_runtime_stats.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_add_atoms_runtime_stats.expected)" ]; then \
		echo "PASS: test_mork_add_atoms_runtime_stats"; \
	else \
		echo "FAIL: test_mork_add_atoms_runtime_stats"; \
		diff <(cat tests/test_mork_add_atoms_runtime_stats.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-deprecated-space-engine-mork-guard: $(BIN)
	@status=0; \
	result=$$($(CETTA_BIN_INVOKE) --space-engine mork --lang he tests/test_space_type.metta 2>&1) || status=$$?; \
	if [ "$(ENABLE_PATHMAP_SPACE)" = "1" ]; then \
		pathmap_line="  pathmap                flattened PathMap-style CeTTa engine without bridge rows"; \
	else \
		pathmap_line="  pathmap                flattened PathMap-style CeTTa engine without bridge rows (requires a bridge build: BUILD=mork or BUILD=main)"; \
	fi; \
	expected=$$(printf '%s\n' \
		"error: unknown space engine 'mork'" \
		"space engines:" \
		"  native                 standard CeTTa / HE engine" \
		"$$pathmap_line" \
		"  native-candidate-exact diagnostic native exact-matcher lane"); \
	if [ "$$status" -eq 2 ] && [ "$$result" = "$$expected" ]; then \
		echo "PASS: deprecated space-engine mork guard"; \
	else \
		echo "FAIL: deprecated space-engine mork guard"; \
		echo "status=$$status"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		exit 1; \
	fi

test-mork-basic-pathmap-guard: $(BIN)
	@if [ "$(ENABLE_PATHMAP_SPACE)" = "1" ]; then \
		echo "SKIP: mork/basic pathmap guards (pathmap lane enabled)"; \
	else \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he \
			-e '!(assertEqualToResult (new-space pathmap) ((Error (new-space pathmap) "generic pathmap-backed spaces require a bridge build (BUILD=mork or BUILD=main)")))' \
			-e '!(bind! &h (new-space hash))' \
			-e '!(assertEqualToResult (space-set-backend! &h pathmap) ((Error (space-set-backend! &h pathmap) "generic pathmap-backed spaces require a bridge build (BUILD=mork or BUILD=main)")))' \
			2>&1); \
		expected=$$'[()]\n[()]\n[()]'; \
		if [ "$$result" = "$$expected" ]; then \
			echo "PASS: mork/basic pathmap guards"; \
		else \
			echo "FAIL: mork/basic pathmap guards"; \
			diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
			exit 1; \
		fi; \
	fi

test-pathmap-lane:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-lane-body
else
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		bridge_build=mork; \
		if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
		echo "INFO: pathmap lane regression suite requires generic pathmap-backed spaces; re-running with BUILD=$$bridge_build"; \
		$(MAKE) BUILD=$$bridge_build test-pathmap-lane-body; \
	else \
		if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
			echo "SKIP: pathmap lane regression suite (no MORK bridge manifest configured)"; \
		else \
			echo "SKIP: pathmap lane regression suite (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
		fi; \
	fi
endif

test-pathmap-lane-body: $(BIN)
	@pass=0; fail=0; no_exp=0; \
	for f in $(PATHMAP_REQUIRED_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f" 2>&1); \
		if [ "$$result" = "$$(cat $$exp)" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			diff <(cat "$$exp") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	summary="$$pass passed, $$fail failed"; \
	if [ $$no_exp -gt 0 ]; then summary="$$summary, $$no_exp no .expected file"; fi; \
	echo "$$summary"; \
	[ $$fail -eq 0 ]
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-bridge-contextual-exact-rows
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-cursor-byte-buffer-count-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-cursor-expr-row-stream-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-query-row-stream-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-long-string-regression
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-match-chain
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-backend-primary-destructive-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-batch-mutations
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-streaming-d4-mutation
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-semi-naive-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lib-pathmap
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-duplicate-multiplicity-backends

probe-pathmap-lane:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) probe-pathmap-lane-body
else
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		bridge_build=mork; \
		if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
		echo "INFO: pathmap probe lane requires generic pathmap-backed spaces; re-running with BUILD=$$bridge_build"; \
		$(MAKE) BUILD=$$bridge_build probe-pathmap-lane-body; \
	else \
		if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
			echo "SKIP: pathmap probe lane (no MORK bridge manifest configured)"; \
		else \
			echo "SKIP: pathmap probe lane (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
		fi; \
	fi
endif

probe-pathmap-lane-body: $(BIN)
	@for f in $(PATHMAP_PROBE_TESTS); do \
		echo "PROBE: $$f"; \
		$(CETTA_BIN_INVOKE) --profile he-extended --lang he "$$f"; \
	done

test-pathmap-runtime-stats-lane:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@if [ "$(ENABLE_RUNTIME_STATS)" != "1" ]; then \
		echo "INFO: pathmap runtime-stats lane requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"; \
		$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-runtime-stats-lane-body; \
	else \
		$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-runtime-stats-lane-body; \
	fi
else
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		bridge_build=mork; \
		if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
		echo "INFO: pathmap runtime-stats lane requires generic pathmap-backed spaces; re-running with BUILD=$$bridge_build and ENABLE_RUNTIME_STATS=1"; \
		$(MAKE) BUILD=$$bridge_build ENABLE_RUNTIME_STATS=1 test-pathmap-runtime-stats-lane-body; \
	else \
		if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
			echo "SKIP: pathmap runtime-stats lane (no MORK bridge manifest configured)"; \
		else \
			echo "SKIP: pathmap runtime-stats lane (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
		fi; \
	fi
endif

test-pathmap-runtime-stats-lane-body:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-indexed-query-work
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-indexed-admission-boundaries
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-pull-consumers-work
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-gslt-execution-contracts
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-program-shadow-sync-work
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-interleaved-dispatch-view
	@expected=$$(cat tests/test_pathmap_direct_store_runtime_stats.expected); \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_pathmap_direct_store_runtime_stats.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: pathmap direct-store runtime-stats regression"; \
	else \
		echo "FAIL: pathmap direct-store runtime-stats regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-bridge-v2
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-conjunction-init
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-match-chain-v3

.PHONY: test-pathmap-indexed-admission-boundaries
test-pathmap-indexed-admission-boundaries: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@positive=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--lang he \
		-e '!(bind! &positive (new-space pathmap))' \
		-e '!(add-atom &positive (edge a b))' \
		-e '!(collapse (match &positive (edge $$x $$y) ($$x $$y)))' 2>&1); \
	automatic=$$(CETTA_PATHMAP_QUERY_INDEX= CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--lang he \
		-e '!(bind! &automatic (new-space pathmap))' \
		-e '!(add-atom &automatic (edge a b))' \
		-e '!(collapse (match &automatic (edge $$x $$y) ($$x $$y)))' 2>&1); \
	ordered=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--lang he \
		-e '!(bind! &ordered (new-space stack))' \
		-e '!(space-set-backend! &ordered pathmap)' \
		-e '!(add-atom &ordered (edge a b))' \
		-e '!(add-atom &ordered (edge c d))' \
		-e '!(collapse (match &ordered (edge $$x $$y) ($$x $$y)))' 2>&1); \
	unsupported=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--lang he \
		-e '!(bind! &unsupported (new-space pathmap))' \
		-e '!(add-atom &unsupported (typed f (arrow a b)))' \
		-e '!(collapse (match &unsupported (typed $$f (arrow $$a $$b)) $$f))' 2>&1); \
	positive_actual=$$(printf '%s\n' "$$positive" | grep '^\[' || true); \
	automatic_actual=$$(printf '%s\n' "$$automatic" | grep '^\[' || true); \
	ordered_actual=$$(printf '%s\n' "$$ordered" | grep '^\[' || true); \
	unsupported_actual=$$(printf '%s\n' "$$unsupported" | grep '^\[' || true); \
	positive_expected=$$(printf '%s\n' '[()]' '[()]' '[((a b))]'); \
	automatic_expected=$$(printf '%s\n' '[()]' '[()]' '[((a b))]'); \
	ordered_expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[((a b) (c d))]'); \
	unsupported_expected=$$(printf '%s\n' '[()]' '[()]' '[(f)]'); \
	if [ "$$positive_actual" != "$$positive_expected" ] || \
	   [ "$$automatic_actual" != "$$automatic_expected" ] || \
	   [ "$$ordered_actual" != "$$ordered_expected" ] || \
	   [ "$$unsupported_actual" != "$$unsupported_expected" ]; then \
		echo "FAIL: PathMap indexed admission changed answers"; \
		exit 1; \
	fi; \
	stat() { \
		printf '%s\n' "$$1" | awk -v key="$$2" \
			'$$1 == "runtime-counter" && $$2 == key { print $$3; found=1 } END { if (!found) exit 1 }'; \
	}; \
	positive_indexed=$$(stat "$$positive" pathmap-indexed-query) || exit 1; \
	automatic_indexed=$$(stat "$$automatic" pathmap-indexed-query) || exit 1; \
	ordered_indexed=$$(stat "$$ordered" pathmap-indexed-query) || exit 1; \
	unsupported_indexed=$$(stat "$$unsupported" pathmap-indexed-query) || exit 1; \
	if [ "$$positive_indexed" -lt 1 ] || [ "$$automatic_indexed" -ne 0 ] || \
	   [ "$$ordered_indexed" -ne 0 ] || \
	   [ "$$unsupported_indexed" -ne 0 ]; then \
		echo "FAIL: PathMap indexed admission expected forced/automatic/ordered/unsupported >0/0/0/0, got $$positive_indexed/$$automatic_indexed/$$ordered_indexed/$$unsupported_indexed"; \
		exit 1; \
	fi; \
	echo "PASS: PathMap indexed admission reserves the flat catalog for forced single-factor or automatic conjunction plans"
else
	@echo "INFO: PathMap indexed admission boundary requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-pathmap-indexed-query-work: $(BIN)
	@result=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=0 $(CETTA_BIN_INVOKE) \
		--emit-runtime-stats --profile he-extended --lang he \
		tests/test_pathmap_indexed_query_work.metta 2>&1); \
	oracle_result=$$(CETTA_PATHMAP_QUERY_INDEX=0 CETTA_PATHMAP_PULL_CONSUMERS=0 $(CETTA_BIN_INVOKE) \
		--profile he-extended --lang he \
		tests/test_pathmap_indexed_query_work.metta 2>&1); \
	expected=$$(cat tests/test_pathmap_indexed_query_work.expected); \
	actual=$$(printf '%s\n' "$$result" | grep '^\[' || true); \
	oracle_actual=$$(printf '%s\n' "$$oracle_result" | grep '^\[' || true); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: counted PathMap indexed query answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$actual") | head -20; \
		exit 1; \
	fi; \
	if [ "$$oracle_actual" != "$$expected" ]; then \
		echo "FAIL: counted PathMap unindexed oracle answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$oracle_actual") | head -20; \
		exit 1; \
	fi; \
	if [ "$$actual" != "$$oracle_actual" ]; then \
		echo "FAIL: counted PathMap indexed/unindexed differential"; \
		diff <(printf '%s\n' "$$oracle_actual") <(printf '%s\n' "$$actual") | head -20; \
		exit 1; \
	fi; \
	stat() { \
		printf '%s\n' "$$result" | awk -v key="$$1" \
			'$$1 == "runtime-counter" && $$2 == key { print $$3; found=1 } END { if (!found) exit 1 }'; \
	}; \
	assert_eq() { \
		value=$$(stat "$$1") || exit 1; \
		if [ "$$value" != "$$2" ]; then \
			echo "FAIL: $$1 expected $$2, got $$value"; \
			exit 1; \
		fi; \
	}; \
	assert_eq pathmap-indexed-query 6; \
	assert_eq pathmap-indexed-catalog-build 1; \
	assert_eq pathmap-indexed-catalog-row-scan 6; \
	assert_eq pathmap-indexed-access-path-build 3; \
	assert_eq pathmap-indexed-access-path-row 8; \
	assert_eq pathmap-indexed-plan-build 3; \
	assert_eq pathmap-indexed-plan-cache-hit 3; \
	assert_eq pathmap-indexed-row-emit 8; \
	assert_eq pathmap-indexed-row-aggregate 4; \
	assert_eq pathmap-indexed-count-pushdown 2; \
	assert_eq pathmap-indexed-replay-hit 3; \
	seeks=$$(stat pathmap-indexed-trie-seek) || exit 1; \
	frames=$$(stat pathmap-indexed-frame-cell-peak) || exit 1; \
	if [ "$$seeks" -gt 40 ] || [ "$$frames" -gt 4 ]; then \
		echo "FAIL: indexed query work bound (seeks=$$seeks, frame-cells=$$frames)"; \
		exit 1; \
	fi; \
	echo "PASS: counted PathMap indexed pull query (oracle differential/cold/warm/multiplicity/work bounds)"

$(EXECUTION_CONTRACTS_TEST_BIN): tests/test_execution_contracts_generated.c \
		$(EXECUTION_CONTRACTS_GENERATED_H) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ \
		tests/test_execution_contracts_generated.c $(LDFLAGS)

.PHONY: test-prepared-pure-call-machine
test-prepared-pure-call-machine: $(BIN)
	@set -e; \
	he_out=$$(mktemp runtime/prepared-pure-call-he.XXXXXX); \
	he_no_gc_out=$$(mktemp runtime/prepared-pure-call-he-no-gc.XXXXXX); \
	prime_out=$$(mktemp runtime/prepared-pure-call-prime.XXXXXX); \
	petta_out=$$(mktemp runtime/prepared-pure-call-petta.XXXXXX); \
	demand_out=$$(mktemp runtime/prepared-pure-call-demand.XXXXXX); \
	sharing_out=$$(mktemp runtime/prepared-pure-call-sharing.XXXXXX); \
	choice_out=$$(mktemp runtime/prepared-pure-call-choice.XXXXXX); \
	choice_diag=$$(mktemp runtime/prepared-pure-call-choice-diag.XXXXXX); \
	trap 'rm -f "$$he_out" "$$he_no_gc_out" "$$prime_out" "$$petta_out" "$$demand_out" "$$sharing_out" "$$choice_out" "$$choice_diag"' EXIT INT TERM; \
	CETTA_GC=1 CETTA_GC_BUDGET_MB=1 ./$(BIN) --lang he --profile he-extended \
		tests/prepared_pure_call_machine.metta >"$$he_out"; \
	diff -u tests/prepared_pure_call_machine.he.expected "$$he_out"; \
	CETTA_GC=0 ./$(BIN) --lang he --profile he-extended \
		tests/prepared_pure_call_machine.metta >"$$he_no_gc_out"; \
	diff -u "$$he_no_gc_out" "$$he_out"; \
	CETTA_GC=1 CETTA_GC_BUDGET_MB=1 ./$(BIN) --lang prime \
		tests/prepared_pure_call_machine.metta \
		>"$$prime_out"; \
	diff -u tests/prepared_pure_call_machine.prime.expected "$$prime_out"; \
	CETTA_GC=1 CETTA_GC_BUDGET_MB=1 ./$(BIN) --lang petta \
		tests/prepared_pure_call_machine.metta \
		>"$$petta_out"; \
	diff -u tests/prepared_pure_call_machine.petta.expected "$$petta_out"; \
	./$(BIN) --lang prime tests/prime/prepared_pure_call_demand.metta \
		>"$$demand_out"; \
	diff -u tests/prime/prepared_pure_call_demand.expected "$$demand_out"; \
	CETTA_GC=1 CETTA_GC_BUDGET_MB=1 ./$(BIN) --lang prime \
		tests/prime/prepared_pure_need_sharing.metta \
		>"$$sharing_out"; \
	diff -u tests/prime/prepared_pure_need_sharing.expected \
		"$$sharing_out"; \
	CETTA_PREPARED_PURE_DEBUG=1 ./$(BIN) --lang prime \
		tests/prime/computed_argument_choicepoints.metta \
		>"$$choice_out" 2>"$$choice_diag"; \
	diff -u tests/prime/computed_argument_choicepoints.expected \
		"$$choice_out"; \
	determinacy_declines=$$(grep -c \
		'head is not determinate from weak-head patterns' \
		"$$choice_diag"); \
	if [ "$$determinacy_declines" -lt 2 ]; then \
		echo "FAIL: weak-head determinacy negatives were not both exercised"; \
		cat "$$choice_diag"; \
		exit 1; \
	fi; \
	observation_count=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
		./$(BIN) --lang prime --count-only \
		tests/prime/prepared_pure_observation_stack.metta); \
	if [ "$$observation_count" != 1 ]; then \
		echo "FAIL: Prime structural observation did not remain heap-resident"; \
		printf '%s\n' "$$observation_count"; \
		exit 1; \
	fi; \
	non_tail=$$(./$(BIN) --lang prime \
		tests/prime/non_tail_symbolic_call_stack.metta); \
	if [ "$$non_tail" != '[10000]' ]; then \
		echo "FAIL: Prime non-tail symbolic recursion left the heap machine"; \
		printf '%s\n' "$$non_tail"; \
		exit 1; \
	fi; \
	non_tail_observation_count=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
		./$(BIN) --lang prime --count-only \
		tests/prime/non_tail_symbolic_observation_stack.metta); \
	if [ "$$non_tail_observation_count" != 1 ]; then \
		echo "FAIL: Prime non-tail symbolic observation changed its answer count"; \
		printf '%s\n' "$$non_tail_observation_count"; \
		exit 1; \
	fi; \
	echo "PASS: generated pure-call machine is stack-safe, demand-preserving, nested-ambiguity-conservative, and non-tail-symbolic"

.PHONY: test-mam-symbolic-battery
test-mam-symbolic-battery: $(BIN)
	@bash scripts/test_mam_symbolic_battery.sh ./$(BIN)

.PHONY: test-mam-held-symbolic
test-mam-held-symbolic: $(BIN)
	@set -e; \
	he_out=$$(mktemp runtime/mam-held-symbolic-he.XXXXXX); \
	prime_out=$$(mktemp runtime/mam-held-symbolic-prime.XXXXXX); \
	petta_out=$$(mktemp runtime/mam-held-symbolic-petta.XXXXXX); \
	trap 'rm -f "$$he_out" "$$prime_out" "$$petta_out"' EXIT INT TERM; \
	timeout 60 ./$(BIN) --lang he --profile he-extended \
		tests/gc/repro_held_symbolic_arg_inner_loop.metta >"$$he_out"; \
	timeout 60 ./$(BIN) --lang prime \
		tests/gc/repro_held_symbolic_arg_inner_loop.metta >"$$prime_out"; \
	timeout 60 ./$(BIN) --lang petta \
		tests/gc/repro_held_symbolic_arg_inner_loop.metta >"$$petta_out"; \
	diff -u tests/gc/repro_held_symbolic_arg_inner_loop.he-prime.expected \
		"$$he_out"; \
	diff -u tests/gc/repro_held_symbolic_arg_inner_loop.he-prime.expected \
		"$$prime_out"; \
	diff -u tests/gc/repro_held_symbolic_arg_inner_loop.petta.expected \
		"$$petta_out"; \
	echo "PASS: held symbolic arguments survive generated tail continuations in all lanes"

.PHONY: test-mam-jetta-smoke
test-mam-jetta-smoke: $(BIN)
	@CETTA_BIN=./$(BIN) bash scripts/test_mam_jetta_suite.sh

.PHONY: test-mam-jetta-paper
test-mam-jetta-paper: $(BIN)
	@MAM_JETTA_SCALE=paper \
		MAM_JETTA_LANGUAGES='he prime petta' \
		MAM_JETTA_HE_PROFILE=he-extended \
		MAM_JETTA_REQUIRE_PASS=1 \
		CETTA_BIN=./$(BIN) \
		bash scripts/bench_mam_jetta_suite.sh

.PHONY: bench-mam-linear-fibonacci
bench-mam-linear-fibonacci: $(BIN)
	@MAM_LINEAR_FIB_LANGUAGES='he prime petta' \
		$(CETTA_SCRIPT_RUN_ENV) \
		bash scripts/bench_mam_linear_fibonacci.sh

.PHONY: test-prepared-pure-call-machine-stats
test-prepared-pure-call-machine-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@result=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
		./$(BIN) --emit-runtime-stats --lang he \
		--profile he-extended tests/prepared_pure_call_machine.metta 2>&1); \
	admissions=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-admission" { print $$3 }'); \
	commits=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-commit" { print $$3 }'); \
	declines=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-decline" { print $$3 }'); \
	collections=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-gc-collection" { print $$3 }'); \
	evacuated=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-gc-evacuated-bytes" { print $$3 }'); \
	reclaimed=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-gc-reclaimed-bytes" { print $$3 }'); \
	if [ "$${admissions:-0}" -lt 2 ] || [ "$${commits:-0}" -lt 1 ] || \
	   [ "$${declines:-0}" -lt 1 ] || [ "$${collections:-0}" -lt 1 ] || \
	   [ "$${collections:-0}" -gt 10 ] || \
	   [ "$${evacuated:-0}" -gt 1048576 ] || \
	   [ "$${reclaimed:-0}" -lt 1 ]; then \
		echo "FAIL: pure-call mechanism witness admission=$$admissions commit=$$commits decline=$$declines collections=$$collections evacuated=$$evacuated reclaimed=$$reclaimed"; \
		exit 1; \
	fi; \
	need_result=$$(CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
		./$(BIN) --emit-runtime-stats --lang prime \
		tests/prime/prepared_pure_need_sharing.metta 2>&1); \
	need_actual=$$(printf '%s\n' "$$need_result" | grep '^\[' || true); \
	need_expected=$$(cat tests/prime/prepared_pure_need_sharing.expected); \
	memo_stores=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-thunk-memo-store" { print $$3 }'); \
	memo_hits=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-thunk-memo-hit" { print $$3 }'); \
	blackholes=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-thunk-blackhole" { print $$3 }'); \
	need_collections=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-gc-collection" { print $$3 }'); \
	ephemeron_reclaimed=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-ephemeron-reclaimed" { print $$3 }'); \
	path_compressions=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-thunk-path-compression" { print $$3 }'); \
	tail_reentries=$$(printf '%s\n' "$$need_result" | awk \
		'$$1 == "runtime-counter" && $$2 == "prepared-pure-call-tail-reentry" { print $$3 }'); \
	if [ "$$need_actual" != "$$need_expected" ] || \
	   [ "$${memo_stores:-0}" -lt 1 ] || [ "$${memo_hits:-0}" -lt 1 ] || \
	   [ "$${blackholes:-0}" -ne 0 ] || \
	   [ "$${need_collections:-0}" -lt 1 ] || \
	   [ "$${ephemeron_reclaimed:-0}" -lt 1 ] || \
	   [ "$${path_compressions:-0}" -lt 1 ] || \
	   [ "$${tail_reentries:-0}" -lt 1 ]; then \
		echo "FAIL: Need update-cell witness stores=$$memo_stores hits=$$memo_hits blackholes=$$blackholes collections=$$need_collections ephemeron-reclaimed=$$ephemeron_reclaimed path-compressions=$$path_compressions tail-reentries=$$tail_reentries"; \
		exit 1; \
	fi; \
	echo "PASS: pure-call mechanism witnesses commit, conservative decline, bounded collection sampling, generated-root reclamation, shared Need updates, and tail reentry"
else
	@echo "INFO: pure-call mechanism witness requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-prepared-pure-call-machine-stats
endif

.PHONY: test-gslt-execution-contracts
test-gslt-execution-contracts: $(BIN) $(EXECUTION_CONTRACTS_TEST_BIN) \
		test-prepared-pure-call-machine
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@PYTHONDONTWRITEBYTECODE=1 $(EXECUTION_CONTRACTS_GENERATOR) \
		--cetta ./$(BIN) --check --check-fold-runtime
	@$(EXECUTION_CONTRACTS_TEST_BIN)
	@result=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--lang he tests/generated/execution_contracts.metta 2>&1); \
	oracle_result=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=0 \
		$(CETTA_BIN_INVOKE) --profile he-extended --lang he \
		tests/generated/execution_contracts.metta 2>&1); \
	expected=$$(cat tests/generated/execution_contracts.expected); \
	actual=$$(printf '%s\n' "$$result" | grep '^\[' || true); \
	oracle_actual=$$(printf '%s\n' "$$oracle_result" | grep '^\[' || true); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: execution-contract native answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$actual") | head -30; \
		exit 1; \
	fi; \
	if [ "$$oracle_actual" != "$$expected" ]; then \
		echo "FAIL: execution-contract oracle answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$oracle_actual") | head -30; \
		exit 1; \
	fi; \
	if printf '%s\n' "$$result" | grep -Fq '[chain]'; then \
		echo "FAIL: default observation profile leaked chain progress"; \
		exit 1; \
	fi; \
	pull_runs=$$(printf '%s\n' "$$result" | awk \
		'$$1 == "runtime-counter" && $$2 == "pathmap-pull-match-run" { print $$3 }'); \
	if [ "$$pull_runs" != 2 ]; then \
		echo "FAIL: relational may-effect admission expected 2 pure pulls, got $$pull_runs"; \
		exit 1; \
	fi; \
	trace=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		CETTA_MATCH_CHAIN_TRACE=1 CETTA_MATCH_CHAIN_TRACE_INTERVAL=1 \
		$(CETTA_BIN_INVOKE) --profile he-extended --lang he \
		tests/generated/execution_contracts.metta 2>&1 >/dev/null); \
	if ! printf '%s\n' "$$trace" | grep -Fq '[chain]'; then \
		echo "FAIL: diagnostic observation profile emitted no chain progress"; \
		exit 1; \
	fi; \
	echo "PASS: GSLT execution contracts drive planner, row, observation, and lifetime artifacts"
else
	@echo "INFO: execution-contract mechanism witnesses require compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-gslt-execution-contracts
endif

ifeq ($(ENABLE_RUNTIME_STATS),1)
test-pathmap-pull-consumers-work: $(BIN)
	@result=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--lang he tests/test_pathmap_pull_consumers_work.metta 2>&1); \
	oracle_result=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS=0 \
		$(CETTA_BIN_INVOKE) --profile he-extended --lang he \
		tests/test_pathmap_pull_consumers_work.metta 2>&1); \
	expected=$$(cat tests/test_pathmap_pull_consumers_work.expected); \
	actual=$$(printf '%s\n' "$$result" | grep '^\[' || true); \
	oracle_actual=$$(printf '%s\n' "$$oracle_result" | grep '^\[' || true); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: counted PathMap pull-consumer answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$actual") | head -20; \
		exit 1; \
	fi; \
	if [ "$$oracle_actual" != "$$expected" ]; then \
		echo "FAIL: counted PathMap materializing-oracle answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$oracle_actual") | head -20; \
		exit 1; \
	fi; \
	stat() { \
		printf '%s\n' "$$result" | awk -v key="$$1" \
			'$$1 == "runtime-counter" && $$2 == key { print $$3; found=1 } END { if (!found) exit 1 }'; \
	}; \
	assert_eq() { \
		value=$$(stat "$$1") || exit 1; \
		if [ "$$value" != "$$2" ]; then \
			echo "FAIL: $$1 expected $$2, got $$value"; \
			exit 1; \
		fi; \
	}; \
	assert_eq pathmap-pull-match-run 6; \
	assert_eq pathmap-pull-match-row 16; \
	assert_eq pathmap-pull-match-generated-outcome 16; \
	assert_eq pathmap-pull-match-generated-outcome-peak 1; \
	assert_eq pathmap-pull-atoms-run 12; \
	assert_eq pathmap-pull-atoms-row 23; \
	assert_eq pathmap-indexed-query 9; \
	assert_eq pathmap-indexed-residual-query 7; \
	assert_eq pathmap-indexed-row-aggregate 4; \
	assert_eq pathmap-indexed-count-pushdown 1; \
	echo "PASS: counted PathMap exact-plus-residual queries feed count/prefix/fold consumers without whole-space materialization"
else
test-pathmap-pull-consumers-work:
	@echo "INFO: PathMap pull-consumer witnesses require the main bridge and compile-time runtime stats; re-running with BUILD=main ENABLE_RUNTIME_STATS=1"
	@$(MAKE) -s BUILD=main ENABLE_RUNTIME_STATS=1 test-pathmap-pull-consumers-work
endif

test-pathmap-program-shadow-sync-work: $(BIN)
	@result=$$(CETTA_PATHMAP_QUERY_INDEX=1 $(CETTA_BIN_INVOKE) \
		--emit-runtime-stats --profile he-extended --space-engine pathmap \
		--lang he tests/test_pathmap_program_shadow_sync_work.metta 2>&1); \
	petta_result=$$(CETTA_PATHMAP_QUERY_INDEX=1 $(CETTA_BIN_INVOKE) \
		--emit-runtime-stats --profile extended --space-engine pathmap \
		--lang petta tests/test_pathmap_program_shadow_sync_work.metta 2>&1); \
	expected=$$(cat tests/test_pathmap_program_shadow_sync_work.expected); \
	actual=$$(printf '%s\n' "$$result" | grep '^\[' || true); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL: PathMap program-shadow answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$actual") | head -20; \
		exit 1; \
	fi; \
	stat_from() { \
		printf '%s\n' "$$1" | awk -v key="$$2" \
			'$$1 == "runtime-counter" && $$2 == key { print $$3; found=1 } END { if (!found) exit 1 }'; \
	}; \
	petta_true=$$(printf '%s\n' "$$petta_result" | grep -c '^true$$' || true); \
	if [ "$$petta_true" -ne 4 ] || \
	   printf '%s\n' "$$petta_result" | grep -Eq '(^|\()Error([ :)]|$$)'; then \
		echo "FAIL: PathMap PeTTa logical-update shadow answers"; \
		printf '%s\n' "$$petta_result" | tail -30; \
		exit 1; \
	fi; \
	refreshes=$$(stat_from "$$result" pathmap-shadow-refresh) || exit 1; \
	materializations=$$(stat_from "$$result" pathmap-materialize-native) || exit 1; \
	if [ "$$refreshes" -gt 5 ] || [ "$$materializations" -gt 5 ]; then \
		echo "FAIL: PathMap program shadow rebuilt per answer (refreshes=$$refreshes, materializations=$$materializations)"; \
		exit 1; \
	fi; \
	petta_projection=$$(stat_from "$$petta_result" pathmap-projection-capture) || exit 1; \
	petta_materializations=$$(stat_from "$$petta_result" pathmap-materialize-native) || exit 1; \
	petta_lookups=$$(stat_from "$$petta_result" match-native-trie-lookup) || exit 1; \
	petta_candidates=$$(stat_from "$$petta_result" match-native-candidates) || exit 1; \
	petta_indexed=$$(stat_from "$$petta_result" pathmap-indexed-query) || exit 1; \
	if [ "$$petta_projection" -ne 0 ] || [ "$$petta_materializations" -ne 0 ] || \
	   [ "$$petta_lookups" -ne 0 ] || [ "$$petta_candidates" -ne 0 ] || \
	   [ "$$petta_indexed" -le 0 ]; then \
		echo "FAIL: PathMap PeTTa direct binding snapshot expected projection/materialization/native-lookup/native-candidates/indexed 0/0/0/0/>0, got $$petta_projection/$$petta_materializations/$$petta_lookups/$$petta_candidates/$$petta_indexed"; \
		exit 1; \
	fi; \
	echo "PASS: PathMap PeTTa logical-update snapshots use exact trie bindings without a native candidate shadow"

.PHONY: test-pathmap-interleaved-dispatch-view
test-pathmap-interleaved-dispatch-view: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@result=$$(CETTA_PATHMAP_QUERY_INDEX=1 CETTA_PATHMAP_PULL_CONSUMERS= \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--space-engine pathmap --lang he \
		tests/pathmap/probe_interleaved_mutation_full_refresh.metta 2>&1); \
	off_result=$$(CETTA_PATHMAP_QUERY_INDEX=0 CETTA_PATHMAP_PULL_CONSUMERS=1 \
		$(CETTA_BIN_INVOKE) --emit-runtime-stats --profile he-extended \
		--space-engine pathmap --lang he \
		tests/pathmap/probe_interleaved_mutation_full_refresh.metta 2>&1); \
	expected=$$(cat tests/pathmap/probe_interleaved_mutation_full_refresh.expected); \
	actual=$$(printf '%s\n' "$$result" | grep '^\[' || true); \
	off_actual=$$(printf '%s\n' "$$off_result" | grep '^\[' || true); \
	if [ "$$actual" != "$$expected" ] || [ "$$off_actual" != "$$expected" ]; then \
		echo "FAIL: PathMap interleaved dispatch-view answers"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$actual") | head -20; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$off_actual") | head -20; \
		exit 1; \
	fi; \
	stat() { \
		printf '%s\n' "$$1" | awk -v key="$$2" \
			'$$1 == "runtime-counter" && $$2 == key { print $$3; found=1 } END { if (!found) exit 1 }'; \
	}; \
	assert_le() { \
		value=$$(stat "$$result" "$$1") || exit 1; \
		if [ "$$value" -gt "$$2" ]; then \
			echo "FAIL: $$1 expected <= $$2, got $$value"; exit 1; \
		fi; \
	}; \
	assert_le eq-index-rebuild 3; \
	assert_le ty-index-rebuild 3; \
	assert_le pathmap-projection-capture 3; \
	assert_le pathmap-projection-rows 3000; \
	assert_le pathmap-shadow-refresh 3; \
	assert_le pathmap-shadow-refresh-atoms 3000; \
	assert_le pathmap-materialize-native 3; \
	assert_le pathmap-materialize-native-atoms 3000; \
	indexed=$$(stat "$$result" pathmap-indexed-query) || exit 1; \
	off_indexed=$$(stat "$$off_result" pathmap-indexed-query) || exit 1; \
	if [ "$$indexed" -lt 1 ] || [ "$$off_indexed" -ne 0 ]; then \
		echo "FAIL: forced/off indexed routing expected positive/zero, got $$indexed/$$off_indexed"; \
		exit 1; \
	fi; \
	echo "PASS: PathMap interleaved exact mutations retain dispatch descriptors, avoid projection churn, and preserve the forced/off indexed differential"
else
	@echo "INFO: PathMap interleaved dispatch-view gate requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-mm2-mork-program-space: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$($(CETTA_BIN_INVOKE) --lang mm2 tests/support/mm2_mork_program_space.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: mm2 MORK program-space lowering regression"; \
	else \
		echo "FAIL: mm2 MORK program-space lowering regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,mm2 MORK program-space lowering regression,$@)
endif

test-mm2-exec-basic: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --lang mm2 tests/mm2_exec_basic.mm2 2>&1); \
	if [ "$$result" = "$$(cat tests/mm2_exec_basic.expected)" ]; then \
		echo "PASS: mm2 direct execution seam"; \
	else \
		echo "FAIL: mm2 direct execution seam"; \
		diff <(cat tests/mm2_exec_basic.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,mm2 direct execution seam,$@)
endif

.PHONY: test-mm2-gslt-cli-v1 test-mm2-gslt-abi-differential-v1 \
	test-mm2-gslt-profile-v1

test-mm2-gslt-cli-v1: $(BIN)
	@set -e; \
	profiles=$$($(CETTA_BIN_INVOKE) --lang mm2 --list-profiles 2>&1); \
	if [ "$$(printf '%s\n' "$$profiles" | wc -l)" -ne 1 ] || \
	   ! printf '%s\n' "$$profiles" | grep -Eq '^gslt[[:space:]]'; then \
		echo 'FAIL: MM2 GSLT profile inventory'; \
		printf '%s\n' "$$profiles"; \
		exit 1; \
	fi; \
	for stem in \
		mm2_exec_basic \
		mm2_gslt_generated_exec \
		mm2_gslt_explicit_sources \
		mm2_kiss_count_groupby \
		mm2_gslt_scheduler_tie \
		mm2_gslt_unsupported_inert; do \
		actual=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt \
			"tests/$$stem.mm2" 2>&1); \
		expected=$$(cat "tests/$$stem.expected"); \
		if [ "$$actual" != "$$expected" ]; then \
			echo "FAIL: MM2 GSLT $$stem"; \
			diff <(printf '%s\n' "$$expected") \
				<(printf '%s\n' "$$actual") | head -20; \
			exit 1; \
		fi; \
	done; \
	native=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt \
		--space-engine native tests/mm2_gslt_generated_exec.mm2 2>&1); \
	automatic=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt \
		tests/mm2_gslt_generated_exec.mm2 2>&1); \
	if [ "$$native" != "$$automatic" ]; then \
		echo 'FAIL: explicit native-C MM2 GSLT realization'; \
		diff <(printf '%s\n' "$$automatic") \
			<(printf '%s\n' "$$native") | head -20; \
		exit 1; \
	fi; \
	if [ "$(MORK_BRIDGE_ACTIVE)" = 1 ]; then \
		pathmap=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt \
			--space-engine pathmap tests/mm2_gslt_generated_exec.mm2 2>&1); \
		if [ "$$pathmap" != "$$native" ]; then \
			echo 'FAIL: explicit Rust/PathMap via C ABI MM2 GSLT realization'; \
			diff <(printf '%s\n' "$$native") \
				<(printf '%s\n' "$$pathmap") | head -20; \
			exit 1; \
		fi; \
	fi; \
	step0_rc=0; \
	step0=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt --steps 0 \
		tests/mm2_gslt_generated_exec.mm2 2>&1) || step0_rc=$$?; \
	step0_expected=$$(printf '%s\n%s' \
		'(Mm2GsltStatus expired steps=0 residual-atoms=2)' \
		"$$(cat tests/mm2_gslt_generated_exec.step0.expected)"); \
	if [ "$$step0_rc" -ne 3 ] || [ "$$step0" != "$$step0_expected" ]; then \
		echo 'FAIL: MM2 GSLT exact zero-fuel expiration'; \
		printf '%s\n' "rc=$$step0_rc" "$$step0"; \
		exit 1; \
	fi; \
	step1_rc=0; \
	step1=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt --steps 1 \
		tests/mm2_gslt_generated_exec.mm2 2>&1) || step1_rc=$$?; \
	step1_expected=$$(printf '%s\n%s' \
		'(Mm2GsltStatus expired steps=1 residual-atoms=3)' \
		"$$(cat tests/mm2_gslt_generated_exec.step1.expected)"); \
	if [ "$$step1_rc" -ne 3 ] || [ "$$step1" != "$$step1_expected" ]; then \
		echo 'FAIL: MM2 GSLT exact one-step expiration'; \
		printf '%s\n' "rc=$$step1_rc" "$$step1"; \
		exit 1; \
	fi; \
	count=$$($(CETTA_BIN_INVOKE) --count-only --lang mm2 --profile gslt \
		tests/mm2_gslt_generated_exec.mm2 2>&1); \
	if [ "$$count" != 3 ]; then \
		echo "FAIL: MM2 GSLT support observation count (got $$count)"; \
		exit 1; \
	fi; \
	compile_rc=0; \
	compile_result=$$($(CETTA_BIN_INVOKE) --compile --lang mm2 \
		--profile gslt -e '(seed a)' 2>&1) || compile_rc=$$?; \
	if [ "$$compile_rc" -ne 2 ] || \
	   ! printf '%s\n' "$$compile_result" | \
		grep -Fq 'compile modes are not declared observations of --lang mm2 --profile gslt'; then \
		echo 'FAIL: MM2 GSLT rejects undeclared compile observation'; \
		printf '%s\n' "rc=$$compile_rc" "$$compile_result"; \
		exit 1; \
	fi; \
	echo 'PASS: MM2 GSLT CLI, internal continuation, inertness, exact fuel, and observation'

test-mm2-gslt-abi-differential-v1: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@set -e; \
	for source in \
		tests/mm2_exec_basic.mm2 \
		tests/mm2_kiss_add_remove.mm2 \
		tests/mm2_kiss_priority.mm2 \
		tests/mm2_kiss_fractal_priority.mm2 \
		tests/mm2_kiss_count_groupby.mm2 \
		tests/mm2_gslt_explicit_sources.mm2 \
		tests/mm2_gslt_scheduler_tie.mm2 \
		tests/support/mork_mm2/test3_var_binding.mm2 \
		tests/support/mork_mm2/test4_conjunctive.mm2 \
		tests/support/mork_mm2/test5_equal_pair.mm2 \
		tests/support/mork_mm2/test6_no_match.mm2 \
		tests/support/mork_mm2/test7_nested.mm2 \
		tests/support/mork_mm2/test8_multi_step.mm2 \
		tests/support/mork_mm2/test9_priority_ordering.mm2 \
		tests/support/mork_mm2/test10_conjunctive_wq.mm2 \
		tests/support/mork_mm2/test_add_constant.mm2 \
		tests/support/mork_mm2/test_add_simple.mm2 \
		tests/support/mork_mm2/test_bulk_remove.mm2 \
		tests/support/mork_mm2/test_remove_simple.mm2 \
		examples/bc_socrates_pln.mm2 \
		examples/bc_roman_4node.mm2 \
		examples/bench_pure_mm2_3hop_scored.mm2; do \
		abi=$$($(CETTA_BIN_INVOKE) --lang mm2 "$$source" 2>&1); \
		gslt=$$($(CETTA_BIN_INVOKE) --lang mm2 --profile gslt \
			"$$source" 2>&1); \
		if [ "$$abi" != "$$gslt" ]; then \
			echo "FAIL: upstream MM2 / authored GSLT differential $$source"; \
			diff <(printf '%s\n' "$$abi") \
				<(printf '%s\n' "$$gslt") | head -20; \
			exit 1; \
		fi; \
	done; \
	echo 'PASS: upstream MM2 (Rust/MORK via C ABI) and authored GSLT agree exactly on 22 admitted programs'; \
	CETTA_BIN=./$(BIN) python3 -m unittest \
		tests.test_mm2_to_metta.TranslatorGate.test_gslt_support_transform_v1_matches_native_corpus
else
	@echo 'SKIP: MM2 ABI/GSLT differential (MORK bridge is not active)'
endif

test-mm2-gslt-profile-v1: \
		test-gslt-support-transform-generation-v1 \
		test-gslt-support-transform-runtime \
		test-mm2-gslt-cli-v1 \
		test-mm2-gslt-abi-differential-v1
	@echo 'PASS: authored MM2 GSLT C profile'

test-import-mm2-mork-session-lowering: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he \
		tests/test_import_mm2_mork_session_lowering.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_import_mm2_mork_session_lowering.expected)" ]; then \
		echo "PASS: mork-space sugar over explicit handles"; \
	else \
		echo "FAIL: mork-space sugar over explicit handles"; \
		diff <(cat tests/test_import_mm2_mork_session_lowering.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,mork-space sugar over explicit handles,$@)
endif

test-mm2-kiss-suite: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	prep=$$($(CETTA_BIN_INVOKE) --quiet --profile he-extended --lang he tests/support/prepare_mm2_kiss_fruit_colors_act.metta 2>&1); \
	if [ -n "$$prep" ]; then \
		echo "FAIL: mm2 KISS ACT prepare"; \
		printf '%s\n' "$$prep"; \
		exit 1; \
	fi; \
	pass=0; fail=0; \
	for stem in mm2_kiss_add_remove mm2_kiss_priority mm2_kiss_fractal_priority mm2_kiss_count_groupby mm2_kiss_act_join; do \
		result=$$($(CETTA_BIN_INVOKE) --lang mm2 "tests/$$stem.mm2" 2>&1); \
		if [ "$$result" = "$$(cat "tests/$$stem.expected")" ]; then \
			echo "PASS: $$stem"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$stem"; \
			diff <(cat "tests/$$stem.expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	step_result=$$($(CETTA_BIN_INVOKE) --lang mm2 --steps 0 tests/mm2_kiss_fractal_priority.mm2 2>&1); \
	if [ "$$step_result" = "$$(cat tests/mm2_kiss_fractal_priority.step0.expected)" ]; then \
		echo "PASS: mm2_kiss_fractal_priority --steps 0"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2_kiss_fractal_priority --steps 0"; \
		diff <(cat tests/mm2_kiss_fractal_priority.step0.expected) <(echo "$$step_result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	step_result=$$($(CETTA_BIN_INVOKE) --lang mm2 --steps 1 tests/mm2_kiss_fractal_priority.mm2 2>&1); \
	if [ "$$step_result" = "$$(cat tests/mm2_kiss_fractal_priority.step1.expected)" ]; then \
		echo "PASS: mm2_kiss_fractal_priority --steps 1"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2_kiss_fractal_priority --steps 1"; \
		diff <(cat tests/mm2_kiss_fractal_priority.step1.expected) <(echo "$$step_result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	for stem in test_import_mm2_module_surface; do \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "tests/$$stem.metta" 2>&1); \
		if [ "$$result" = "$$(cat "tests/$$stem.expected")" ]; then \
			echo "PASS: $$stem"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$stem"; \
			diff <(cat "tests/$$stem.expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	if [ "$(ENABLE_PATHMAP_SPACE)" = "1" ]; then \
		stem=test_include_mm2_space_target; \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "tests/$$stem.metta" 2>&1); \
		if [ "$$result" = "$$(cat "tests/$$stem.expected")" ]; then \
			echo "PASS: $$stem"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$stem"; \
			diff <(cat "tests/$$stem.expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]
else
	$(call reexec_mork_bridge_or_skip,mm2 KISS raw example suite,$@)
endif

test-mork-surface-suite: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@pass=0; fail=0; \
	for stem in \
		test_mork_counterexample_loom_surface \
		test_mork_algebra_surface \
		test_mork_attached_exact_match_regression \
		test_mork_encoding_boundary_surface \
		test_mork_full_pipeline_surface \
		test_mork_handle_errors_surface \
		test_mork_kiss_examples \
		test_mork_lib_surface \
		test_mork_mm2_metta_showcase \
		test_mork_native_handle_fresh_id_regression \
		test_mork_open_act_surface \
		test_mork_overlay_zipper_surface \
		test_mork_product_zipper_surface \
		test_mork_zipper_surface \
		test_new_space_mork_surface \
		test_step_space_surface; do \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he "tests/$$stem.metta" 2>&1); \
		if [ "$$result" = "$$(cat "tests/$$stem.expected")" ]; then \
			echo "PASS: $$stem"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$stem"; \
			diff <(cat "tests/$$stem.expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]
else
	$(call reexec_mork_bridge_or_skip,mork surface suite,$@)
endif

test-mork-runtime-stats-isolation:
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" = "1" ] || [ -n "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		if [ "$(ENABLE_RUNTIME_STATS)" != "1" ]; then \
			echo "INFO: mork runtime-stats isolation requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"; \
			$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-isolation-body; \
		else \
			$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-isolation-body; \
		fi; \
	else \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
			bridge_build=mork; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
			echo "INFO: mork runtime-stats isolation requires the MORK bridge; re-running with BUILD=$$bridge_build and ENABLE_RUNTIME_STATS=1"; \
			$(MAKE) BUILD=$$bridge_build ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-isolation-body; \
		else \
			if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
				echo "SKIP: mork runtime-stats isolation (no MORK bridge manifest configured)"; \
			else \
				echo "SKIP: mork runtime-stats isolation (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
			fi; \
		fi; \
	fi

test-mork-runtime-stats-isolation-body: $(BIN)
	@result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_mork_runtime_stats_isolation.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_runtime_stats_isolation.expected)" ]; then \
		echo "PASS: test_mork_runtime_stats_isolation"; \
	else \
		echo "FAIL: test_mork_runtime_stats_isolation"; \
		diff <(cat tests/test_mork_runtime_stats_isolation.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-closed-stream-fastpath: $(BIN)
	@result=$$($(CETTA_BIN_INVOKE) --quiet --profile he-extended --lang he tests/test_closed_stream_fastpath.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_closed_stream_fastpath.expected)" ]; then \
		echo "PASS: test_closed_stream_fastpath"; \
	else \
		echo "FAIL: test_closed_stream_fastpath"; \
		diff <(cat tests/test_closed_stream_fastpath.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-closed-stream-runtime-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@result=$$($(CETTA_BIN_INVOKE) --quiet --profile he-extended --lang he tests/test_closed_stream_runtime_stats.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_closed_stream_runtime_stats.expected)" ]; then \
		echo "PASS: test_closed_stream_runtime_stats"; \
	else \
		echo "FAIL: test_closed_stream_runtime_stats"; \
		diff <(cat tests/test_closed_stream_runtime_stats.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	@echo "INFO: closed-stream runtime-stats regression requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-mm2-var-scope-across-exprs: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --lang mm2 "$(MORK_MM2_VAR_SCOPE)" 2>&1); \
	if [ "$$result" = "$$(cat tests/mm2_var_scope_across_exprs.expected)" ]; then \
		echo "PASS: mm2 variable scope is per-expression"; \
	else \
		echo "FAIL: mm2 variable scope leaked across expressions"; \
		diff <(cat tests/mm2_var_scope_across_exprs.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,mm2 variable scope is per-expression,$@)
endif

test-mm2-conformance-var-binding: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --lang mm2 "$(MORK_MM2_TEST3)" 2>&1); \
	if [ "$$result" = "$$(cat tests/mm2_conformance_var_binding.expected)" ]; then \
		echo "PASS: mm2 var-binding conformance seam"; \
	else \
		echo "FAIL: mm2 var-binding conformance seam"; \
		diff <(cat tests/mm2_conformance_var_binding.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,mm2 var-binding conformance seam,$@)
endif

test-mm2-conformance-lean-suite: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	pass=0; fail=0; \
	for case in \
		"$(MORK_MM2_TEST4):tests/mm2_conformance_test4.expected" \
		"$(MORK_MM2_TEST5):tests/mm2_conformance_test5.expected" \
		"$(MORK_MM2_TEST6):tests/mm2_conformance_test6.expected" \
		"$(MORK_MM2_TEST7):tests/mm2_conformance_test7.expected" \
		"$(MORK_MM2_TEST8):tests/mm2_conformance_test8.expected" \
		"$(MORK_MM2_TEST9):tests/mm2_conformance_test9.expected" \
		"$(MORK_MM2_TEST10):tests/mm2_conformance_test10.expected"; do \
		file=$${case%%:*}; expected=$${case#*:}; \
		result=$$($(CETTA_BIN_INVOKE) --lang mm2 "$$file" 2>&1); \
		if [ "$$result" = "$$(cat "$$expected")" ]; then \
			echo "PASS: mm2 lean conformance $$(basename "$$file")"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: mm2 lean conformance $$(basename "$$file")"; \
			diff <(cat "$$expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]
else
	$(call reexec_mork_bridge_or_skip,mm2 lean conformance suite,$@)
endif

test-mm2-sink-suite: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	pass=0; fail=0; \
	for case in \
		"$(MORK_MM2_SINK_ADD_CONSTANT):tests/mm2_sink_add_constant.expected" \
		"$(MORK_MM2_SINK_ADD_SIMPLE):tests/mm2_sink_add_simple.expected" \
		"$(MORK_MM2_SINK_REMOVE_SIMPLE):tests/mm2_sink_remove_simple.expected" \
		"$(MORK_MM2_SINK_BULK_REMOVE):tests/mm2_sink_bulk_remove.expected" \
		"$(MORK_MM2_SINK_COUNT_SIMPLE):tests/mm2_sink_count_simple.expected" \
		"$(MORK_MM2_SINK_HEAD_LIMIT):tests/mm2_sink_head_limit.expected"; do \
		file=$${case%%:*}; expected=$${case#*:}; \
		result=$$($(CETTA_BIN_INVOKE) --lang mm2 "$$file" 2>&1); \
		if [ "$$result" = "$$(cat "$$expected")" ]; then \
			echo "PASS: mm2 sink suite $$(basename "$$file")"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: mm2 sink suite $$(basename "$$file")"; \
			diff <(cat "$$expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]
else
	$(call reexec_mork_bridge_or_skip,mm2 sink suite,$@)
endif

test-pathmap-conjunction-init: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@for source in $(PATHMAP_RUNTIME_STATS_METTA_TESTS); do \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --space-engine pathmap --lang he "$$source" 2>&1); \
		if [ "$$result" = "$$(cat "$${source%.metta}.expected")" ]; then \
			echo "PASS: pathmap runtime-stats $$(basename "$$source")"; \
		else \
			echo "FAIL: pathmap runtime-stats $$(basename "$$source")"; \
			diff <(cat "$${source%.metta}.expected") <(echo "$$result") | head -20; \
			exit 1; \
		fi; \
	done
else
	@echo "INFO: pathmap conjunction init regression requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif
else
	$(call reexec_pathmap_bridge_or_skip,pathmap conjunction init regression,$@)
endif

test-pathmap-bridge-v2: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(CETTA_PATHMAP_QUERY_INDEX=0 $(CETTA_BIN_INVOKE) --profile he-extended --space-engine pathmap --lang he tests/test_pathmap_imported_bridge_v2.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: pathmap bridge v2 regression"; \
	else \
		echo "FAIL: pathmap bridge v2 regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	@echo "INFO: pathmap bridge v2 regression requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif
else
	$(call reexec_pathmap_bridge_or_skip,pathmap bridge v2 regression,$@)
endif

test-pathmap-long-string-regression: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]'); \
	result=$$($(CETTA_BIN_INVOKE) --space-engine pathmap --lang he tests/support/pathmap_imported_long_string_probe.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: pathmap long-string regression"; \
	else \
		echo "FAIL: pathmap long-string regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_pathmap_bridge_or_skip,pathmap long-string regression,$@)
endif

test-pathmap-match-chain: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --space-engine pathmap --lang he tests/test_match_chain_imported_regression.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_match_chain_imported_regression.expected)" ]; then \
		echo "PASS: pathmap nested-match chain regression"; \
	else \
		echo "FAIL: pathmap nested-match chain regression"; \
		diff <(cat tests/test_match_chain_imported_regression.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_pathmap_bridge_or_skip,pathmap nested-match chain regression,$@)
endif

test-pathmap-match-chain-v3: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --space-engine pathmap --lang he tests/test_imported_match_chain_conjunction_lowering.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_imported_match_chain_conjunction_lowering.expected)" ]; then \
		echo "PASS: pathmap nested-match conjunction lowering regression"; \
	else \
		echo "FAIL: pathmap nested-match conjunction lowering regression"; \
		diff <(cat tests/test_imported_match_chain_conjunction_lowering.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	@echo "INFO: pathmap nested-match conjunction lowering regression requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif
else
	$(call reexec_pathmap_bridge_or_skip,pathmap nested-match conjunction lowering regression,$@)
endif

test-mork-lib-pathmap: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --space-engine pathmap --lang he tests/support/mork_lib_pathmap_imported.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: mork lib pathmap probe"; \
	else \
		echo "FAIL: mork lib pathmap probe"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_pathmap_bridge_or_skip,mork lib pathmap probe,$@)
endif

test-mork-open-act: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_mork_open_act_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_open_act_surface.expected)" ]; then \
		echo "PASS: mork open-act probe"; \
	else \
		echo "FAIL: mork open-act probe"; \
		diff <(cat tests/test_mork_open_act_surface.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,mork open-act probe,$@)
endif

test-pretty-vars-flags: $(BIN)
	@raw_result=$$($(CETTA_BIN_INVOKE) --raw-vars --profile he-extended --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	default_result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	pretty_result=$$($(CETTA_BIN_INVOKE) --pretty-vars --profile he-extended --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	if printf '%s\n' "$$raw_result" | grep -Fq '#'; then \
		:; \
	else \
		echo "FAIL: raw-vars did not preserve raw suffixes"; \
		printf '%s\n' "$$raw_result"; \
		exit 1; \
	fi; \
	if [ "$$default_result" = "$$raw_result" ]; then \
		:; \
	else \
		echo "FAIL: default non-tty output changed"; \
		diff <(echo "$$raw_result") <(echo "$$default_result") | head -20; \
		exit 1; \
	fi; \
	if [ "$$pretty_result" = "$$(cat tests/test_pretty_vars_surface.pretty.expected)" ]; then \
		echo "PASS: pretty-vars flags"; \
	else \
		echo "FAIL: pretty-vars output mismatch"; \
		diff <(cat tests/test_pretty_vars_surface.pretty.expected) <(echo "$$pretty_result") | head -20; \
		exit 1; \
	fi

test-pretty-namespaces-flags: $(BIN)
	@raw_result=$$($(CETTA_BIN_INVOKE) --raw-namespaces --profile he-extended --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	default_result=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	pretty_result=$$($(CETTA_BIN_INVOKE) --pretty-namespaces --profile he-extended --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	if printf '%s\n' "$$raw_result" | grep -Fq 'mork:open-act' && \
	   printf '%s\n' "$$raw_result" | grep -Fq 'runtime:test-module' && \
	   printf '%s\n' "$$raw_result" | grep -Fq '$mork:space'; then \
		:; \
	else \
		echo "FAIL: raw-namespaces did not preserve canonical separators"; \
		printf '%s\n' "$$raw_result"; \
		exit 1; \
	fi; \
	if [ "$$default_result" = "$$raw_result" ]; then \
		:; \
	else \
		echo "FAIL: default non-tty namespace output changed"; \
		diff <(echo "$$raw_result") <(echo "$$default_result") | head -20; \
		exit 1; \
	fi; \
	if [ "$$pretty_result" = "$$(cat tests/test_pretty_namespaces_surface.pretty.expected)" ]; then \
		echo "PASS: pretty-namespaces flags"; \
	else \
		echo "FAIL: pretty-namespaces output mismatch"; \
		diff <(cat tests/test_pretty_namespaces_surface.pretty.expected) <(echo "$$pretty_result") | head -20; \
		exit 1; \
	fi

prepare-bio-eqtl-act: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@./scripts/bench_mork_act_eqtl.sh prepare
	@echo "PASS: prepared runtime/bench_eqtl_for_mining.act"
else
	$(call reexec_mork_bridge_or_skip,bio eqtl ACT prepare,$@)
endif

bench-bio-eqtl-act-modes: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	./scripts/bench_mork_act_eqtl.sh all
else
	$(call reexec_mork_bridge_or_skip,bio eqtl ACT benchmark,$@)
endif

prepare-bio-1m-act: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	result=$$($(CETTA_BIN_INVOKE) --quiet --profile he-extended --lang he tests/support/prepare_bio_1m_act.metta 2>&1); \
	if [ -z "$$result" ]; then \
		echo "PASS: prepared runtime/bench_bio_1m.act"; \
	else \
		echo "FAIL: bio 1m ACT prepare"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi
else
	$(call reexec_mork_bridge_or_skip,bio 1m ACT prepare,$@)
endif

bench-bio-1m-act-attach: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	./scripts/bench_mork_act_bio_1m_attach.sh
else
	$(call reexec_mork_bridge_or_skip,bio 1m ACT attached benchmark,$@)
endif

bench-bio-1m-act-modes: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@ \
	echo "NOTE: attached ACT is the verified 1.4M path; combined source/materialize comparison remains experimental"; \
	./scripts/bench_mork_act_bio_1m_attach.sh
else
	$(call reexec_mork_bridge_or_skip,bio 1m ACT benchmark,$@)
endif

test-duplicate-multiplicity-backends: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	for backend in native native-candidate-exact pathmap; do \
		result=$$($(CETTA_BIN_INVOKE) --profile he-extended --space-engine "$$backend" --lang he tests/support/duplicate_multiplicity_probe.metta 2>&1); \
		if [ "$$result" = "$$expected" ]; then \
			echo "PASS: $$backend duplicate multiplicity probe"; \
		else \
			echo "FAIL: $$backend duplicate multiplicity probe"; \
			diff <(echo "$$expected") <(echo "$$result") | head -20; \
			exit 1; \
		fi; \
	done
else
	$(call reexec_pathmap_bridge_or_skip,duplicate multiplicity backend probe,$@)
endif

test-runtime-stats-cli: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@result=$$($(CETTA_BIN_INVOKE) --emit-runtime-stats --quiet --lang he tests/support/runtime_stats_cli_probe.metta 2>&1 >/dev/null); \
	if printf '%s\n' "$$result" | grep -Fq 'runtime-counter query-equations ' && \
	   printf '%s\n' "$$result" | grep -Fq 'runtime-counter rename-vars ' && \
	   ! printf '%s\n' "$$result" | grep -Fq '[ok]'; then \
		echo "PASS: runtime stats cli flags"; \
	else \
		echo "FAIL: runtime stats cli flags"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi
else
	@echo "INFO: runtime stats cli flags requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-rhocalc-runtime-stats: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@result=$$($(CETTA_BIN_INVOKE) --emit-runtime-stats --quiet --num-threads 2 --lang rhocalc tests/rhocalc_run/core_comm_run.mrho 2>&1 >/dev/null); \
	if printf '%s\n' "$$result" | grep -Fq 'runtime-counter parallel-queue-push ' && \
	   printf '%s\n' "$$result" | grep -Eq 'runtime-counter parallel-worker-task [1-9][0-9]*' && \
	   printf '%s\n' "$$result" | grep -Eq 'runtime-counter rho-async-endpoint-publish [1-9][0-9]*' && \
	   printf '%s\n' "$$result" | grep -Eq 'runtime-counter rho-async-endpoint-match [1-9][0-9]*' && \
	   ! printf '%s\n' "$$result" | grep -Fq '[ok]'; then \
		echo "PASS: rhocalc public runtime stats"; \
	else \
		echo "FAIL: rhocalc public runtime stats"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi
else
	@echo "INFO: rhocalc public runtime stats requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

test-help-flags: $(BIN)
	@help_long=$$($(CETTA_BIN_INVOKE) --help 2>&1); \
	help_short=$$($(CETTA_BIN_INVOKE) -h 2>&1); \
	lang_list=$$($(CETTA_BIN_INVOKE) --list-languages 2>&1); \
	if printf '%s\n' "$$help_long" | grep -Fq 'usage: cetta [--lang <name>] [--syntax <metta|mrho|rho>] <file>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --translate --lang A [--syntax S] --lang B [--syntax T] <file>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --rho-reduction-limit <n> <file>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --rho-scheduler <canonical|rotating> <file>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --num-threads <n> <file>' && \
	   HELP_TEXT="$$help_long" python3 -c "from pathlib import Path; import os, re, sys; text = Path('src/main.c').read_text(); accepted = sorted(set(re.findall(r'strcmp\\(argv\\[i\\],\\s*\\\"(--[^\\\"= ]+)\\\"\\)\\s*==\\s*0', text))); documented = sorted(set(re.findall(r'--[A-Za-z0-9-]+', os.environ['HELP_TEXT']))); sys.exit(0 if accepted == documented else 1)" >/dev/null && \
	   printf '%s\n' "$$lang_list" | grep -Fq 'Strict-core rho-calculus reducer to quiescence' && \
	   printf '%s\n' "$$lang_list" | grep -Fq 'Experimental productive and recurrent MeTTa candidate' && \
	   printf '%s\n' "$$lang_list" | grep -Fq 'Experimental proof-relevant interaction and cost language' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --lang zero <file.metta> # direct (eval subject) and (zero-query pattern template) requests; ! is not Zero syntax' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --lang zerouv <file.metta> # productive step, finite paths, authored control, and recurrence articles' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --lang metta-interact <file.metta> # revisioned events, continuations, and inspectable cost' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --lang mm2 [--steps <n>] <file.mm2>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --lang mm2 --profile gslt [--space-engine <native|pathmap>] [--steps <n>] <file.mm2>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'upstream support-valued MORK ABI; least serialized exec atom first' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'native selects native C; pathmap selects Rust/PathMap via the C ABI' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'plain mm2 retains upstream MORK through its C ABI; gslt selects authored semantics' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'unsupported exec directives remain inert in the final support' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'least full directive by MORK compact-expression byte order' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cached by alpha-normalized structure' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'exact external upper bound (default 1000000000000000); zero executes nothing' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'GSLT expiration is explicit and exits with status 3' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --prefer-rationals <file.metta>' && \
	   [ "$$help_long" = "$$help_short" ]; then \
		echo "PASS: cli help flags"; \
	else \
		echo "FAIL: cli help flags"; \
		printf '%s\n' '--- --help ---'; \
		printf '%s\n' "$$help_long"; \
		printf '%s\n' '--- --list-languages ---'; \
		printf '%s\n' "$$lang_list"; \
		printf '%s\n' '--- -h ---'; \
		printf '%s\n' "$$help_short"; \
		exit 1; \
	fi

probe-imported-conjunction-lanes: $(BIN)
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@ \
	$(CETTA_BIN_INVOKE) --profile he-extended --space-engine pathmap --lang he \
		tests/support/imported_conjunction_lane_probe.metta
else
	$(call reexec_pathmap_bridge_or_skip,imported conjunction lane probe,$@)
endif

# Slow: regenerate .expected files from HE CLI oracle.
# Run ONE AT A TIME to avoid OOM. Requires a metta CLI on PATH or a local
# hyperon env under miniforge/miniconda.
oracle-refresh:
	@for f in tests/test_*.metta tests/he_*.metta; do \
		[ -f "$$f" ] || continue; \
		if [ "$(ENABLE_PYTHON)" != "1" ] && \
		   { [ "$$f" = "tests/test_py_ops_surface.metta" ] || \
		     [ "$$f" = "tests/test_import_foreign_python_file.metta" ] || \
		     [ "$$f" = "tests/test_import_foreign_pkg_error.metta" ] || \
		     [ "$$f" = "tests/test_namespace_sugar_guardrails.metta" ]; }; then \
			echo "SKIP: $$f (requires a Python-enabled build)"; \
			continue; \
		fi; \
		exp="$${f%.metta}.expected"; \
		echo "oracle: $$f"; \
		if [ -n "$$HE_METTA_BIN" ]; then \
			timeout 30 "$$HE_METTA_BIN" "$$f" > "$$exp" 2>&1; \
		elif command -v metta >/dev/null 2>&1; then \
			timeout 30 metta "$$f" > "$$exp" 2>&1; \
		elif [ -x "$$HOME/miniforge3/envs/hyperon/bin/metta" ]; then \
			timeout 30 "$$HOME/miniforge3/envs/hyperon/bin/metta" "$$f" > "$$exp" 2>&1; \
		elif [ -x "$$HOME/miniconda3/envs/hyperon/bin/metta" ]; then \
			timeout 30 "$$HOME/miniconda3/envs/hyperon/bin/metta" "$$f" > "$$exp" 2>&1; \
		elif [ -f "$$HOME/miniforge3/bin/activate" ]; then \
			source "$$HOME/miniforge3/bin/activate" hyperon && \
			timeout 30 metta "$$f" > "$$exp" 2>&1; \
		elif [ -f "$$HOME/miniconda3/bin/activate" ]; then \
			source "$$HOME/miniconda3/bin/activate" hyperon && \
			timeout 30 metta "$$f" > "$$exp" 2>&1; \
		else \
			echo "FAIL: no metta CLI found (set HE_METTA_BIN or install/activate a hyperon env)" >&2; \
			exit 1; \
		fi; \
	done; \
	echo "done — .expected files updated"

# Benchmark: forward chaining depth 3. Uses --count-only to avoid giant stdout.
# Checks theorem count matches the current pinned regression number.
bench-d3: $(BIN)
	@count=$$($(CETTA_BIN_INVOKE) --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
	echo "depth-3 total: $$count theorems"; \
	if [ "$$count" = "3421" ]; then \
		echo "PASS: theorem count matches"; \
	else \
		echo "FAIL: expected 3421, got $$count"; exit 1; \
	fi

bench-d3-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --space-engine "$$backend" --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 total: $$count theorems"; \
		if [ "$$count" = "3421" ]; then \
			echo "PASS: $$backend theorem count matches"; \
		else \
			echo "FAIL: expected 3421, got $$count for $$backend"; exit 1; \
		fi; \
	done

probe-d3-nodup: $(BIN)
	@count=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
	echo "depth-3 nodup probe: $$count theorems"; \
	if [ "$$count" = "2759" ]; then \
		echo "PASS: nodup theorem count matches"; \
	else \
		echo "FAIL: expected 2759, got $$count"; exit 1; \
	fi

probe-d3-nodup-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he --space-engine "$$backend" --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 nodup probe: $$count theorems"; \
		if [ "$$count" = "2759" ]; then \
			echo "PASS: $$backend nodup theorem count matches"; \
		else \
			echo "FAIL: expected 2759, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-d3-nodup: probe-d3-nodup

bench-d3-nodup-backends: probe-d3-nodup-backends

probe-fc-native-memory: $(BIN)
ifeq ($(ENABLE_RUNTIME_STATS),1)
	@echo "=== native FC memory buckets (depth 3 duplicate vs nodup) ==="; \
	out=$$($(CETTA_BIN_INVOKE) --profile he-extended --lang he tests/support/fc_native_memory_probe.metta 2>&1); \
	status=$$?; \
	printf '%s\n' "$$out" | grep -E '^(=== fc-native-memory-probe ===|\\(fc3-(dup|nodup).+\\))$$' || true; \
	if [ $$status -ne 0 ]; then \
		printf '%s\n' "$$out" | tail -20; \
		exit $$status; \
	fi; \
	echo; \
	echo "=== native FC operational frontier (depth 4 nodup) ==="; \
	out=$$(/usr/bin/time -f 'elapsed=%e rss_kb=%M exit=%x' timeout 300 $(CETTA_BIN_INVOKE) --count-only tests/nil_pc_fc_d4_nodup.metta 2>&1 >/dev/null); \
	printf '%s\n' "$$out" | tail -20; \
	echo; \
	echo "=== native FC operational frontier (depth 4 duplicate) ==="; \
	out=$$(/usr/bin/time -f 'elapsed=%e rss_kb=%M exit=%x' timeout 300 $(CETTA_BIN_INVOKE) --count-only tests/nil_pc_fc_d4.metta 2>&1 >/dev/null); \
	printf '%s\n' "$$out" | tail -20
else
	@echo "INFO: native FC memory probe requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"
	@$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $@
endif

bench-conj-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --space-engine "$$backend" --count-only benchmarks/bench_conjunction_he.metta 2>&1 | tail -1); \
		echo "$$backend conjunction total: $$count results"; \
		if [ "$$count" = "216" ]; then \
			echo "PASS: $$backend conjunction count matches"; \
		else \
			echo "FAIL: expected 216, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-conj12-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --space-engine "$$backend" --count-only benchmarks/bench_conjunction12_he.metta 2>&1 | tail -1); \
		echo "$$backend conjunction12 total: $$count results"; \
		if [ "$$count" = "20736" ]; then \
			echo "PASS: $$backend conjunction12 count matches"; \
		else \
			echo "FAIL: expected 20736, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-join8-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --space-engine "$$backend" --count-only benchmarks/bench_matchjoin8_he.metta 2>&1 | tail -1); \
		echo "$$backend join8 total: $$count results"; \
		if [ "$$count" = "4096" ]; then \
			echo "PASS: $$backend join8 count matches"; \
		else \
			echo "FAIL: expected 4096, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-join12-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --space-engine "$$backend" --count-only tests/bench_matchjoin12_he.metta 2>&1 | tail -1); \
		echo "$$backend join12 total: $$count results"; \
		if [ "$$count" = "20736" ]; then \
			echo "PASS: $$backend join12 count matches"; \
		else \
			echo "FAIL: expected 20736, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-conj12-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh benchmarks/bench_conjunction12_he.metta "$$backend"; \
		echo "---"; \
	done

bench-dup-conj-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$($(CETTA_BIN_INVOKE) --space-engine "$$backend" --count-only benchmarks/bench_duplicate_conjunction_he.metta 2>&1 | tail -1); \
		echo "$$backend duplicate conjunction total: $$count results"; \
		if [ "$$count" = "4096" ]; then \
			echo "PASS: $$backend duplicate conjunction count matches"; \
		else \
			echo "FAIL: expected 4096, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-dup-conj-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh benchmarks/bench_duplicate_conjunction_he.metta "$$backend"; \
		echo "---"; \
	done

bench-join8-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh benchmarks/bench_matchjoin8_he.metta "$$backend"; \
		echo "---"; \
	done

bench-join12-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh tests/bench_matchjoin12_he.metta "$$backend"; \
		echo "---"; \
	done

bench-d4: $(BIN)
	@out=$$(timeout 600 $(CETTA_BIN_INVOKE) --count-only tests/nil_pc_fc_d4.metta 2>&1); \
	status=$$?; \
	count=$$(printf '%s\n' "$$out" | tail -1); \
	echo "depth-4 total: $$count theorems"; \
	if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: depth-4 produced a count"; \
	else \
		printf '%s\n' "$$out" | tail -5; \
		echo "FAIL: depth-4 did not produce a valid count"; exit 1; \
	fi

bench-d4-nodup: $(BIN)
	@out=$$(timeout 600 $(CETTA_BIN_INVOKE) --count-only tests/nil_pc_fc_d4_nodup.metta 2>&1); \
	status=$$?; \
	count=$$(printf '%s\n' "$$out" | tail -1); \
	echo "depth-4 nodup total: $$count theorems"; \
	if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: depth-4 nodup produced a count"; \
	else \
		printf '%s\n' "$$out" | tail -5; \
		echo "FAIL: depth-4 nodup did not produce a valid count"; exit 1; \
	fi

bench-d4-backends: $(BIN)
	@env $(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV)=1 \
		python3 scripts/bench_d4_progress.py "$(CETTA_SCRIPT_BIN)" \
		tests/nil_pc_fc_d4.metta --backends "$(subst $(space),$(comma),$(strip $(SPACE_ENGINES)))" \
		--duration "$(D4_PROBE_TIMEOUT)" --output runtime/d4_progress_current.json

bench-d4-nodup-backends: $(BIN)
	@env $(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV)=1 \
		python3 scripts/bench_d4_progress.py "$(CETTA_SCRIPT_BIN)" \
		tests/nil_pc_fc_d4_nodup.metta --backends "$(subst $(space),$(comma),$(strip $(SPACE_ENGINES)))" \
		--duration "$(D4_PROBE_TIMEOUT)" --output runtime/d4_nodup_progress_current.json

probe-d4-nodup-capability-backends: $(BIN)
	@env $(CETTA_GSLT_MATCH_CHAIN_TRACE_ENV)=1 \
		python3 scripts/bench_d4_progress.py "$(CETTA_SCRIPT_BIN)" \
		tests/nil_pc_fc_d4_nodup.metta --backends "$(subst $(space),$(comma),$(strip $(SPACE_ENGINES)))" \
		--duration "$(D4_PROBE_TIMEOUT)" --mode census \
		--output runtime/d4_nodup_capability_current.json

bench-compare-petta: $(BIN)
	@./scripts/bench_compare_cetta_petta.sh

bench-mork-add-interface: $(BIN)
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@./scripts/bench_mork_add_interface.sh
else
	$(call reexec_mork_bridge_or_skip,mork add interface benchmark,$@)
endif

bench-mork-add-interface-timing:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_TIMING=1 bench-mork-add-interface

bench-mork-bridge-add:
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_add
	@for n in $(or $(BENCH_MORK_BRIDGE_SIZES),1000 10000 100000); do \
		echo "=== bridge-add $$n ==="; \
		./runtime/bench_mork_bridge_add "$$n" $(or $(BENCH_MORK_BRIDGE_REPEAT),3); \
		echo; \
	done
else
	$(call reexec_mork_bridge_or_skip,mork low-level bridge add benchmark,$@)
endif

bench-mork-bridge-query:
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_query
	@for n in $(or $(BENCH_MORK_BRIDGE_QUERY_SIZES),1000 10000 100000); do \
		echo "=== bridge-query $$n ==="; \
		./runtime/bench_mork_bridge_query "$$n" $(or $(BENCH_MORK_BRIDGE_QUERY_REPEAT),3); \
		echo; \
	done
else
	$(call reexec_mork_bridge_or_skip,mork low-level bridge query benchmark,$@)
endif

bench-mork-bridge-scalar-cursor:
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_scalar_cursor
	@for n in $(or $(BENCH_MORK_BRIDGE_SCALAR_CURSOR_SIZES),1000 10000 100000); do \
		echo "=== bridge-scalar-cursor $$n ==="; \
		./runtime/bench_mork_bridge_scalar_cursor "$$n" $(or $(BENCH_MORK_BRIDGE_SCALAR_CURSOR_REPEAT),3); \
		echo; \
	done
else
	$(call reexec_mork_bridge_or_skip,mork low-level bridge scalar and cursor benchmark,$@)
endif

bench-mork-bridge-space-ops:
ifeq ($(MORK_BRIDGE_ACTIVE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_space_ops
	@for n in $(or $(BENCH_MORK_BRIDGE_SPACE_OPS_SIZES),1000 10000 100000); do \
		echo "=== bridge-space-ops $$n ==="; \
		./runtime/bench_mork_bridge_space_ops "$$n" $(or $(BENCH_MORK_BRIDGE_SPACE_OPS_REPEAT),3); \
		echo; \
	done
else
	$(call reexec_mork_bridge_or_skip,mork low-level bridge ACT and algebra benchmark,$@)
endif

bench-closed-stream-fastpath: $(BIN)
	@./scripts/bench_closed_stream_fastpath.sh $(or $(BENCH_CLOSED_STREAM_SIZES),1000 10000 100000) $(or $(BENCH_CLOSED_STREAM_REPEAT),3)

tail-recursion-check: $(BIN)
	@result=$$($(CETTA_BIN_INVOKE) tests/tail_recursion_deep.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/tail_recursion_deep.expected)" ]; then \
		echo "PASS: deep tail recursion under explicit fuel"; \
	else \
		echo "FAIL: deep tail recursion mismatch"; \
		diff <(cat tests/tail_recursion_deep.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-width-tuple-stack: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) ./scripts/test_width_tuple_stack_regression.sh $(or $(WIDTH_TUPLE_STACK_WIDTH),50000)

test-wide-typed-call-stack: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) ./scripts/test_wide_typed_call_stack_regression.sh $(or $(WIDE_TYPED_CALL_STACK_WIDTH),20000)

# LLVM IR validation: verify emitted IR with an available LLVM parser.
compile-test: $(BIN)
	@verifier=; \
	if command -v "$(LLVM_OPT)" >/dev/null 2>&1; then \
		verifier=opt; \
	elif command -v "$(LLVM_CLANG)" >/dev/null 2>&1; then \
		verifier=clang; \
	else \
		echo "FAIL: compile-test requires $(LLVM_OPT) or $(LLVM_CLANG)"; \
		exit 1; \
	fi; \
	pass=0; fail=0; \
	for f in tests/test_equations.metta tests/test_basic_eval.metta tests/test_disc_trie.metta tests/test_compile_arity.metta tests/test_compile_hybrid_interop.metta; do \
		[ -f "$$f" ] || continue; \
		ir=$$($(CETTA_BIN_INVOKE) --compile "$$f" 2>&1); \
		if { \
			if [ "$$verifier" = opt ]; then \
				printf '%s\n' "$$ir" | "$(LLVM_OPT)" -S -o /dev/null 2>/dev/null; \
			else \
				printf '%s\n' "$$ir" | "$(LLVM_CLANG)" -Wno-override-module -x ir -c -o /dev/null - 2>/dev/null; \
			fi; \
		}; then \
			if [ "$$f" = "tests/test_compile_arity.metta" ]; then \
				if printf '%s\n' "$$ir" | grep -q 'define void @cetta_foo__arity_1' && \
				   printf '%s\n' "$$ir" | grep -q 'define void @cetta_foo__arity_2'; then \
					echo "IR-OK: $$f"; pass=$$((pass + 1)); \
				else \
					echo "IR-FAIL: $$f"; \
					echo "missing distinct compiled symbols for foo/1 and foo/2"; \
					fail=$$((fail + 1)); \
				fi; \
			elif [ "$$f" = "tests/test_compile_hybrid_interop.metta" ]; then \
				call_origin_body=$$(printf '%s\n' "$$ir" | awk '/define void @cetta_call_2dorigin__arity_0/{flag=1} flag{print} /^}/&&flag{exit}'); \
				call_id_body=$$(printf '%s\n' "$$ir" | awk '/define void @cetta_call_2did__arity_1/{flag=1} flag{print} /^}/&&flag{exit}'); \
				call_plus_body=$$(printf '%s\n' "$$ir" | awk '/define void @cetta_call_2dplus__arity_1/{flag=1} flag{print} /^}/&&flag{exit}'); \
				if printf '%s\n' "$$call_origin_body" | grep -Fq 'call %ResultSet* @cetta_rs_alloc()' && \
				   printf '%s\n' "$$call_origin_body" | grep -Fq 'call void @cetta_origin__arity_0' && \
				   printf '%s\n' "$$call_origin_body" | grep -Fq 'call void @metta_eval' && \
				   printf '%s\n' "$$call_id_body" | grep -Fq 'call %ResultSet* @cetta_rs_alloc()' && \
				   printf '%s\n' "$$call_id_body" | grep -Fq 'call void @cetta_id1__arity_1' && \
				   printf '%s\n' "$$call_id_body" | grep -Fq 'call void @metta_eval' && \
				   printf '%s\n' "$$call_plus_body" | grep -Fq '@str__2b' && \
				   printf '%s\n' "$$call_plus_body" | grep -Fq 'call void @metta_eval' && \
				   ! printf '%s\n' "$$call_plus_body" | grep -Fq 'call %ResultSet* @cetta_rs_alloc()'; then \
					echo "IR-OK: $$f"; pass=$$((pass + 1)); \
				else \
					echo "IR-FAIL: $$f"; \
					echo "missing hybrid direct-call/metta_eval interop evidence"; \
					fail=$$((fail + 1)); \
				fi; \
			else \
				echo "IR-OK: $$f"; pass=$$((pass + 1)); \
			fi; \
		else \
			echo "IR-FAIL: $$f"; fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; echo "$$pass passed, $$fail failed"; \
	test $$fail -eq 0

# Transaction-local storage is authored data, not a guest-specific runtime
# convention.  The persistent mutation must compile but change execution.
RELATIONAL_STATE_TRANSACTION_V1_SOURCES = \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_RELATIONAL_STATE_CORE_V1) \
	$(RELATIONAL_STATE_TRANSACTION_V1_SOURCE) \
	$(METAMATH_RELATIONAL_STATE_COMPILER_V1)

$(RELATIONAL_STATE_TRANSACTION_V1_TABLES): \
		$(RELATIONAL_STATE_TRANSACTION_V1_SOURCES) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(RELATIONAL_STATE_TRANSACTION_V1_SOURCES),--source $(source)) \
		--query '(state-table-v1 ?table ?arity ?key-arity ?lifetime)' \
		--out $@

$(RELATIONAL_STATE_TRANSACTION_V1_ACTIONS): \
		$(RELATIONAL_STATE_TRANSACTION_V1_SOURCES) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(RELATIONAL_STATE_TRANSACTION_V1_SOURCES),--source $(source)) \
		--query '(state-action-v1 ?operation ?index ?action)' --out $@

$(RELATIONAL_STATE_TRANSACTION_V1_STATE): \
		$(RELATIONAL_STATE_TRANSACTION_V1_TABLES) \
		$(RELATIONAL_STATE_TRANSACTION_V1_ACTIONS) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		--source $(RELATIONAL_STATE_TRANSACTION_V1_TABLES) \
		--source $(RELATIONAL_STATE_TRANSACTION_V1_ACTIONS) --out $@

$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_SOURCE): \
		$(RELATIONAL_STATE_TRANSACTION_V1_SOURCE)
	@mkdir -p $(dir $@)
	sed 's/state-transactional-v1/state-persistent-v1/g' $< >$@

$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_TABLES): \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_SOURCE) \
		$(METAMATH_RELATIONAL_STATE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
		--source $(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_SOURCE) \
		--source $(METAMATH_RELATIONAL_STATE_COMPILER_V1) \
		--query '(state-table-v1 ?table ?arity ?key-arity ?lifetime)' \
		--out $@

$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_ACTIONS): \
		$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		$(METAMATH_RELATIONAL_STATE_CORE_V1) \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_SOURCE) \
		$(METAMATH_RELATIONAL_STATE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
		--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
		--source $(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_SOURCE) \
		--source $(METAMATH_RELATIONAL_STATE_COMPILER_V1) \
		--query '(state-action-v1 ?operation ?index ?action)' --out $@

$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE): \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_TABLES) \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_ACTIONS) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		--source $(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_TABLES) \
		--source $(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_ACTIONS) \
		--out $@

.PHONY: test-cogslt-relational-state-transaction-v1
test-cogslt-relational-state-transaction-v1: \
		$(RELATIONAL_STATE_TRANSACTION_V1_TEST_BIN) \
		$(RELATIONAL_STATE_TRANSACTION_V1_STATE) \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE)
	@test "$$(wc -l <$(RELATIONAL_STATE_TRANSACTION_V1_STATE))" -eq 6
	@test "$$(wc -l <$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE))" -eq 6
	@rg -F -q '(state-table-v1 guest-scratch-v1 1 1 state-transactional-v1)' \
		$(RELATIONAL_STATE_TRANSACTION_V1_STATE)
	@rg -F -q '(state-table-v1 guest-scratch-v1 1 1 state-persistent-v1)' \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE)
	@if cmp -s $(RELATIONAL_STATE_TRANSACTION_V1_STATE) \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE); then exit 1; fi
	@$(RELATIONAL_STATE_TRANSACTION_V1_TEST_BIN) \
		$(RELATIONAL_STATE_TRANSACTION_V1_STATE) \
		$(RELATIONAL_STATE_TRANSACTION_V1_PERSISTENT_STATE)
	@if rg -ni 'transaction-canary|guest-scratch|guest-output' \
		$(RELATIONAL_STATE_PROGRAM_V1_SRC) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER); then \
		echo 'transaction canary knowledge leaked into the generic runtime'; \
		exit 1; \
	fi
	@if ldd $(RELATIONAL_STATE_TRANSACTION_V1_TEST_BIN) | \
		rg -qi 'python|libswipl'; then \
		echo 'transaction lifetime test retained a foreign dependency'; \
		exit 1; \
	fi

# The native frame decoder accepts only layouts justified by both the
# generated semantic native types and the generated storage plan.
FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES = \
	$(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
	$(METAMATH_RELATIONAL_STATE_CORE_V1) \
	$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
	$(METAMATH_RELATIONAL_STATE_COMPILER_V1)

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_TABLES): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES),--source $(source)) \
		--query '(state-table-v1 ?table ?arity ?key-arity ?lifetime)' \
		--out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_MACHINE): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES),--source $(source)) \
		--query '(state-proof-machine-v1 ?machine ?configuration)' \
		--out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_ACTION): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(foreach source,$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE_SOURCES),--source $(source)) \
		--query '(state-action-v1 ?operation ?index ?action)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_TABLES) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_MACHINE) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_TABLES) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_MACHINE) --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PROOF): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		--query '(proof-artifact-v1 ?fact)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_EVIDENCE): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		--query '(proof-sequence-evidence-artifact-v1 ?fact)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_RELATIONAL): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		--query '(proof-sequence-relational-artifact-v1 ?fact)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_NTT): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		$(FINITE_HORN_REFLECTION_V1) $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_SOURCE) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		--source $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--query '(compile-oslf-native-type-v1 ?fact)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_COMBINED): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_ACTION) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PROOF) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_EVIDENCE) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_RELATIONAL) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_NTT) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) merge-answers \
		$(foreach source,$(filter %.answers,$^),--source $(source)) --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_COMBINED) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answer-facts --source $< --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD) \
		--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
		--query '(proof-storage-required-v1 ?key)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD) \
		--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
		--query '(proof-storage-denoted-v1 ?key)' --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED_KEYS): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head proof-storage-required-v1 --arg 0 --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED_KEYS): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED) \
		$(LANGDEF_COMPILER_V1_BIN)
	$(LANGDEF_COMPILER_V1_BIN) project-answers --source $< \
		--head proof-storage-denoted-v1 --arg 0 --out $@

$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STORAGE): \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED_KEYS) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED_KEYS) \
		$(PROOF_STORAGE_PLAN_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	cmp $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_REQUIRED_KEYS) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_DENOTED_KEYS)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_PAYLOAD) \
		--source $(PROOF_STORAGE_PLAN_COMPILER_V1) \
		--query '(compile-proof-storage-plan-v1 ?fact)' --out $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_SOURCE) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		--source $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--reflect-source $(OSLF_NATIVE_PROGRAM_CANARY_V1_SOURCE) \
		--query '(compile-oslf-native-type-v1 ?fact)' --out $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_ARITY): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT)
	sed \
		's/(oslf-head-signature-v1 canary-path-v1 (q-succ (q-succ q-zero)))/(oslf-head-signature-v1 canary-path-v1 (q-succ q-zero))/' \
		$< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_CONSTRUCTOR): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT)
	sed \
		'/canary-path-from-edge-v1/ s/(q-sym canary-edge-v1)/(q-sym canary-absent-v1)/' \
		$< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_BODY): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT)
	sed \
		'/canary-path-transitive-v1/ s/q-nil))))$$/canary-broken-tail-v1))))/' \
		$< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_DELETED_STEP): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT)
	sed '/canary-path-transitive-v1/d' $< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT): \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_SOURCE) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		--source $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--reflect-source $(OSLF_NATIVE_OPEN_PROGRAM_V1_SOURCE) \
		--query '(compile-oslf-native-type-v1 ?fact)' --out $@

$(OSLF_NATIVE_OPEN_CAPABILITY_V1_REFLECTION): \
		$(OSLF_NATIVE_OPEN_CAPABILITY_V1_SOURCE) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		--reflect-source $(OSLF_NATIVE_OPEN_CAPABILITY_V1_SOURCE) \
		--query '(source-rule (q-sym OSLFNativeOpenCapabilityV1) ?rule)' \
		--out $@

$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_NTT): \
		$(PROOF_GSLT_TRACE_EXECUTION_COMPOSITION_V1) \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(OSLF_NATIVE_TYPE_COMPILER_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) oslf-ntt \
		--composition $(PROOF_GSLT_TRACE_EXECUTION_COMPOSITION_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--reflection $(FINITE_HORN_REFLECTION_V1) \
		--compiler $(OSLF_NATIVE_TYPE_COMPILER_V1) \
		--timeout 120 --out $@

$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_V1): \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		$(foreach source,$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES),--reflect-source $(source)) \
		--reflect-source $(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		--query '(source-rule (q-sym ProofGSLTTraceInputCanaryV1) ?rule)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_DELETED_V1): \
		$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_V1)
	sed '/proof-trace-input-canary-assertion-v1/d' $< >$@
	@! cmp -s $< $@

$(METAMATH_PROOF_MACHINE_CAPABILITY_V1): \
		$(METAMATH_PROOF_MACHINE_NTT_V1_SOURCES) \
		$(METAMATH_PROOF_MACHINE_CAPABILITY_CANARY_V1) \
		$(FINITE_HORN_REFLECTION_V1) \
		$(LANGDEF_COMPILER_V1_BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) answers \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--source $(FINITE_HORN_REFLECTION_V1) \
		$(foreach source,$(METAMATH_PROOF_MACHINE_NTT_V1_SOURCES),--reflect-source $(source)) \
		--reflect-source $(METAMATH_PROOF_MACHINE_CAPABILITY_CANARY_V1) \
		--query '(source-rule (q-sym MetamathProofMachineCapabilityCanaryV1) ?rule)' \
		--timeout 120 --out $@

$(METAMATH_PROOF_MACHINE_CAPABILITY_DELETED_V1): \
		$(METAMATH_PROOF_MACHINE_CAPABILITY_V1)
	sed '/mm-proof-capability-formula-identity-v1/d' $< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_OPEN_PROGRAM_V1_MISSING_INTERFACE): \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT)
	sed '/oslf-external-relation-v1/d' $< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_WRONG_TRANSITION): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT)
	sed 's/finite-horn-backward-goal-reduction-v1/unsupported-transition-v1/' \
		$< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_PROGRAM_CANARY_V1_MISSING_CARRIER): \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT)
	sed '/oslf-carrier-v1/d' $< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_RELATION): \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT)
	sed '/oslf-external-relation-v1/ s/(q-sym canary-open-edge-v1)/(q-sym canary-open-absent-v1)/' \
		$< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_ARITY): \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT)
	sed '/oslf-external-relation-v1/ s/(q-int 2)/(q-int 1)/' $< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_OPEN_PROGRAM_V1_DUPLICATE_INTERFACE): \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT)
	sed '/oslf-external-relation-v1/p' $< >$@
	@! cmp -s $< $@

$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_OBJ): \
		$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_SRC) \
		$(OSLF_NATIVE_TYPE_PLAN_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_SRC)

$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_BIN): \
		$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_OBJ) \
		$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ \
		$(OSLF_NATIVE_TYPE_VM_V1_LDFLAGS)

.PHONY: test-cogslt-oslf-native-type-program-v1
test-cogslt-oslf-native-type-program-v1: \
		$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_BIN) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_ARITY) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_CONSTRUCTOR) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_BODY) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_DELETED_STEP) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_WRONG_TRANSITION) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_MISSING_CARRIER) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_MISSING_INTERFACE) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_RELATION) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_ARITY) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_DUPLICATE_INTERFACE)
	@$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_BIN) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_ARITY) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_CONSTRUCTOR) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_BAD_BODY) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_DELETED_STEP) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_MISSING_INTERFACE) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_RELATION) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_WRONG_ARITY) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_DUPLICATE_INTERFACE) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_WRONG_TRANSITION) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_MISSING_CARRIER)
	@if rg -ni 'metamath|megalodon|tptp|set\.mm|\$$[acdefpv]' \
		$(OSLF_NATIVE_TYPE_PLAN_V1_SRC) \
		$(OSLF_NATIVE_TYPE_PLAN_V1_HEADER) \
		$(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_SRC); then \
		echo 'guest-language knowledge leaked into the native OSLF program ABI'; \
		exit 1; \
	fi
	@if ldd $(OSLF_NATIVE_TYPE_PROGRAM_V1_TEST_BIN) | \
		rg -qi 'python|libswipl'; then \
		echo 'the native OSLF plan retained a foreign dependency'; \
		exit 1; \
	fi

$(OSLF_NATIVE_TYPE_INSPECT_V1_OBJ): \
		$(OSLF_NATIVE_TYPE_INSPECT_V1_SRC) \
		$(OSLF_NATIVE_TYPE_PLAN_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(OSLF_NATIVE_TYPE_INSPECT_V1_SRC)

$(OSLF_NATIVE_TYPE_INSPECT_V1_BIN): \
		$(OSLF_NATIVE_TYPE_INSPECT_V1_OBJ) \
		$(OSLF_NATIVE_TYPE_INSPECT_V1_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ \
		$(OSLF_NATIVE_TYPE_VM_V1_LDFLAGS)

.PHONY: test-cogslt-oslf-native-type-analysis-v1
test-cogslt-oslf-native-type-analysis-v1: \
		$(OSLF_NATIVE_TYPE_INSPECT_V1_BIN) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT)
	@analysis_output=$$($(OSLF_NATIVE_TYPE_INSPECT_V1_BIN) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT)); \
	printf '%s\n' "$$analysis_output"; \
	printf '%s\n' "$$analysis_output" | awk -F '\t' '\
		$$1 == "program" { \
			expected_external[$$2] = $$6; expected_open[$$2] = $$7; \
			programs++ \
		} \
		$$1 == "external" { actual_external[$$2]++ } \
		$$1 == "open" { actual_open[$$2]++ } \
		$$1 == "flow" { \
			flow_steps[$$2] = $$3; flow_range[$$2] = $$4; \
			flow_linear[$$2] = $$5; flow_both[$$2] = $$6; \
			flow_max[$$2] = $$7; flows++ \
		} \
		END { \
			if (programs != 4 || flows != 4 || \
				flow_steps[0] != 629 || flow_range[0] != 462 || \
				flow_linear[0] != 580 || flow_both[0] != 425 || \
				flow_max[0] != 35 || \
				flow_steps[1] != 969 || flow_range[1] != 826 || \
				flow_steps[2] != 9 || flow_range[2] != 8 || \
				flow_steps[3] != 3 || flow_range[3] != 2 || \
				expected_external[2] != 0 || \
				expected_open[2] != 0 || expected_external[3] != 1 || \
				expected_open[3] != 1) exit 1; \
			for (program in expected_external) \
				if (actual_external[program] + 0 != \
					expected_external[program] + 0 || \
					actual_open[program] + 0 != \
					expected_open[program] + 0) exit 1; \
		}'
	@if rg -ni 'metamath|megalodon|tptp|set\.mm|\$$[acdefpv]|canary' \
		$(OSLF_NATIVE_TYPE_INSPECT_V1_SRC); then \
		echo 'guest-language knowledge leaked into the native OSLF inspector'; \
		exit 1; \
	fi
	@if ldd $(OSLF_NATIVE_TYPE_INSPECT_V1_BIN) | \
		rg -qi 'python|libswipl'; then \
		echo 'the native OSLF inspector retained a foreign dependency'; \
		exit 1; \
	fi

$(OSLF_NATIVE_TYPE_VM_V1_TEST_OBJ): \
		$(OSLF_NATIVE_TYPE_VM_V1_TEST_SRC) \
		$(OSLF_NATIVE_TYPE_VM_V1_HEADER) \
		$(OSLF_NATIVE_TYPE_PLAN_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(OSLF_NATIVE_TYPE_VM_V1_TEST_SRC)

$(OSLF_NATIVE_TYPE_VM_V1_MATCH_OBJ): src/match.c $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		$(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(OSLF_NATIVE_TYPE_VM_V1_VARIANT_OBJ): \
		src/variant_shape.c $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		$(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(OSLF_NATIVE_TYPE_VM_V1_PRIME_NEED_OBJ): \
		src/prime_need.c $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		$(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

$(OSLF_NATIVE_TYPE_VM_V1_TEST_BIN): \
		$(OSLF_NATIVE_TYPE_VM_V1_TEST_OBJ) \
		$(OSLF_NATIVE_TYPE_VM_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ \
		$(OSLF_NATIVE_TYPE_VM_V1_LDFLAGS)

$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN): \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_SRC) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		$(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_H) \
		$(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_C) \
		$(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_H) \
		$(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_C) \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_LINK_OBJ) \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) $(CFLAGS) \
		-Wl,--gc-sections \
		-o $@ $(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_SRC) \
		$(METAMATH_CURSOR_FOLD_GENERATED_C_V1) \
		$(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_C) \
		$(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_C) \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_LINK_OBJ) \
		$(OSLF_NATIVE_TYPE_VM_V1_LDFLAGS)

.PHONY: test-cogslt-proof-gslt-relational-runtime-v1
test-cogslt-proof-gslt-relational-runtime-v1: \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(GSLT_RULE_MUTATOR_V1_BIN) $(LANGDEF_COMPILER_V1_BIN) \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		tests/langdef/metamath/positive_theorem_normal_unknown.mm \
		tests/langdef/metamath/positive_theorem_compressed_unknown.mm \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
		tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
		tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm \
		tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_machine_v1 \
		--header-include langdef/metamath/generated/proof_machine_language_v1.generated.h
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATION_TEST_V1) \
		--generator $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1) \
		--language-manifest $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_machine_provider_catalog_v1 \
		--header-include langdef/metamath/generated/proof_machine_provider_catalog_v1.generated.h
	@$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		tests/support/metamath_mini_thm_v0.mm \
		tests/langdef/metamath/invalid_theorem_wrong_claim.mm \
		tests/support/metamath_mini_thm_compressed_v0.mm \
		tests/langdef/metamath/invalid_theorem_compressed_out_of_range.mm \
		tests/langdef/metamath/positive_theorem_normal_unknown.mm \
		tests/langdef/metamath/positive_theorem_compressed_unknown.mm \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
		tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
		tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm \
		tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm
	@test "$$(rg -c 'proof-relational-runtime-compressed-input-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1))" -eq 1
	@test "$$(rg -c 'proof-relational-runtime-normal-input-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1))" -eq 1
	@test "$$(rg -c 'proof-relational-runtime-outcome-query-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1))" -eq 4
	@test "$$(rg -c 'proof-relational-runtime-input-byte-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1))" -eq 2
	@test "$$(rg -c 'proof-relational-runtime-distinct-pairs-v1' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1))" -eq 1
	@set -eu; \
	work=$$(mktemp -d runtime/proof-relational-runtime-mutation.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	sed '/proof-relational-runtime-distinct-pairs-v1/d' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		>"$$work/deleted.answers"; \
	sed '/proof-relational-runtime-distinct-pairs-v1/s/mm-symbol-variable/mm-symbol-constant/' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		>"$$work/falsified.answers"; \
	for mutation in deleted falsified; do \
		if $(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN) \
			"$$work/$$mutation.answers" \
			$(METAMATH_PROOF_MACHINE_NTT_V1) \
			tests/support/metamath_mini_thm_v0.mm \
			tests/langdef/metamath/invalid_theorem_wrong_claim.mm \
			tests/support/metamath_mini_thm_compressed_v0.mm \
			tests/langdef/metamath/invalid_theorem_compressed_out_of_range.mm \
			tests/langdef/metamath/positive_theorem_normal_unknown.mm \
			tests/langdef/metamath/positive_theorem_compressed_unknown.mm \
			tests/langdef/metamath/positive_theorem_normal_reuse.mm \
			tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
			tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
			tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm \
			tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm \
			>"$$work/$$mutation.out" 2>"$$work/$$mutation.err"; then \
			exit 1; \
		fi; \
		rg -q '^FAIL:' "$$work/$$mutation.err"; \
	done
	@set -eu; \
	work=$$(mktemp -d runtime/proof-relational-incomplete-mutation.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	sed -e '/proof-runtime-result-incomplete-v1/d' \
		-e '/proof-relational-runtime-input-byte-v1/d' \
		$(PROOF_GSLT_METAMATH_RELATIONAL_RUNTIME_ABI_V1) \
		>"$$work/deleted.answers"; \
	if $(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN) \
		"$$work/deleted.answers" \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		tests/support/metamath_mini_thm_v0.mm \
		tests/langdef/metamath/invalid_theorem_wrong_claim.mm \
		tests/support/metamath_mini_thm_compressed_v0.mm \
		tests/langdef/metamath/invalid_theorem_compressed_out_of_range.mm \
		tests/langdef/metamath/positive_theorem_normal_unknown.mm \
		tests/langdef/metamath/positive_theorem_compressed_unknown.mm \
		tests/langdef/metamath/positive_theorem_normal_reuse.mm \
		tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
		tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
		tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm \
		tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm \
		>"$$work/deleted.out" 2>"$$work/deleted.err"; then \
		exit 1; \
	fi; \
	rg -q '^FAIL:' "$$work/deleted.err"
	@set -eu; \
	work=$$(mktemp -d runtime/proof-relational-authored-incomplete.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	compile_provider() { \
		$(LANGDEF_COMPILER_V1_BIN) answers \
			--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
			--source $(METAMATH_PROOF_SEMANTIC_EXEC_V1) \
			--source $(PROOF_GSLT_ARTICLE_CORE_V1) \
			--source $(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
			--source $(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
			--source $(METAMATH_OCCURRENCE_FOLD_CORE_V1) \
			--source $(METAMATH_SOURCE_FOLD_V1) \
			--source $(METAMATH_RELATIONAL_STATE_CORE_V1) \
			--source $(METAMATH_SOURCE_STATE_V1) \
			--source $(METAMATH_SOURCE_PROOF_V1) \
			--source $(PROOF_GSLT_TRACE_SEMANTICS_V1) \
			--source $(PROOF_GSLT_TRACE_COMPILER_V1) \
			--source $(PROOF_GSLT_TRACE_INPUT_V1) \
			--source $(PROOF_GSLT_RELATIONAL_PROJECTION_CORE_V1) \
			--source $(PROOF_GSLT_RELATIONAL_RUNTIME_COMPILER_V1) \
			--source $(PROOF_GSLT_METAMATH_CALCULUS_V1) \
			--source $(PROOF_GSLT_METAMATH_RELATIONAL_PROJECTION_V1) \
			--source "$$1" \
			--query '(proof-relational-runtime-artifact-v1 ?record)' \
			--out "$$2" >/dev/null; \
	}; \
	for kind in normal compressed; do \
		$(GSLT_RULE_MUTATOR_V1_BIN) mutate \
			--source $(METAMATH_PROOF_TRACE_CODEC_V1) \
			--out "$$work/$$kind-codec.metta" \
			--rule "mm-proof-trace-accept-incomplete-$$kind-v1" \
			--mode delete; \
		compile_provider "$$work/$$kind-codec.metta" \
			"$$work/$$kind-provider.answers"; \
		test "$$(rg -c 'proof-relational-runtime-outcome-query-v1' \
			"$$work/$$kind-provider.answers")" -eq 3; \
		test "$$(rg -c 'proof-relational-runtime-input-byte-v1' \
			"$$work/$$kind-provider.answers")" -eq 1; \
		if $(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN) \
			"$$work/$$kind-provider.answers" \
			$(METAMATH_PROOF_MACHINE_NTT_V1) \
			tests/support/metamath_mini_thm_v0.mm \
			tests/langdef/metamath/invalid_theorem_wrong_claim.mm \
			tests/support/metamath_mini_thm_compressed_v0.mm \
			tests/langdef/metamath/invalid_theorem_compressed_out_of_range.mm \
			tests/langdef/metamath/positive_theorem_normal_unknown.mm \
			tests/langdef/metamath/positive_theorem_compressed_unknown.mm \
			tests/langdef/metamath/positive_theorem_normal_reuse.mm \
			tests/langdef/metamath/invalid_theorem_compressed_save_after_continuation.mm \
			tests/langdef/metamath/invalid_theorem_compressed_incomplete_tail.mm \
			tests/langdef/metamath/invalid_theorem_normal_incomplete_tail.mm \
			tests/langdef/metamath/positive_theorem_compressed_incomplete_after_continuation.mm \
			>"$$work/$$kind.out" 2>"$$work/$$kind.err"; then \
			exit 1; \
		fi; \
		rg -q '^FAIL:' "$$work/$$kind.err"; \
	done
	@if rg -ni 'metamath|megalodon|tptp|set[.]mm|[$$][acdefpv]|mm-state|mm-label|canary' \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the relational proof runtime'; \
		exit 1; \
	fi
	@if ldd $(PROOF_GSLT_RELATIONAL_RUNTIME_NATIVE_V1_TEST_BIN) | \
		rg -qi 'python|libswipl'; then \
		echo 'the relational proof runtime retained a foreign dependency'; \
		exit 1; \
	fi

.PHONY: test-cogslt-oslf-native-type-vm-v1
test-cogslt-oslf-native-type-vm-v1: \
		$(OSLF_NATIVE_TYPE_VM_V1_TEST_BIN) \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_DELETED_STEP) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT) \
		$(OSLF_NATIVE_OPEN_CAPABILITY_V1_REFLECTION) \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_NTT) \
		$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_V1) \
		$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_DELETED_V1) \
		$(METAMATH_PROOF_MACHINE_CAPABILITY_V1) \
		$(METAMATH_PROOF_MACHINE_CAPABILITY_DELETED_V1)
	@$(OSLF_NATIVE_TYPE_VM_V1_TEST_BIN) \
		$(METAMATH_PROOF_MACHINE_NTT_V1) \
		$(METAMATH_AUTHORING_NTT_V1) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_NTT) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_DELETED_STEP) \
		$(OSLF_NATIVE_OPEN_PROGRAM_V1_NTT) \
		$(OSLF_NATIVE_OPEN_CAPABILITY_V1_REFLECTION) \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_NTT) \
		$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_V1) \
		$(METAMATH_PROOF_TRACE_NATIVE_CAPABILITY_DELETED_V1) \
		$(METAMATH_PROOF_MACHINE_CAPABILITY_V1) \
		$(METAMATH_PROOF_MACHINE_CAPABILITY_DELETED_V1)
	@chart_output=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_SOURCE) \
		--query-text '(canary-path-v1 "root-λ" "leaf")'); \
	printf '%s\n' "$$chart_output" | rg -q '"outcome":"Unique"'
	@chart_output=$$($(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(OSLF_NATIVE_PROGRAM_CANARY_V1_SOURCE) \
		--query-text '(canary-path-v1 "leaf" "root-λ")'); \
	printf '%s\n' "$$chart_output" | rg -q '"outcome":"NoAnswer"'
	@if rg -ni 'metamath|megalodon|tptp|set\.mm|\$$[acdefpv]|canary' \
		$(OSLF_NATIVE_TYPE_VM_V1_SRC) \
		$(OSLF_NATIVE_TYPE_VM_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the native OSLF VM'; \
		exit 1; \
	fi
	@if ldd $(OSLF_NATIVE_TYPE_VM_V1_TEST_BIN) | \
		rg -qi 'python|libswipl'; then \
		echo 'the native OSLF VM retained a foreign compiler dependency'; \
		exit 1; \
	fi

$(FIRST_ORDER_FRAME_DECODER_V1_TEST_OBJ): \
		$(FIRST_ORDER_FRAME_DECODER_V1_TEST_SRC) \
		$(FIRST_ORDER_FRAME_DECODER_V1_HEADER) \
		$(OSLF_NATIVE_TYPE_PLAN_V1_HEADER) \
		$(PROOF_STORAGE_PLAN_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(FIRST_ORDER_FRAME_DECODER_V1_TEST_SRC)

$(FIRST_ORDER_FRAME_DECODER_V1_TEST_BIN): \
		$(FIRST_ORDER_FRAME_DECODER_V1_TEST_OBJ) \
		$(FIRST_ORDER_FRAME_DECODER_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-cogslt-first-order-frame-decoder-v1
test-cogslt-first-order-frame-decoder-v1: \
		$(FIRST_ORDER_FRAME_DECODER_V1_TEST_BIN) \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		$(METAMATH_PROOF_STORAGE_PLAN_V1) \
		$(OSLF_NATIVE_TYPE_UNKNOWN_V1) \
		$(OSLF_NATIVE_TYPE_MALFORMED_ARITY_V1) \
		$(OSLF_NATIVE_TYPE_DUPLICATE_HEAD_V1) \
		$(PROOF_STORAGE_UNKNOWN_V1) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_NTT) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STORAGE)
	@$(FIRST_ORDER_FRAME_DECODER_V1_TEST_BIN) \
		$(METAMATH_PROOF_SEMANTIC_NTT_V1) \
		$(METAMATH_PROOF_STORAGE_PLAN_V1) \
		$(OSLF_NATIVE_TYPE_UNKNOWN_V1) \
		$(OSLF_NATIVE_TYPE_MALFORMED_ARITY_V1) \
		$(OSLF_NATIVE_TYPE_DUPLICATE_HEAD_V1) \
		$(PROOF_STORAGE_UNKNOWN_V1) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STATE) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_NTT) \
		$(FIRST_ORDER_FRAME_GENERATED_GUEST_V1_STORAGE)
	@if rg -ni 'metamath|megalodon|tptp|set\.mm' \
		$(OSLF_NATIVE_TYPE_PLAN_V1_SRC) \
		$(OSLF_NATIVE_TYPE_PLAN_V1_HEADER) \
		$(PROOF_STORAGE_PLAN_V1_SRC) \
		$(PROOF_STORAGE_PLAN_V1_HEADER) \
		$(FIRST_ORDER_FRAME_DECODER_V1_SRC) \
		$(FIRST_ORDER_FRAME_DECODER_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the native frame decoder'; \
		exit 1; \
	fi
	@if rg -ni 'label-kind-v1|formula-v1|floating-variable-v1|mandatory-variable-v1|ordered-hypothesis-v1|assertion-disjoint-v1|active-hypothesis-v1|active-disjoint-v1|symbol-kind-v1' \
		$(FIRST_ORDER_FRAME_DECODER_V1_SRC) \
		$(FIRST_ORDER_FRAME_DECODER_V1_HEADER); then \
		echo 'authored frame-role names leaked into the native decoder'; \
		exit 1; \
	fi

# An admitted persistent frame is compiled once into dense binder slots and
# two-phase literal/hole instructions.  The uncached interpreter remains the
# independent executable specification for every positive and negative case.
$(GSLT_DENSE_BITSET_V1_TEST_OBJ): \
		$(GSLT_DENSE_BITSET_V1_TEST_SRC) \
		$(GSLT_DENSE_BITSET_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_DENSE_BITSET_V1_TEST_SRC)

$(GSLT_DENSE_BITSET_V1_TEST_BIN): \
		$(GSLT_DENSE_BITSET_V1_TEST_OBJ) \
		$(GSLT_DENSE_BITSET_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-dense-bitset-v1
test-gslt-dense-bitset-v1: $(GSLT_DENSE_BITSET_V1_TEST_BIN)
	@$(GSLT_DENSE_BITSET_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|set\.mm|proof|parser' \
		$(GSLT_DENSE_BITSET_V1_SRC) \
		$(GSLT_DENSE_BITSET_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the dense bitset'; \
		exit 1; \
	fi

$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_OBJ): \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_SRC) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_SRC)

$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_BIN): \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_OBJ) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-indexed-instruction-decoder-v1
test-gslt-indexed-instruction-decoder-v1: \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_BIN)
	@$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|compressed[_ -]?proof|mandatory[_ -]?hypothesis' \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_SRC) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the indexed instruction decoder'; \
		exit 1; \
	fi

$(GSLT_INDEXED_VALUE_TABLE_V1_TEST_OBJ): \
		$(GSLT_INDEXED_VALUE_TABLE_V1_TEST_SRC) \
		$(GSLT_INDEXED_VALUE_TABLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_INDEXED_VALUE_TABLE_V1_TEST_SRC)

$(GSLT_INDEXED_VALUE_TABLE_V1_TEST_BIN): \
		$(GSLT_INDEXED_VALUE_TABLE_V1_TEST_OBJ) \
		$(GSLT_INDEXED_VALUE_TABLE_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-indexed-value-table-v1
test-gslt-indexed-value-table-v1: $(GSLT_INDEXED_VALUE_TABLE_V1_TEST_BIN)
	@$(GSLT_INDEXED_VALUE_TABLE_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|proof|parser|label|saved' \
		$(GSLT_INDEXED_VALUE_TABLE_V1_SRC) \
		$(GSLT_INDEXED_VALUE_TABLE_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the indexed value table'; \
		exit 1; \
	fi

$(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_OBJ): \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_SRC) \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_SRC)

$(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_BIN): \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_OBJ) \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-split-indexed-table-v1
test-gslt-split-indexed-table-v1: $(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_BIN)
	@$(GSLT_SPLIT_INDEXED_TABLE_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|set\.mm' \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_SRC) \
		$(GSLT_SPLIT_INDEXED_TABLE_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the split indexed table'; \
		exit 1; \
	fi

$(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_OBJ): \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_SRC) \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_SRC)

$(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_BIN): \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_OBJ) \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-literal-hole-program-v1
test-gslt-literal-hole-program-v1: \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_BIN)
	@$(GSLT_LITERAL_HOLE_PROGRAM_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|proof|parser|label|saved' \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_SRC) \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the literal/hole program'; \
		exit 1; \
	fi

$(GSLT_U32_INDEX_V1_TEST_OBJ): \
		$(GSLT_U32_INDEX_V1_TEST_SRC) \
		$(GSLT_U32_INDEX_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_U32_INDEX_V1_TEST_SRC)

$(GSLT_U32_INDEX_V1_TEST_BIN): \
		$(GSLT_U32_INDEX_V1_TEST_OBJ) \
		$(GSLT_U32_INDEX_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-u32-index-v1
test-gslt-u32-index-v1: $(GSLT_U32_INDEX_V1_TEST_BIN)
	@$(GSLT_U32_INDEX_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|proof|parser|label|rule' \
		$(GSLT_U32_INDEX_V1_SRC) $(GSLT_U32_INDEX_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the finite index'; \
		exit 1; \
	fi

$(GSLT_U32_SLICE_ARENA_V1_TEST_OBJ): \
		$(GSLT_U32_SLICE_ARENA_V1_TEST_SRC) \
		$(GSLT_U32_SLICE_ARENA_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_U32_SLICE_ARENA_V1_TEST_SRC)

$(GSLT_U32_SLICE_ARENA_V1_TEST_BIN): \
		$(GSLT_U32_SLICE_ARENA_V1_TEST_OBJ) \
		$(GSLT_U32_SLICE_ARENA_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-u32-slice-arena-v1
test-gslt-u32-slice-arena-v1: $(GSLT_U32_SLICE_ARENA_V1_TEST_BIN)
	@$(GSLT_U32_SLICE_ARENA_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|formula|proof|parser|token|span' \
		$(GSLT_U32_SLICE_ARENA_V1_SRC) \
		$(GSLT_U32_SLICE_ARENA_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the slice arena'; \
		exit 1; \
	fi

$(GSLT_EPOCH_SLOTS_V1_TEST_OBJ): \
		$(GSLT_EPOCH_SLOTS_V1_TEST_SRC) \
		$(GSLT_EPOCH_SLOTS_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_EPOCH_SLOTS_V1_TEST_SRC)

$(GSLT_EPOCH_SLOTS_V1_TEST_BIN): \
		$(GSLT_EPOCH_SLOTS_V1_TEST_OBJ) $(GSLT_EPOCH_SLOTS_V1_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-epoch-slots-v1
test-gslt-epoch-slots-v1: $(GSLT_EPOCH_SLOTS_V1_TEST_BIN)
	@$(GSLT_EPOCH_SLOTS_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|binder|proof|parser|register' \
		$(GSLT_EPOCH_SLOTS_V1_SRC) $(GSLT_EPOCH_SLOTS_V1_HEADER); then \
		echo 'guest-language knowledge leaked into epoch slots'; \
		exit 1; \
	fi

$(GSLT_GROUND_DENSE_TERM_V1_TEST_OBJ): \
		$(GSLT_GROUND_DENSE_TERM_V1_TEST_SRC) \
		$(GSLT_GROUND_DENSE_TERM_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(GSLT_GROUND_DENSE_TERM_V1_TEST_SRC)

$(GSLT_GROUND_DENSE_TERM_V1_TEST_BIN): \
		$(GSLT_GROUND_DENSE_TERM_V1_TEST_OBJ) \
		$(GSLT_GROUND_DENSE_TERM_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-gslt-ground-dense-term-v1
test-gslt-ground-dense-term-v1: \
		$(GSLT_GROUND_DENSE_TERM_V1_TEST_BIN)
	@$(GSLT_GROUND_DENSE_TERM_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|compressed[_ -]?proof|mandatory[_ -]?hypothesis' \
		$(GSLT_GROUND_DENSE_TERM_V1_SRC) \
		$(GSLT_GROUND_DENSE_TERM_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the dense term machine'; \
		exit 1; \
	fi

$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_OBJ): \
		$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_SRC) \
		$(RELATIONAL_STACK_PROOF_V1_HEADER) \
		$(RELATIONAL_STORE_V1_HEADER) \
		$(RELATIONAL_VALUE_LIST_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-c -o $@ $(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_SRC)

$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_BIN): \
		$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_OBJ) \
		$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-cogslt-relational-stack-proof-cache-v1
test-cogslt-relational-stack-proof-cache-v1: \
		test-gslt-dense-bitset-v1 \
		test-gslt-indexed-instruction-decoder-v1 \
		test-gslt-indexed-value-table-v1 \
		test-gslt-split-indexed-table-v1 \
		test-gslt-literal-hole-program-v1 \
		test-gslt-u32-index-v1 \
		test-gslt-u32-slice-arena-v1 \
		test-gslt-epoch-slots-v1 \
		test-gslt-ground-dense-term-v1 \
		$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_BIN)
	@$(RELATIONAL_STACK_PROOF_CACHE_V1_TEST_BIN)
	@if rg -ni 'metamath|megalodon|tptp|set\.mm' \
		$(RELATIONAL_STACK_PROOF_V1_SRC) \
		$(RELATIONAL_STACK_PROOF_V1_HEADER) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_SRC) \
		$(GSLT_INDEXED_INSTRUCTION_DECODER_V1_HEADER) \
		$(GSLT_INDEXED_VALUE_TABLE_V1_SRC) \
		$(GSLT_INDEXED_VALUE_TABLE_V1_HEADER) \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_SRC) \
		$(GSLT_LITERAL_HOLE_PROGRAM_V1_HEADER) \
		$(GSLT_U32_INDEX_V1_SRC) \
		$(GSLT_U32_INDEX_V1_HEADER) \
		$(GSLT_U32_SLICE_ARENA_V1_SRC) \
		$(GSLT_U32_SLICE_ARENA_V1_HEADER) \
		$(GSLT_EPOCH_SLOTS_V1_SRC) \
		$(GSLT_EPOCH_SLOTS_V1_HEADER) \
		$(GSLT_DENSE_BITSET_V1_SRC) \
		$(GSLT_DENSE_BITSET_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the frame cache'; \
		exit 1; \
	fi

# The article checker executes only compiled structural proof presentations.
$(PROOF_GSLT_ARTICLE_V1_TEST_BIN): \
		$(BUILD_CONFIG_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_SRC) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_TEST_SRC)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(PROOF_GSLT_ARTICLE_V1_SRC) \
		$(PROOF_GSLT_ARTICLE_V1_TEST_SRC)

.PHONY: test-cogslt-proof-article-v1
test-cogslt-proof-article-v1: $(PROOF_GSLT_ARTICLE_V1_TEST_BIN)
	@$(PROOF_GSLT_ARTICLE_V1_TEST_BIN)
	@if rg -ni '\b(he|petta|metta|rho|mrho|rhocalc|hyperon)\b|metamath|megalodon|tptp|cetta[-_ ]?prime|set\.mm' \
		$(PROOF_GSLT_ARTICLE_V1_SRC) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the generic proof checker'; \
		exit 1; \
	fi

$(PROOF_GSLT_PLAN_V1_TEST_OBJ): \
		$(PROOF_GSLT_PLAN_V1_TEST_SRC) \
		$(PROOF_GSLT_PLAN_V1_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(PROOF_GSLT_PLAN_V1_TEST_SRC)

$(PROOF_GSLT_PLAN_V1_TEST_BIN): \
		$(PROOF_GSLT_PLAN_V1_TEST_OBJ) \
		$(PROOF_GSLT_PLAN_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_OBJ): \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_SRC) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_HEADER) \
		$(PROOF_GSLT_PLAN_V1_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_SRC)

$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_BIN): \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_OBJ) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_OBJ): \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_SRC) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER) \
		$(PROOF_GSLT_PLAN_V1_HEADER) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_SRC)

$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_BIN): \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_OBJ) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_OBJ): \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_SRC) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_HEADER) \
		$(RELATIONAL_STATE_PROGRAM_V1_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_SRC)

$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_BIN): \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_OBJ) \
		$(PROOF_GSLT_RELATIONAL_PROJECTION_NATIVE_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_OBJ): \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_SRC) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_MACHINE_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_HEADER) \
		$(PROOF_STORAGE_PLAN_V1_HEADER) \
		$(RELATIONAL_VALUE_LIST_V1_HEADER) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -c -o $@ \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_SRC)

$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_BIN): \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_OBJ) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_LINK_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^ $(LDFLAGS)

.PHONY: test-cogslt-proof-stage-v1
test-cogslt-proof-stage-v1: \
		test-cogslt-proof-article-v1 \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		$(PROOF_GSLT_PLAN_V1_TEST_BIN) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_BIN) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_BIN) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_BIN) \
		$(PROOF_GSLT_PROP_ANSWERS_V1) \
		$(PROOF_GSLT_EVEN_ANSWERS_V1) \
		$(PROOF_GSLT_BINDER_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_METAMATH_PLAN_V1) \
		$(PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_V1) \
		$(PROOF_GSLT_PROP_DELETE_MP_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_APART_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_ANSWERS_V1)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		ecc42c94da8963a709f4567b81e51c620d7763dc6482cbd805925e3df8b8e995 \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_PROP_CANARY_V1)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		f37cbb4e1c0edd7de8484f97260823f3877eb82539321669000badc2535a8070 \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_EVEN_CANARY_V1)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		c9e09fcbd8c72cd21e1904abd3986926f2aa715bd25fb15d0c364fa085a4fd1d \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_BINDER_CANARY_V1)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		56ffbd22319503676c1683634290ddcde912438ec404a8cfdbd89982d09dbff6 \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_CANARY_V1)
	@$(PROOF_GSLT_PLAN_V1_TEST_BIN) \
		$(PROOF_GSLT_PROP_ANSWERS_V1) \
		$(PROOF_GSLT_EVEN_ANSWERS_V1) \
		$(PROOF_GSLT_BINDER_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_ANSWERS_V1) \
		$(PROOF_GSLT_PROP_DELETE_MP_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_APART_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_ASSERTION_ANSWERS_V1)
	@$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_TEST_BIN) \
		$(PROOF_GSLT_SEQUENCE_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_PROP_ANSWERS_V1) \
		$(PROOF_GSLT_SEQUENCE_DELETE_ABI_ROLE_V1)
	@$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_TEST_BIN) \
		$(PROOF_GSLT_METAMATH_PLAN_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_DELETE_ROLE_V1) \
		$(PROOF_GSLT_SEQUENCE_ANSWERS_V1) \
		mm-state-symbol-kind \
		mm-state-formula \
		mm-state-floating-variable \
		mm-state-assertion-active-hypothesis \
		mm-state-assertion-ordered-hypothesis \
		mm-state-assertion-mandatory-variable \
		mm-state-assertion-disjoint-variable \
		mm-state-label-kind
	@$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_TEST_BIN) \
		$(PROOF_GSLT_METAMATH_PLAN_V1) \
		$(PROOF_GSLT_METAMATH_EVIDENCE_ABI_V1) \
		$(PROOF_GSLT_METAMATH_RELATIONAL_ABI_V1) \
		mm-state-symbol-kind \
		mm-state-formula \
		mm-state-floating-variable \
		mm-state-assertion-active-hypothesis \
		mm-state-assertion-ordered-hypothesis \
		mm-state-assertion-mandatory-variable \
		mm-state-assertion-disjoint-variable \
		mm-state-label-kind
	@if rg -ni '\b(he|petta|metta|rho|mrho|rhocalc|hyperon)\b|metamath|megalodon|tptp|cetta[-_ ]?prime|set\.mm' \
		$(PROOF_GSLT_ARTICLE_CORE_V1) \
		$(PROOF_GSLT_ARTICLE_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_V1) \
		$(PROOF_GSLT_ARTICLE_DOCUMENT_COMPILER_V1) \
		$(PROOF_GSLT_SEQUENCE_RELATIONS_V1) \
		$(PROOF_GSLT_SEQUENCE_SCHEMA_V1) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_COMPILER_V1) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_CORE_V1) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_COMPILER_V1) \
		$(PROOF_GSLT_ARTICLE_V1_SRC) \
		$(PROOF_GSLT_ARTICLE_V1_HEADER) \
		$(PROOF_GSLT_PLAN_V1_SRC) \
		$(PROOF_GSLT_PLAN_V1_HEADER) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_SRC) \
		$(PROOF_GSLT_SEQUENCE_EVIDENCE_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_ASSERTION_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_DECLARATION_V1_HEADER) \
		$(PROOF_GSLT_RELATIONAL_MACHINE_V1_SRC) \
		$(PROOF_GSLT_RELATIONAL_MACHINE_V1_HEADER); then \
		echo 'guest-language knowledge leaked into the structural proof stack'; \
		exit 1; \
	fi

# The finite Horn presentation is an arity-checked, one-carrier reification of
# the mathematical GSLT core. These gates validate only its schema boundary;
# parser behavior and backend correspondence have separate gates.
$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_finite_horn_gslt_v1.c \
		src/native_sha256.c src/native_sha256.h
	@mkdir -p runtime
	$(CC) $(CFLAGS) -Isrc -I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_finite_horn_gslt_v1.c \
		src/native_sha256.c

.PHONY: test-gslt2parse-schema-v1-native-metamath
test-gslt2parse-schema-v1-native-metamath: \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		8c1b50b54686eed8b6e11ff61160cf17db79d814b933896aca78f76638043b82 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		langdef/metamath/syntax_v1.metta
	@echo '(GSLT2ParseLanguagePackageV1 metamath PASS)'

.PHONY: test-gslt2parse-schema-v1-native-tptp
test-gslt2parse-schema-v1-native-tptp: \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		4bb4743974e886104667e91cac389784b6e62a267815bb0745272db2a353f104 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/char_core_v1.metta \
		langdef/tptp/syntax_fof_cnf_v1.metta
	@echo '(GSLT2ParseLanguagePackageV1 tptp PASS)'

.PHONY: test-gslt2parse-schema-v1-native-megalodon
test-gslt2parse-schema-v1-native-megalodon: \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		2eba88d8bb4681540cb84bf6dd15e183cf83ad2821ae682cb94a754b4f2eb36a \
		--quote-index 3 \
		573cb858dcf1e678fb6656c00077a53c53d977e51b74211c5dc21b2de91829be \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/char_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/languages/megalodon_dynamic_v1.metta
	@echo '(GSLT2ParseLanguagePackageV1 megalodon PASS)'

.PHONY: test-gslt2parse-schema-v1-native-prime
test-gslt2parse-schema-v1-native-prime: \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		9c39249e6f56edfff37766fecd63ecea2a2664eb2a920e27d74d47c3fbc3f1dd \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/cetta_prime_scalar_classes_v1.metta \
		experiments/gslt2parse_foundation/presentations/languages/cetta_prime_reader_v1.metta
	@echo '(GSLT2ParseLanguagePackageV1 prime PASS)'

.PHONY: test-gslt2parse-schema-v1-native-rho-runtime
test-gslt2parse-schema-v1-native-rho-runtime: \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		ee90a8366ce08bd38cc04a92ffb81d633031e1f95aa646db87b8fe3e8c708563 \
		experiments/gslt2parse_foundation/presentations/shared/rho_abstract_syntax_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/rho_runtime_term_abi_v1.metta
	@echo '(GSLT2ParseLanguagePackageV1 rho-runtime PASS)'

.PHONY: test-gslt2parse-schema-v1-native-rho-reader
test-gslt2parse-schema-v1-native-rho-reader: \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		5042f6651530edd7a0d8dd397e6fd21ebca21b1ab8693bbd9c1efd99fd2c4895 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/he_reader_scalar_classes_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/rho_abstract_syntax_v1.metta \
		experiments/gslt2parse_foundation/presentations/languages/cetta_rho_mrho_reader_v1.metta
	@echo '(GSLT2ParseLanguagePackageV1 rho-reader PASS)'

test-gslt2parse-schema-v1-native: $(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN)
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		bd763693062eeaea7e95fe73f036af4e1707fe1f338c25345a622de25d48e7a4 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/canaries/two_char_v1.metta
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		0186885115acae44b9e982bc6fc19c236f35fda523e3c94abc1a96c0d4e509a9 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/syntax_compiler_v1.metta
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		f3f8841d2e65d3b8cda4bc7691f8059e7d2c97621c245a710de6ac5d872ec722 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/reflection/finite_horn_reflection_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/syntax_compiler_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/relational_cfg_lowering_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/relational_cfg_link_v1.metta
	@$(GSLT2PARSE_SCHEMA_V1_NATIVE_BIN) \
		493efbe57646f3e566e02e5e112d4243728a389f0b1ad1eb6bb1bd5e0cdfcdb5 \
		experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/parserpack/parser_pack_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/reflection/finite_horn_reflection_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/syntax_compiler_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/relational_cfg_lowering_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/relational_cfg_link_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/parser_pack_compiler_v1.metta
	@$(MAKE) --no-print-directory -k \
		test-gslt2parse-schema-v1-native-metamath \
		test-gslt2parse-schema-v1-native-tptp \
		test-gslt2parse-schema-v1-native-megalodon \
		test-gslt2parse-schema-v1-native-prime \
		test-gslt2parse-schema-v1-native-rho-runtime \
		test-gslt2parse-schema-v1-native-rho-reader
	@if rg -ni 'metamath|megalodon|tptp' \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.h \
		experiments/gslt2parse_foundation/presentations/parserpack/parser_pack_core_v1.metta \
		experiments/gslt2parse_foundation/presentations/reflection/finite_horn_reflection_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/syntax_compiler_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/relational_cfg_lowering_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/relational_cfg_link_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/parser_pack_compiler_v1.metta \
		experiments/gslt2parse_foundation/presentations/compiler/parser_action_bytecode_compiler_v1.metta; then \
		echo 'guest-language name leaked into the generic schema or compiler'; \
		exit 1; \
	fi

test-gslt2parse-schema-v1: test-gslt2parse-schema-v1-native
	@python3 tools/test_gslt2parse_schema_v1.py

test-rule-machine-gslt-v1: $(BIN) $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@python3 tools/test_rule_machine_gslt_v1.py \
		--cetta "$(abspath $(BIN))" \
		--chart "$(GSLT2PARSE_CHART_V1_NATIVE_BIN)" \
		--nil-root "$(CURDIR)/../../repos/ngeiswei-chaining-run"

test-subzero-free-bag-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(SUBZERO_FREE_BAG_CORE_V1) \
		$(SUBZERO_ONE_STEP_OBSERVATION_V1) \
		$(SUBZERO_PUBLIC_RESULT_BAG_V1) \
		$(SUBZERO_FREE_BAG_TEST_V1)
	@python3 $(SUBZERO_FREE_BAG_TEST_V1) \
		--chart "$(GSLT2PARSE_CHART_V1_NATIVE_BIN)" \
		--core "$(SUBZERO_FREE_BAG_CORE_V1)" \
		--observation "$(SUBZERO_ONE_STEP_OBSERVATION_V1)" \
		--public-observation "$(SUBZERO_PUBLIC_RESULT_BAG_V1)"

$(GSLT_HORN_RUNTIME_TEST_BIN): \
		tests/support/test_gslt_horn_runtime.c \
		src/gslt_horn_runtime.h \
		$(GSLT_HORN_RUNTIME_CANARY_V1) \
		$(SUBZERO_FREE_BAG_CORE_V1) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_gslt_horn_runtime.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

test-gslt-horn-runtime: $(GSLT_HORN_RUNTIME_TEST_BIN)
	@$(GSLT_HORN_RUNTIME_TEST_BIN) \
		$(GSLT_HORN_RUNTIME_CANARY_V1) \
		$(SUBZERO_FREE_BAG_CORE_V1)

$(GSLT_LANGUAGE_RUNTIME_TEST_BIN): \
		tests/support/test_gslt_language_runtime.c \
		src/gslt_language_runtime.h \
		$(SUBZERO_GENERATED_LANGUAGE_V1_H) \
		$(SUBZERO_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		$(GSLT_IL_GENERATED_LANGUAGE_V1_H) \
		$(GSLT_IL_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_H) \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C) \
		$(GSLT_COMPILED_CANARY_V1_GENERATED_H) \
		$(GSLT_COMPILED_CANARY_V1_GENERATED_C) \
		$(GSLT_PIPELINE_CANARY_V1_GENERATED_H) \
		$(GSLT_PIPELINE_CANARY_V1_GENERATED_C) \
		$(SUBZERO_LANGDEF_V1) \
		$(SUBZERO_FREE_BAG_CORE_V1) \
		$(SUBZERO_PUBLIC_RESULT_BAG_V1) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(COMPILED_READER_RUNTIME_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_gslt_language_runtime.c \
		$(GSLT_COMPILED_CANARY_V1_GENERATED_C) \
		$(GSLT_PIPELINE_CANARY_V1_GENERATED_C) \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(COMPILED_READER_RUNTIME_OBJ) $(LDFLAGS)

test-gslt-language-runtime: \
		test-gslt-dense-bitset-v1 $(GSLT_LANGUAGE_RUNTIME_TEST_BIN)
	@$(GSLT_LANGUAGE_RUNTIME_TEST_BIN) $(SUBZERO_LANGDEF_V1)

$(GSLT_COMPILED_PACKET_CHECK_BIN): \
		tests/support/check_gslt_compiled_packet_v1.c \
		src/gslt_compiled_runtime.h \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(COMPILED_READER_RUNTIME_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/check_gslt_compiled_packet_v1.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(COMPILED_READER_RUNTIME_OBJ) $(LDFLAGS)

.PHONY: check-gslt-compiled-packet-v1
check-gslt-compiled-packet-v1: $(GSLT_COMPILED_PACKET_CHECK_BIN)
	@test -n "$(PACKET)" || \
		{ echo "PACKET must name a CGP1 file" >&2; exit 2; }
	@$(GSLT_COMPILED_PACKET_CHECK_BIN) "$(PACKET)"

$(NIK_RUNTIME_TEST_BIN): \
		tests/support/test_nik_runtime_v1.c \
		src/nik_runtime.h \
		src/nik_runtime_internal.h \
		src/inference_checker.h \
		$(PRIME_NIK_AUTHORITIES_GENERATED_H) \
		$(PRIME_NIK_AUTHORITIES_GENERATED_C) \
		$(PRIME_NIK_RUNTIME_GENERATED_H) \
		$(PRIME_NIK_RUNTIME_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(COMPILED_READER_RUNTIME_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_nik_runtime_v1.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(COMPILED_READER_RUNTIME_OBJ) $(LDFLAGS)

.PHONY: test-nik-runtime-v1
test-nik-runtime-v1: $(NIK_RUNTIME_TEST_BIN)
	@$(NIK_RUNTIME_TEST_BIN)

$(GSLT_PROVIDER_RUNTIME_TEST_BIN): \
		tests/support/test_gslt_provider_runtime.c \
		src/gslt_provider_runtime.h \
		src/gslt_space_fact_provider_v1.h \
		src/gslt_finite_fact_provider_v1.h \
		src/gslt_horn_runtime.h \
		src/gslt_compiled_runtime.h \
		$(GSLT_PROVIDER_CANARY_V1_GENERATED_H) \
		$(GSLT_PROVIDER_CANARY_V1_GENERATED_C) \
		$(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_H) \
		$(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_gslt_provider_runtime.c \
		$(GSLT_PROVIDER_CANARY_V1_GENERATED_C) \
		$(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

.PHONY: test-gslt-provider-runtime
test-gslt-provider-runtime: $(GSLT_PROVIDER_RUNTIME_TEST_BIN)
	@$(GSLT_PROVIDER_RUNTIME_TEST_BIN)

$(PROOF_TRACE_COMPILED_RUNTIME_V1_TEST_BIN): \
		tests/support/test_proof_trace_compiled_runtime_v1.c \
		$(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_H) \
		$(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_C) \
		$(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_H) \
		$(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_C) \
		src/gslt_finite_fact_provider_v1.h \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_proof_trace_compiled_runtime_v1.c \
		$(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_C) \
		$(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

.PHONY: test-metamath-cogslt-proof-trace-compiled-v1
test-metamath-cogslt-proof-trace-compiled-v1: \
		$(PROOF_TRACE_COMPILED_RUNTIME_V1_TEST_BIN)
	@if rg -ni 'metamath|proof[_ -]?trace|compressed[_ -]?proof|mandatory[_ -]?hypothesis' \
			src/gslt_compiled_runtime.c src/gslt_compiled_runtime.h; then \
		echo 'guest-specific proof vocabulary entered the generic compiled runtime' >&2; \
		exit 1; \
	fi
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_trace_v1 \
		--header-include langdef/metamath/generated/proof_trace_language_v1.generated.h
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATION_TEST_V1) \
		--generator $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1) \
		--language-manifest $(METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_trace_provider_catalog_v1 \
		--header-include langdef/metamath/generated/proof_trace_provider_catalog_v1.generated.h
	@$(PROOF_TRACE_COMPILED_RUNTIME_V1_TEST_BIN) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1) \
		tests/langdef/metamath/proof_trace_service_normal.query \
		tests/langdef/metamath/proof_trace_service_wrong_target.query \
		tests/langdef/metamath/proof_trace_service_compressed.query \
		tests/langdef/metamath/proof_trace_service_compressed_range.query \
		tests/langdef/metamath/proof_trace_service_incomplete_normal.query \
		tests/langdef/metamath/proof_trace_service_unknown_not_verified.query

$(GSLT_ABT_PROVIDER_V1_TEST_BIN): \
		tests/support/test_gslt_abt_provider_v1.c \
		src/gslt_abt_provider_v1.h \
		src/gslt_provider_runtime.h \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_gslt_abt_provider_v1.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

.PHONY: test-gslt-abt-provider-v1
test-gslt-abt-provider-v1: $(GSLT_ABT_PROVIDER_V1_TEST_BIN)
	@$(GSLT_ABT_PROVIDER_V1_TEST_BIN)

$(ZERO_INTERACT_SPACE_PROVIDER_TEST_BIN): \
		tests/support/test_zero_interact_space_provider_v1.c \
		src/gslt_revisioned_space_provider_v1.h \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_H) \
		$(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_zero_interact_space_provider_v1.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

.PHONY: test-zero-interact-space-provider-v1
test-zero-interact-space-provider-v1: $(ZERO_INTERACT_SPACE_PROVIDER_TEST_BIN)
	@$(ZERO_INTERACT_SPACE_PROVIDER_TEST_BIN)

$(GSLT_SUPPORT_TRANSFORM_RUNTIME_TEST_BIN): \
		tests/support/test_gslt_support_transform_runtime.c \
		src/gslt_support_transform_runtime.h \
		$(MM2_GSLT_PROFILE_GENERATED_H) \
		$(MM2_GSLT_PROFILE_GENERATED_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/test_gslt_support_transform_runtime.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

.PHONY: test-gslt-support-transform-runtime
test-gslt-support-transform-runtime: $(GSLT_SUPPORT_TRANSFORM_RUNTIME_TEST_BIN)
	@$(GSLT_SUPPORT_TRANSFORM_RUNTIME_TEST_BIN)

.PHONY: test-gslt-support-transform-generation-v1
test-gslt-support-transform-generation-v1: \
		$(GSLT_SUPPORT_TRANSFORM_GENERATION_TEST_V1) \
		$(MM2_GSLT_PROFILE_GENERATED_H) \
		$(MM2_GSLT_PROFILE_GENERATED_C)
	@python3 $(GSLT_SUPPORT_TRANSFORM_GENERATION_TEST_V1) \
		--generator $(GSLT_SUPPORT_TRANSFORM_GENERATOR_V1) \
		--manifest $(MM2_GSLT_PROFILE_V1) \
		--header $(MM2_GSLT_PROFILE_GENERATED_H) \
		--source $(MM2_GSLT_PROFILE_GENERATED_C) \
		--symbol cetta_mm2_gslt_profile_v1 \
		--header-include generated/mm2_gslt_profile_v1.generated.h

$(METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN): \
		tests/support/check_mettazero_compilation_certificate_v1.c \
		$(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) \
		$(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ \
		tests/support/check_mettazero_compilation_certificate_v1.c \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

test-subzero-cli-v1: $(BIN) test-gslt-language-runtime
	@set -e; \
	for realization in horn-reference compiled-worklist; do \
		for source in tests/subzero/*.metta; do \
			expected="$${source%.metta}.expected"; \
			actual=$$(mktemp "$(BOOTSTRAP_TMPDIR)/subzero-cli.XXXXXX"); \
			trap 'rm -f "$$actual"' EXIT; \
			./$(BIN) --lang subzero --gslt-realization "$$realization" \
				"$$source" >"$$actual"; \
			diff -u "$$expected" "$$actual"; \
			rm -f "$$actual"; \
			trap - EXIT; \
		done; \
	done; \
	if ./$(BIN) --lang he --gslt-realization compiled-worklist \
		-e '!(+ 1 2)' >/dev/null 2>&1; then \
		echo 'FAIL: GSLT realization leaked outside generated language'; \
		exit 1; \
	fi; \
	he_result=$$(./$(BIN) --lang he -e '!(+ 1 2)'); \
	test "$$he_result" = '[3]'; \
	echo '(SubzeroCliV1Summary realizations=2 fixtures=8 bag=1 contextual=2 inert=2 scope=1 scalars=1 empty=1 he-isolation=1)'

test-mettazero-cli-v1: $(BIN)
	@set -e; \
	for realization in horn-reference compiled-worklist; do \
		for source in tests/zero/*.metta; do \
			expected="$${source%.metta}.expected"; \
			actual=$$(mktemp "$(BOOTSTRAP_TMPDIR)/mettazero-cli.XXXXXX"); \
			trap 'rm -f "$$actual"' EXIT; \
			./$(BIN) --lang zero --gslt-realization "$$realization" \
				"$$source" >"$$actual"; \
			diff -u "$$expected" "$$actual"; \
			rm -f "$$actual"; \
			trap - EXIT; \
		done; \
	done; \
	echo '(MettaZeroCliV1Summary realizations=2 fixtures=14 query=4 reflection=1 evaluation=1 inert=2 hygiene=2 reify=4)'

test-mettazero-emit-cli-v1: $(BIN)
	@set -e; \
	for realization in horn-reference compiled-worklist; do \
		for source in tests/zero_emit/*.metta; do \
			expected="$${source%.metta}.expected"; \
			actual=$$(mktemp "$(BOOTSTRAP_TMPDIR)/mettazero-emit-cli.XXXXXX"); \
			trap 'rm -f "$$actual"' EXIT; \
			./$(BIN) --lang zero --profile emit \
				--gslt-realization "$$realization" "$$source" >"$$actual"; \
			diff -u "$$expected" "$$actual"; \
			rm -f "$$actual"; \
			trap - EXIT; \
		done; \
	done; \
	echo '(MettaZeroEmitCliV1Summary realizations=2 fixtures=10 emit=3 add-alias=1 match=5 let=8 eval=1 duplicates=1 branch-local=1 inert=1)'

.PHONY: test-mettazero-interact-cli-v1
test-mettazero-interact-cli-v1: $(BIN)
	@set -e; \
	engines='native'; \
	engine_count=1; \
	if test "$(ENABLE_PATHMAP_SPACE)" = 1; then \
		engines='native pathmap'; \
		engine_count=2; \
	fi; \
	for engine in $$engines; do \
		for realization in horn-reference compiled-worklist; do \
			for source in tests/zero_emit/*.metta tests/zero_interact/*.metta; do \
				expected="$${source%.metta}.expected"; \
				actual=$$(mktemp "$(BOOTSTRAP_TMPDIR)/mettazero-interact-cli.XXXXXX"); \
				trap 'rm -f "$$actual"' EXIT; \
				./$(BIN) --lang zero --profile interact \
					--space-engine "$$engine" \
					--gslt-realization "$$realization" \
					"$$source" >"$$actual"; \
				diff -u "$$expected" "$$actual"; \
				rm -f "$$actual"; \
				trap - EXIT; \
			done; \
		done; \
	done; \
	echo "(MettaZeroInteractCliV1Summary engines=$$engine_count realizations=2 fixtures=15 occurrence-bag=1 branch-local=1 immutable-worlds=1 indexed-candidate=1 support-indexed-abt=2)"

test-mettazero-emit-semantics-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_REVISIONED_EMIT_V1) \
		$(METTAZERO_EMIT_SEMANTICS_TEST_V1)
	@python3 $(METTAZERO_EMIT_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--observation $(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		--emit $(METTAZERO_REVISIONED_EMIT_V1)

test-mettazero-emit-rule-mutations-v1: \
		test-mettazero-emit-semantics-v1 \
		$(METTAZERO_EMIT_RULE_MUTATIONS_TEST_V1) \
		tools/test_gslt_language_rule_mutations_v1.py
	@python3 $(METTAZERO_EMIT_RULE_MUTATIONS_TEST_V1) \
		--harness $(METTAZERO_EMIT_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--observation $(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		--emit $(METTAZERO_REVISIONED_EMIT_V1)

.PHONY: test-mettazero-interact-semantics-v1
test-mettazero-interact-semantics-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_OPEN_SUBSTITUTION_V1) \
		$(METTAZERO_SUPPORT_INDEXED_ABT_MATCH_V1) \
		$(METTAZERO_REVISIONED_INTERACT_V1) \
		$(METTAZERO_INTERACT_PROVIDER_REFERENCE_V1) \
		$(METTAZERO_INTERACT_SEMANTICS_TEST_V1)
	@python3 $(METTAZERO_INTERACT_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--observation $(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		--open-substitution $(METTAZERO_OPEN_SUBSTITUTION_V1) \
		--support-indexed-abt $(METTAZERO_SUPPORT_INDEXED_ABT_MATCH_V1) \
		--interact $(METTAZERO_REVISIONED_INTERACT_V1) \
		--provider $(METTAZERO_INTERACT_PROVIDER_REFERENCE_V1)

.PHONY: test-mettazero-interact-rule-mutations-v1
test-mettazero-interact-rule-mutations-v1: \
		test-mettazero-interact-semantics-v1 \
		$(METTAZERO_INTERACT_RULE_MUTATIONS_TEST_V1) \
		tools/test_gslt_language_rule_mutations_v1.py
	@python3 $(METTAZERO_INTERACT_RULE_MUTATIONS_TEST_V1) \
		--harness $(METTAZERO_INTERACT_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--observation $(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		--open-substitution $(METTAZERO_OPEN_SUBSTITUTION_V1) \
		--support-indexed-abt $(METTAZERO_SUPPORT_INDEXED_ABT_MATCH_V1) \
		--interact $(METTAZERO_REVISIONED_INTERACT_V1) \
		--provider $(METTAZERO_INTERACT_PROVIDER_REFERENCE_V1)

test-mettazero-generation-v1: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_H) \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_language_v1 \
		--header-include generated/zero_language_v1.generated.h
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile exp \
		--header $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_exp_language_v1 \
		--header-include generated/zero_exp_language_v1.generated.h
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile emit \
		--header $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_emit_language_v1 \
		--header-include generated/zero_emit_language_v1.generated.h
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_GROUND_LIBRARY_CANARY_V1_MANIFEST) \
		--source-root . \
		--header $(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_H) \
		--source $(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C) \
		--symbol cetta_zero_ground_library_v1 \
		--header-include tests/generated/metta_zero_ground_library_v1.generated.h

.PHONY: test-mettazero-interact-generation-v1
test-mettazero-interact-generation-v1: \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		$(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_H) \
		$(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile interact \
		--header $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_interact_language_v1 \
		--header-include generated/zero_interact_language_v1.generated.h
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATION_TEST_V1) \
		--generator $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(METTAZERO_INTERACT_PROVIDER_CATALOG_V1) \
		--language-manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_H) \
		--source $(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_C) \
		--symbol cetta_zero_interact_provider_catalog_v1 \
		--header-include generated/zero_interact_provider_catalog_v1.generated.h

test-mettazero-compilation-certificate-v1: \
		$(BIN) \
		$(METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN) \
		$(METTAZERO_COMPILATION_CERTIFICATE_V1) \
		$(METTAZERO_EXP_COMPILATION_CERTIFICATE_V1) \
		$(METTAZERO_EMIT_COMPILATION_CERTIFICATE_V1) \
		$(METTAZERO_INTERACT_COMPILATION_CERTIFICATE_V1) \
		$(GSLT_COMPILATION_CERTIFICATE_TEST_V1)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_TEST_V1) \
		--checker $(METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN) \
		--producer $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile base \
		--header $(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_language_v1 \
		--certificate $(METTAZERO_COMPILATION_CERTIFICATE_V1)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_TEST_V1) \
		--checker $(METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN) \
		--producer $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile exp \
		--header $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_exp_language_v1 \
		--certificate $(METTAZERO_EXP_COMPILATION_CERTIFICATE_V1)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_TEST_V1) \
		--checker $(METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN) \
		--producer $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile emit \
		--header $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_emit_language_v1 \
		--certificate $(METTAZERO_EMIT_COMPILATION_CERTIFICATE_V1)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_TEST_V1) \
		--checker $(METTAZERO_COMPILATION_CERTIFICATE_CHECKER_V1_BIN) \
		--producer $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile interact \
		--header $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_interact_language_v1 \
		--certificate $(METTAZERO_INTERACT_COMPILATION_CERTIFICATE_V1)
	@if strings ./$(BIN) | grep -Fq \
			'GsltCompilationCertificateV1Accepted'; then \
		echo 'FAIL: compilation checker leaked into the normal runtime'; \
		exit 1; \
	fi
	@echo '(MettaZeroCompilationCertificateV1Summary profiles=4 stages=4 tamper-rejections=20 runtime-linked=0)'

test-mettazero-realization-triangle-v1: \
		$(BIN) \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_SEMANTIC_RUNNER_V1) \
		tests/fixtures/metta_zero_ground_capability_v1.metta \
		tools/test_mettazero_realization_triangle_v1.py
	@python3 tools/test_mettazero_realization_triangle_v1.py \
		--cetta ./$(BIN) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--observation $(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		--runner $(METTAZERO_SEMANTIC_RUNNER_V1) \
		--ground-capability tests/fixtures/metta_zero_ground_capability_v1.metta

test-mettazero-rule-mutations-v1: \
		test-mettazero-realization-triangle-v1 \
		tools/test_mettazero_rule_mutations_v1.py \
		tools/test_gslt_language_rule_mutations_v1.py
	@python3 tools/test_mettazero_rule_mutations_v1.py \
		--harness tools/test_mettazero_realization_triangle_v1.py \
		--cetta ./$(BIN) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--observation $(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		--runner $(METTAZERO_SEMANTIC_RUNNER_V1) \
		--ground-capability tests/fixtures/metta_zero_ground_capability_v1.metta

.PHONY: test-mettazero-interact-v1
test-mettazero-interact-v1: \
		test-mettazero-interact-generation-v1 \
		test-mettazero-compilation-certificate-v1 \
		test-mettazero-interact-rule-mutations-v1 \
		test-mettazero-interact-cli-v1 \
		test-gslt-abt-provider-v1 \
		test-zero-interact-space-provider-v1
	@echo '(MettaZeroInteractV1Summary generated=1 provider-catalog=1 authored-rules=55 mutation-kills=55 physical-providers=2 abt-support-indexed=1 differential=1 fail-closed=1)'

test-mettazero: \
		test-mettazero-generation-v1 \
		test-mettazero-compilation-certificate-v1 \
		test-mettazero-cli-v1 \
		test-mettazero-emit-cli-v1 \
		test-mettazero-emit-semantics-v1 \
		test-mettazero-emit-rule-mutations-v1 \
		test-mettazero-interact-v1 \
		test-mettazero-realization-triangle-v1 \
		test-mettazero-rule-mutations-v1
	@echo 'PASS: staged query-first MeTTa Zero candidate'

test-gslt-il-generation-v1: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		$(GSLT_IL_GENERATED_LANGUAGE_V1_H) \
		$(GSLT_IL_GENERATED_LANGUAGE_V1_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(GSLT_IL_LANGDEF_V1) \
		--source-root langdef \
		--header $(GSLT_IL_GENERATED_LANGUAGE_V1_H) \
		--source $(GSLT_IL_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_gslt_il_language_v1 \
		--header-include generated/gslt_il_language_v1.generated.h

test-gslt-il-cli-v1: $(BIN) $(GSLT_IL_CLI_TEST_V1)
	@python3 $(GSLT_IL_CLI_TEST_V1) \
		--cetta ./$(BIN) \
		--fixtures tests/gslt_il

test-gslt-il-semantics-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(GSLT_IL_FINITE_COMMAND_V1) \
		$(GSLT_IL_SEMANTICS_TEST_V1)
	@python3 $(GSLT_IL_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--semantics $(GSLT_IL_FINITE_COMMAND_V1)

test-gslt-il-rule-mutations-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(GSLT_IL_FINITE_COMMAND_V1) \
		$(GSLT_IL_SEMANTICS_TEST_V1) \
		$(GSLT_IL_RULE_MUTATIONS_TEST_V1) \
		tools/test_gslt_language_rule_mutations_v1.py
	@python3 $(GSLT_IL_RULE_MUTATIONS_TEST_V1) \
		--harness $(GSLT_IL_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--semantics $(GSLT_IL_FINITE_COMMAND_V1)

test-gslt-il: \
		test-gslt-il-generation-v1 \
		test-gslt-il-cli-v1 \
		test-gslt-il-semantics-v1 \
		test-gslt-il-rule-mutations-v1
	@echo 'PASS: experimental generated MeTTa-facing indexed GSLT metalanguage'

test-zerouv-generation-v1: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		$(ZEROUV_GENERATED_LANGUAGE_V1_H) \
		$(ZEROUV_GENERATED_LANGUAGE_V1_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(ZEROUV_LANGDEF_V1) \
		--source-root langdef \
		--header $(ZEROUV_GENERATED_LANGUAGE_V1_H) \
		--source $(ZEROUV_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zerouv_language_v1 \
		--header-include generated/zerouv_language_v1.generated.h

test-zerouv-cli-v1: $(BIN) $(ZEROUV_CLI_TEST_V1)
	@python3 $(ZEROUV_CLI_TEST_V1) \
		--cetta ./$(BIN) \
		--fixtures tests/zerouv

test-zerouv-semantics-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(ZEROUV_CONTROL_V1) \
		$(ZEROUV_SEMANTICS_TEST_V1)
	@python3 $(ZEROUV_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--control $(ZEROUV_CONTROL_V1)

test-zerouv-rule-mutations-v1: \
		test-zerouv-semantics-v1 \
		$(ZEROUV_RULE_MUTATIONS_TEST_V1) \
		tools/test_gslt_language_rule_mutations_v1.py
	@python3 $(ZEROUV_RULE_MUTATIONS_TEST_V1) \
		--harness $(ZEROUV_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--query-kernel $(METTAZERO_QUERY_KERNEL_V1) \
		--control $(ZEROUV_CONTROL_V1)

test-zerouv: \
		test-zerouv-generation-v1 \
		test-zerouv-cli-v1 \
		test-zerouv-semantics-v1 \
		test-zerouv-rule-mutations-v1
	@echo 'PASS: experimental productive and recurrent MeTTa candidate'

test-metta-interact-generation-v1: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		$(METTA_INTERACT_GENERATED_LANGUAGE_V1_H) \
		$(METTA_INTERACT_GENERATED_LANGUAGE_V1_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTA_INTERACT_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTA_INTERACT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTA_INTERACT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_metta_interact_language_v1 \
		--header-include generated/metta_interact_language_v1.generated.h

test-metta-interact-cli-v1: $(BIN) $(METTA_INTERACT_CLI_TEST_V1)
	@python3 $(METTA_INTERACT_CLI_TEST_V1) \
		--cetta ./$(BIN) \
		--fixtures tests/metta_interact

test-metta-interact-semantics-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTA_INTERACT_CORE_V1) \
		$(METTA_INTERACT_SEQUENCE_V1) \
		$(METTA_INTERACT_SEMANTICS_TEST_V1)
	@python3 $(METTA_INTERACT_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--core $(METTA_INTERACT_CORE_V1) \
		--sequence $(METTA_INTERACT_SEQUENCE_V1)

test-metta-interact-rule-mutations-v1: \
		test-metta-interact-semantics-v1 \
		$(METTA_INTERACT_RULE_MUTATIONS_TEST_V1) \
		tools/test_gslt_language_rule_mutations_v1.py
	@python3 $(METTA_INTERACT_RULE_MUTATIONS_TEST_V1) \
		--harness $(METTA_INTERACT_SEMANTICS_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--quote-match $(METTAZERO_QUOTE_MATCH_V1) \
		--core $(METTA_INTERACT_CORE_V1) \
		--sequence $(METTA_INTERACT_SEQUENCE_V1)

test-metta-interact: \
		test-metta-interact-generation-v1 \
		test-metta-interact-cli-v1 \
		test-metta-interact-semantics-v1 \
		test-metta-interact-rule-mutations-v1
	@echo 'PASS: experimental proof-relevant MeTTa interaction language'

test-gslt-language-generation-v1: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		$(SUBZERO_GENERATED_LANGUAGE_V1_H) \
		$(SUBZERO_GENERATED_LANGUAGE_V1_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(SUBZERO_LANGDEF_V1) \
		--header $(SUBZERO_GENERATED_LANGUAGE_V1_H) \
		--source $(SUBZERO_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_subzero_language_v1 \
		--header-include generated/subzero_language_v1.generated.h

test-subzero-rule-mutations-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(SUBZERO_FREE_BAG_TEST_V1) \
		$(SUBZERO_FREE_BAG_CORE_V1) \
		$(SUBZERO_ONE_STEP_OBSERVATION_V1) \
		$(SUBZERO_PUBLIC_RESULT_BAG_V1)
	@python3 tools/test_gslt_language_rule_mutations_v1.py \
		--harness $(SUBZERO_FREE_BAG_TEST_V1) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--core $(SUBZERO_FREE_BAG_CORE_V1) \
		--observation $(SUBZERO_ONE_STEP_OBSERVATION_V1) \
		--public-observation $(SUBZERO_PUBLIC_RESULT_BAG_V1)

test-subzero-realization-triangle-v1: \
		$(BIN) \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(SUBZERO_FREE_BAG_CORE_V1) \
		$(SUBZERO_PUBLIC_RESULT_BAG_V1) \
		tools/test_subzero_realization_triangle_v1.py \
		src/gslt_compiled_runtime.c
	@python3 tools/test_subzero_realization_triangle_v1.py \
		--cetta ./$(BIN) \
		--chart $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--core $(SUBZERO_FREE_BAG_CORE_V1) \
		--public-observation $(SUBZERO_PUBLIC_RESULT_BAG_V1) \
		--compiled-runtime src/gslt_compiled_runtime.c

test-subzero: \
		test-gslt-language-generation-v1 \
		test-subzero-free-bag-v1 \
		test-subzero-rule-mutations-v1 \
		test-subzero-realization-triangle-v1 \
		test-gslt-horn-runtime \
		test-gslt-language-runtime \
		test-subzero-cli-v1
	@echo 'PASS: staged Subzero candidate'

$(PRIME_NIK_AUTHORITY_SEMANTICS_V1) \
$(PRIME_NIK_AUTHORITIES_GENERATED_H) \
$(PRIME_NIK_AUTHORITIES_GENERATED_C) &: \
		$(PRIME_NIK_AUTHORITY_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@python3 $(PRIME_NIK_AUTHORITY_GENERATOR_V1) \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--semantic $(PRIME_NIK_AUTHORITY_SEMANTICS_V1) \
		--header $(PRIME_NIK_AUTHORITIES_GENERATED_H) \
		--source $(PRIME_NIK_AUTHORITIES_GENERATED_C) \
		--symbol cetta_prime_nik_authorities_v1 \
		--header-include generated/prime_nik_authorities_v1.generated.h

$(PRIME_NIK_RUNTIME_GENERATED_H) $(PRIME_NIK_RUNTIME_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(PRIME_NIK_RUNTIME_MANIFEST_V1) \
		$(PRIME_NIK_AUTHORITY_SEMANTICS_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(PRIME_NIK_RUNTIME_MANIFEST_V1) \
		--source-root langdef \
		--header $(PRIME_NIK_RUNTIME_GENERATED_H) \
		--source $(PRIME_NIK_RUNTIME_GENERATED_C) \
		--symbol cetta_prime_nik_runtime_v1 \
		--header-include generated/prime_nik_runtime_v1.generated.h

$(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_H) \
$(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_C) &: \
		$(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_V1) \
		$(PRIME_NIK_RUNTIME_MANIFEST_V1) \
		$(PRIME_NIK_RUNTIME_GENERATED_H) \
		$(PRIME_NIK_RUNTIME_GENERATED_C)
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_V1) \
		--language-manifest $(PRIME_NIK_RUNTIME_MANIFEST_V1) \
		--source-root langdef \
		--header $(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_H) \
		--source $(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_C) \
		--symbol cetta_prime_nik_side_condition_provider_catalog_v1 \
		--header-include generated/prime_nik_side_condition_provider_catalog_v1.generated.h

.PHONY: test-prime-nik-generation-v1
test-prime-nik-generation-v1: \
		$(PRIME_NIK_AUTHORITY_GENERATION_TEST_V1) \
		$(PRIME_NIK_AUTHORITY_SEMANTICS_V1) \
		$(PRIME_NIK_AUTHORITIES_GENERATED_H) \
		$(PRIME_NIK_AUTHORITIES_GENERATED_C) \
		$(PRIME_NIK_RUNTIME_GENERATED_H) \
		$(PRIME_NIK_RUNTIME_GENERATED_C) \
		$(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_H) \
		$(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_C)
	@python3 $(PRIME_NIK_AUTHORITY_GENERATION_TEST_V1) \
		--generator $(PRIME_NIK_AUTHORITY_GENERATOR_V1) \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--semantic $(PRIME_NIK_AUTHORITY_SEMANTICS_V1) \
		--header $(PRIME_NIK_AUTHORITIES_GENERATED_H) \
		--source $(PRIME_NIK_AUTHORITIES_GENERATED_C) \
		--symbol cetta_prime_nik_authorities_v1 \
		--header-include generated/prime_nik_authorities_v1.generated.h
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(PRIME_NIK_RUNTIME_MANIFEST_V1) \
		--source-root langdef \
		--header $(PRIME_NIK_RUNTIME_GENERATED_H) \
		--source $(PRIME_NIK_RUNTIME_GENERATED_C) \
		--symbol cetta_prime_nik_runtime_v1 \
		--header-include generated/prime_nik_runtime_v1.generated.h
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATION_TEST_V1) \
		--generator $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_V1) \
		--language-manifest $(PRIME_NIK_RUNTIME_MANIFEST_V1) \
		--source-root langdef \
		--header $(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_H) \
		--source $(PRIME_NIK_SIDE_CONDITION_PROVIDER_CATALOG_GENERATED_C) \
		--symbol cetta_prime_nik_side_condition_provider_catalog_v1 \
		--header-include generated/prime_nik_side_condition_provider_catalog_v1.generated.h

.PHONY: test-prime-nik-lean-export-v1
test-prime-nik-lean-export-v1: $(PRIME_NIK_AUTHORITY_EXPORTER_V1)
	@test -n "$(METTAPEDIA_LEAN_ROOT)" || { \
		echo "METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project"; \
		exit 2; \
	}
	@set -eu; \
	mkdir -p $(BOOTSTRAP_TMPDIR); \
	tmp_catalog=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-catalog.XXXXXX"); \
	tmp_catalog_abs=$$(realpath "$$tmp_catalog"); \
	checked_catalog_abs=$$(realpath "$(PRIME_NIK_AUTHORITY_CATALOG_V1)"); \
	trap 'rm -f "$$tmp_catalog_abs"' EXIT INT TERM; \
	cd "$(METTAPEDIA_LEAN_ROOT)" && \
		lake env lean --run \
			"$(abspath $(PRIME_NIK_AUTHORITY_EXPORTER_V1))" \
			"$$tmp_catalog_abs"; \
	cmp -s "$$tmp_catalog_abs" "$$checked_catalog_abs"; \
	echo "(PrimeNikLeanExportV1Summary exact=1 authorities=4)"

.PHONY: test-prime-nik-megalodon-v1
test-prime-nik-megalodon-v1: \
		$(PRIME_NIK_MEGALODON_TEST_V1) \
		$(PRIME_NIK_MEGALODON_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_NEGATIVE_V1)
	@set -eu; \
	if [ -x "$(MEGALODON_AUTO_BIN)" ]; then \
		python3 $(PRIME_NIK_MEGALODON_TEST_V1) \
			--megalodon "$(MEGALODON_AUTO_BIN)" \
			--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
			--positive $(PRIME_NIK_MEGALODON_POSITIVE_V1) \
			--negative $(PRIME_NIK_MEGALODON_NEGATIVE_V1); \
	else \
		echo "SKIP: Megalodon executable differential (binary unavailable)"; \
	fi

.PHONY: test-prime-nik-megalodon-term-v1
test-prime-nik-megalodon-term-v1: \
		$(BIN) \
		$(PRIME_NIK_MEGALODON_TERM_TEST_V1) \
		$(PRIME_NIK_MEGALODON_TERM_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_TERM_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_TERM_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable term differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_TERM_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-term.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_TERM_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_TERM_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--positive $(PRIME_NIK_MEGALODON_TERM_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

.PHONY: test-prime-nik-megalodon-polymorphic-v1
test-prime-nik-megalodon-polymorphic-v1: \
		$(BIN) \
		$(PRIME_NIK_MEGALODON_POLY_TEST_V1) \
		$(PRIME_NIK_MEGALODON_POLY_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_POLY_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_POLY_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable polymorphic differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_POLY_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-poly.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_POLY_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_POLY_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--positive $(PRIME_NIK_MEGALODON_POLY_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

.PHONY: test-prime-nik-megalodon-known-implication-v1
test-prime-nik-megalodon-known-implication-v1: \
		$(BIN) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_TEST_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable known-implication differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-known-imp.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_KNOWN_IMP_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--positive $(PRIME_NIK_MEGALODON_KNOWN_IMP_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

.PHONY: test-prime-nik-megalodon-definition-v1
test-prime-nik-megalodon-definition-v1: \
		$(BIN) \
		$(PRIME_NIK_MEGALODON_DEFINITION_TEST_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable definition differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-definition.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_DEFINITION_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--positive $(PRIME_NIK_MEGALODON_DEFINITION_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

.PHONY: test-prime-nik-proof-dag-v1
test-prime-nik-proof-dag-v1: \
		$(PRIME_NIK_PROOF_DAG_COMPILER_V1) \
		$(PRIME_NIK_PROOF_DAG_TEST_V1)
	python3 $(PRIME_NIK_PROOF_DAG_TEST_V1)

.PHONY: test-prime-nik-megalodon-tactics-package-v1
test-prime-nik-megalodon-tactics-package-v1: \
		$(BIN) \
		$(NIK_RUNTIME_TEST_BIN) \
		$(PRIME_NIK_PROOF_DAG_COMPILER_V1) \
		$(PRIME_NIK_MEGALODON_TACTICS_TEST_V1) \
		$(PRIME_NIK_MEGALODON_TACTICS_POSITIVE_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon tactics-package differential (binary unavailable)"; \
		exit 0; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_TACTICS_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--differential "$(abspath $(NIK_RUNTIME_TEST_BIN))" \
		--positive $(PRIME_NIK_MEGALODON_TACTICS_POSITIVE_V1)

.PHONY: test-prime-nik-core-v1
test-prime-nik-core-v1: test-nik-runtime-v1 test-prime-nik-generation-v1 \
		test-prime-nik-proof-dag-v1

.PHONY: test-prime-nik-qualification-v1
test-prime-nik-qualification-v1: \
		test-prime-nik-megalodon-v1 test-prime-nik-megalodon-term-v1 \
		test-prime-nik-megalodon-polymorphic-v1 \
		test-prime-nik-megalodon-known-implication-v1 \
		test-prime-nik-megalodon-definition-v1 \
		test-prime-nik-megalodon-tactics-package-v1
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	if [ -n "$$lean_root" ]; then \
		$(MAKE) -s METTAPEDIA_LEAN_ROOT="$$lean_root" \
			test-prime-nik-lean-export-v1; \
	else \
		echo "SKIP: Prime NIK Lean export (Mettapedia Lean project unavailable)"; \
	fi

.PHONY: test-prime-nik-v1
test-prime-nik-v1: test-prime-nik-core-v1 \
		test-prime-nik-qualification-v1

$(MM2_GSLT_PROFILE_GENERATED_H) $(MM2_GSLT_PROFILE_GENERATED_C) &: \
		$(GSLT_SUPPORT_TRANSFORM_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(MM2_GSLT_PROFILE_V1)
	@python3 $(GSLT_SUPPORT_TRANSFORM_GENERATOR_V1) \
		--manifest $(MM2_GSLT_PROFILE_V1) \
		--header $(MM2_GSLT_PROFILE_GENERATED_H) \
		--source $(MM2_GSLT_PROFILE_GENERATED_C) \
		--symbol cetta_mm2_gslt_profile_v1 \
		--header-include generated/mm2_gslt_profile_v1.generated.h

$(GSLT_IL_GENERATED_LANGUAGE_V1_H) $(GSLT_IL_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(GSLT_IL_LANGDEF_V1) \
		$(GSLT_IL_FINITE_COMMAND_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(GSLT_IL_LANGDEF_V1) \
		--source-root langdef \
		--header $(GSLT_IL_GENERATED_LANGUAGE_V1_H) \
		--source $(GSLT_IL_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_gslt_il_language_v1 \
		--header-include generated/gslt_il_language_v1.generated.h

$(ZEROUV_GENERATED_LANGUAGE_V1_H) $(ZEROUV_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(ZEROUV_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(ZEROUV_CONTROL_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(ZEROUV_LANGDEF_V1) \
		--source-root langdef \
		--header $(ZEROUV_GENERATED_LANGUAGE_V1_H) \
		--source $(ZEROUV_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zerouv_language_v1 \
		--header-include generated/zerouv_language_v1.generated.h

$(METTA_INTERACT_GENERATED_LANGUAGE_V1_H) $(METTA_INTERACT_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTA_INTERACT_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTA_INTERACT_CORE_V1) \
		$(METTA_INTERACT_SEQUENCE_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTA_INTERACT_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTA_INTERACT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTA_INTERACT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_metta_interact_language_v1 \
		--header-include generated/metta_interact_language_v1.generated.h

$(SUBZERO_GENERATED_LANGUAGE_V1_H) $(SUBZERO_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(SUBZERO_LANGDEF_V1) \
		$(SUBZERO_FREE_BAG_CORE_V1) \
		$(SUBZERO_PUBLIC_RESULT_BAG_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(SUBZERO_LANGDEF_V1) \
		--header $(SUBZERO_GENERATED_LANGUAGE_V1_H) \
		--source $(SUBZERO_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_subzero_language_v1 \
		--header-include generated/subzero_language_v1.generated.h

$(METTAZERO_GENERATED_LANGUAGE_V1_H) $(METTAZERO_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_language_v1 \
		--header-include generated/zero_language_v1.generated.h

$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_SEMANTIC_RUNNER_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile exp \
		--header $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_exp_language_v1 \
		--header-include generated/zero_exp_language_v1.generated.h

$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_REVISIONED_EMIT_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile emit \
		--header $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_emit_language_v1 \
		--header-include generated/zero_emit_language_v1.generated.h

$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_OPEN_SUBSTITUTION_V1) \
		$(METTAZERO_REVISIONED_INTERACT_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile interact \
		--header $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_interact_language_v1 \
		--header-include generated/zero_interact_language_v1.generated.h

$(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_H) $(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_C) &: \
		$(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_INTERACT_PROVIDER_CATALOG_V1) \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C)
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(METTAZERO_INTERACT_PROVIDER_CATALOG_V1) \
		--language-manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_H) \
		--source $(METTAZERO_INTERACT_PROVIDER_CATALOG_GENERATED_C) \
		--symbol cetta_zero_interact_provider_catalog_v1 \
		--header-include generated/zero_interact_provider_catalog_v1.generated.h

$(METTAZERO_COMPILATION_CERTIFICATE_V1): \
		$(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_GENERATED_LANGUAGE_V1_C)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--header $(METTAZERO_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_language_v1 \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--certificate $@

$(METTAZERO_EXP_COMPILATION_CERTIFICATE_V1): \
		$(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_SEMANTIC_RUNNER_V1) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile exp \
		--header $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EXP_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_exp_language_v1 \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--certificate $@

$(METTAZERO_EMIT_COMPILATION_CERTIFICATE_V1): \
		$(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_REVISIONED_EMIT_V1) \
		$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile emit \
		--header $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_EMIT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_emit_language_v1 \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--certificate $@

$(METTAZERO_INTERACT_COMPILATION_CERTIFICATE_V1): \
		$(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_LANGDEF_V1) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_OPEN_SUBSTITUTION_V1) \
		$(METTAZERO_REVISIONED_INTERACT_V1) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		$(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C)
	@python3 $(GSLT_COMPILATION_CERTIFICATE_GENERATOR_V1) \
		--manifest $(METTAZERO_LANGDEF_V1) \
		--source-root langdef \
		--profile interact \
		--header $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_H) \
		--source $(METTAZERO_INTERACT_GENERATED_LANGUAGE_V1_C) \
		--symbol cetta_zero_interact_language_v1 \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--schema tools/gslt2parse_schema_v1.py \
		--certificate $@

$(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_H) $(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_MANIFEST) \
		$(METTAZERO_QUOTE_MATCH_V1) \
		$(METTAZERO_QUERY_KERNEL_V1) \
		$(METTAZERO_CLOSED_BAG_OBSERVATION_V1) \
		$(METTAZERO_GROUND_LIBRARY_CANARY_V1_SOURCE)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METTAZERO_GROUND_LIBRARY_CANARY_V1_MANIFEST) \
		--source-root . \
		--header $(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_H) \
		--source $(METTAZERO_GROUND_LIBRARY_CANARY_V1_GENERATED_C) \
		--symbol cetta_zero_ground_library_v1 \
		--header-include tests/generated/metta_zero_ground_library_v1.generated.h

$(GSLT_COMPILED_CANARY_V1_GENERATED_H) $(GSLT_COMPILED_CANARY_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(GSLT_COMPILED_CANARY_V1_MANIFEST) \
		$(GSLT_COMPILED_CANARY_V1_SOURCE)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(GSLT_COMPILED_CANARY_V1_MANIFEST) \
		--header $(GSLT_COMPILED_CANARY_V1_GENERATED_H) \
		--source $(GSLT_COMPILED_CANARY_V1_GENERATED_C) \
		--symbol cetta_gslt_compiled_canary_v1 \
		--header-include tests/generated/gslt_compiled_canary_v1.generated.h

$(GSLT_PIPELINE_CANARY_V1_GENERATED_H) $(GSLT_PIPELINE_CANARY_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(GSLT_PIPELINE_CANARY_V1_MANIFEST) \
		$(GSLT_PIPELINE_CANARY_V1_SOURCE)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(GSLT_PIPELINE_CANARY_V1_MANIFEST) \
		--header $(GSLT_PIPELINE_CANARY_V1_GENERATED_H) \
		--source $(GSLT_PIPELINE_CANARY_V1_GENERATED_C) \
		--symbol cetta_gslt_pipeline_canary_v1 \
		--header-include tests/generated/gslt_pipeline_canary_v1.generated.h

$(GSLT_PROVIDER_CANARY_V1_GENERATED_H) $(GSLT_PROVIDER_CANARY_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(GSLT_PROVIDER_CANARY_V1_MANIFEST) \
		$(GSLT_PROVIDER_CANARY_V1_SOURCE)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(GSLT_PROVIDER_CANARY_V1_MANIFEST) \
		--header $(GSLT_PROVIDER_CANARY_V1_GENERATED_H) \
		--source $(GSLT_PROVIDER_CANARY_V1_GENERATED_C) \
		--symbol cetta_gslt_provider_canary_v1 \
		--header-include tests/generated/gslt_provider_canary_v1.generated.h

$(PROOF_TRACE_COMPILED_CANARY_V1_GENERATED_H) $(PROOF_TRACE_COMPILED_CANARY_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(PROOF_TRACE_COMPILED_CANARY_V1_MANIFEST) \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES) \
		$(PROOF_GSLT_TRACE_INPUT_CANARY_V1) \
		$(PROOF_GSLT_TRACE_COMPRESSED_COMPOSITION_CANARY_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(PROOF_TRACE_COMPILED_CANARY_V1_MANIFEST) \
		--source-root . \
		--header $(PROOF_TRACE_COMPILED_CANARY_V1_GENERATED_H) \
		--source $(PROOF_TRACE_COMPILED_CANARY_V1_GENERATED_C) \
		--symbol cetta_proof_trace_compiled_canary_v1 \
		--header-include tests/generated/proof_trace_compiled_canary_v1.generated.h

$(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_H) $(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST) \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES) \
		$(PROOF_GSLT_TRACE_SERVICE_V1)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_TRACE_LANGUAGE_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_trace_v1 \
		--header-include langdef/metamath/generated/proof_trace_language_v1.generated.h

$(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_H) $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_C) &: \
		$(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1) \
		$(METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST) \
		$(METAMATH_PROOF_TRACE_NATIVE_PROGRAM_V1_SOURCES) \
		$(PROOF_GSLT_TRACE_SERVICE_V1)
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1) \
		--language-manifest $(METAMATH_PROOF_TRACE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_TRACE_PROVIDER_CATALOG_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_trace_provider_catalog_v1 \
		--header-include langdef/metamath/generated/proof_trace_provider_catalog_v1.generated.h

$(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_H) $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_C) &: \
		$(GSLT_LANGUAGE_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST) \
		$(METAMATH_PROOF_MACHINE_COMPILED_V1_SOURCES)
	@python3 $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_machine_v1 \
		--header-include langdef/metamath/generated/proof_machine_language_v1.generated.h

$(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_H) $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_C) &: \
		$(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1) \
		$(METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST) \
		$(METAMATH_PROOF_MACHINE_COMPILED_V1_SOURCES)
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1) \
		--language-manifest $(METAMATH_PROOF_MACHINE_LANGUAGE_V1_MANIFEST) \
		--source-root . \
		--header $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_H) \
		--source $(METAMATH_PROOF_MACHINE_PROVIDER_CATALOG_V1_GENERATED_C) \
		--symbol cetta_metamath_proof_machine_provider_catalog_v1 \
		--header-include langdef/metamath/generated/proof_machine_provider_catalog_v1.generated.h

$(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_H) $(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C) &: \
		$(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		tools/gslt2parse_schema_v1.py \
		$(GSLT_PROVIDER_CANARY_CATALOG_V1) \
		$(GSLT_PROVIDER_CANARY_V1_MANIFEST) \
		$(GSLT_PROVIDER_CANARY_V1_SOURCE)
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(GSLT_PROVIDER_CANARY_CATALOG_V1) \
		--language-manifest $(GSLT_PROVIDER_CANARY_V1_MANIFEST) \
		--header $(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_H) \
		--source $(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C) \
		--symbol cetta_gslt_provider_canary_catalog_v1 \
		--header-include tests/generated/gslt_provider_canary_catalog_v1.generated.h

.PHONY: test-gslt-provider-generation-v1
test-gslt-provider-generation-v1: \
		$(GSLT_PROVIDER_CANARY_V1_GENERATED_H) \
		$(GSLT_PROVIDER_CANARY_V1_GENERATED_C) \
		$(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_H) \
		$(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C)
	@python3 tools/test_gslt_language_generation_v1.py \
		--generator $(GSLT_LANGUAGE_GENERATOR_V1) \
		--manifest $(GSLT_PROVIDER_CANARY_V1_MANIFEST) \
		--header $(GSLT_PROVIDER_CANARY_V1_GENERATED_H) \
		--source $(GSLT_PROVIDER_CANARY_V1_GENERATED_C) \
		--symbol cetta_gslt_provider_canary_v1 \
		--header-include tests/generated/gslt_provider_canary_v1.generated.h
	@python3 $(GSLT_PROVIDER_CATALOG_GENERATION_TEST_V1) \
		--generator $(GSLT_PROVIDER_CATALOG_GENERATOR_V1) \
		--catalog $(GSLT_PROVIDER_CANARY_CATALOG_V1) \
		--language-manifest $(GSLT_PROVIDER_CANARY_V1_MANIFEST) \
		--header $(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_H) \
		--source $(GSLT_PROVIDER_CANARY_CATALOG_GENERATED_C) \
		--symbol cetta_gslt_provider_canary_catalog_v1 \
		--header-include tests/generated/gslt_provider_canary_catalog_v1.generated.h

.PHONY: test-subzero-free-bag-v1 test-gslt-horn-runtime \
	test-gslt-language-runtime test-gslt-provider-runtime \
	test-gslt-abt-provider-v1 \
	test-gslt-provider-generation-v1 test-subzero-cli-v1 \
	test-gslt-language-generation-v1 test-subzero-rule-mutations-v1 \
	test-subzero-realization-triangle-v1 test-subzero \
	test-mettazero-cli-v1 test-mettazero-generation-v1 \
	test-mettazero-emit-cli-v1 \
	test-mettazero-emit-semantics-v1 \
	test-mettazero-emit-rule-mutations-v1 \
	test-mettazero-interact-semantics-v1 \
	test-mettazero-interact-rule-mutations-v1 \
	test-mettazero-compilation-certificate-v1 \
	test-mettazero-realization-triangle-v1 \
	test-mettazero-rule-mutations-v1 test-mettazero \
	test-gslt-il-generation-v1 test-gslt-il-cli-v1 \
	test-gslt-il-semantics-v1 test-gslt-il-rule-mutations-v1 test-gslt-il \
	test-zerouv-generation-v1 test-zerouv-cli-v1 \
	test-zerouv-semantics-v1 test-zerouv-rule-mutations-v1 test-zerouv \
	test-metta-interact-generation-v1 test-metta-interact-cli-v1 \
	test-metta-interact-semantics-v1 \
	test-metta-interact-rule-mutations-v1 test-metta-interact

$(GSLT2PARSE_CHART_V1_NATIVE_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_chart_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.h \
		src/native_sha256.c src/native_sha256.h
	@mkdir -p runtime
	$(CC) $(CFLAGS) -Isrc -I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_chart_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_gslt_v1.c \
		src/native_sha256.c

test-gslt2parse-c-horn-v1-native: $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@python3 tools/test_finite_horn_chart_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN)

test-gslt2parse-c-horn-v1-differential: test-gslt2parse-c-horn-v1-native
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_action_bytecode_compiler_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations"
	@swipl -q -f \
		"$(GSLT2PARSE_PETTA_ROOT)/experiments/gslt2parse_foundation/test_finite_horn_ground_terms.pl" \
		-- "$(CURDIR)/experiments/gslt2parse_foundation/presentations"

test-gslt2parse-parser-action-bytecode-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_ACTION_BYTECODE_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_action_bytecode_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--stream-binary $(PARSER_ACTION_BYTECODE_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-lookahead-semantics-v1: $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_lookahead_semantics_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guard-compiler-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_compiler_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guard-regular-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_regular_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-lr1-v1:
	@python3 tools/test_parser_pack_lr1_v1.py

test-gslt2parse-parser-pack-lexical-plan-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_lexical_plan_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guarded-lexical-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guard-plan-prime-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_plan_prime_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--profile prime

test-gslt2parse-parser-pack-guard-plan-he-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_plan_prime_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--profile he

test-gslt2parse-he-parser-authority-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_parser_authority_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)"

test-gslt2parse-he-unicode-residual-dfa-v1:
	@python3 tools/test_he_unicode_residual_dfa_v1.py

test-gslt2parse-he-string-slr-specialization-v1: \
		$(PARSER_PACK_SLR_SUMMARY_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_string_slr_specialization_v1.py \
		--binary $(PARSER_PACK_SLR_SUMMARY_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-he-reader-escape-differential-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_escape_differential_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--cetta-root "$(CURDIR)" \
		--mettapedia-root "$${METTAPEDIA_ROOT:-../../Mettapedia}"

test-gslt2parse-he-reader-source-faithfulness-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_source_faithfulness_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-he-reader-source-correspondence-v1: \
		test-gslt2parse-he-unicode-residual-dfa-v1 \
		test-gslt2parse-he-string-slr-specialization-v1 \
		test-gslt2parse-he-reader-escape-differential-v1 \
		test-gslt2parse-he-reader-source-faithfulness-v1
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_source_correspondence_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--cetta-root "$(CURDIR)" \
		--mettapedia-root "$${METTAPEDIA_ROOT:-../../Mettapedia}"

test-gslt2parse-rho-abstract-syntax-v1:
	@mettapedia_root="$${METTAPEDIA_ROOT:-../../Mettapedia/lean/mettapedia}"; \
	python3 tools/test_rho_abstract_syntax_v1.py \
		--mettapedia-root "$$mettapedia_root"

test-gslt2parse-rhocalc-reader-authority-v1: $(BIN)
	@python3 tools/test_rhocalc_reader_authority_v1.py \
		--binary "$(CETTA_SCRIPT_BIN)"

test-gslt2parse-rhocalc-parser-pack-v1: \
		$(BIN) \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_rhocalc_parser_pack_v1.py \
		--cetta-binary "$(CETTA_SCRIPT_BIN)" \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-rho-surface-convergence-v1: \
		$(BIN) \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN) \
		$(GSLT2PARSE_TERM_PROJECTION_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_rhocalc_surface_convergence_v1.py \
		--cetta-binary "$(CETTA_SCRIPT_BIN)" \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--projection-binary $(GSLT2PARSE_TERM_PROJECTION_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-stable-parser-parse-only-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_stable_parser_parse_only_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-he-gslt-parse-only-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_gslt_parse_only_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN) \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

bench-gslt2parse-he-gslt-parse-only-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_gslt_parse_only_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN) \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--benchmark

bench-gslt2parse-stable-parser-parse-only-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_stable_parser_parse_only_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--benchmark

test-gslt2parse-he-reader-guard-exec-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_guard_exec_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)"

test-gslt2parse-he-reader-guarded-lexical-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--scalar-exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		$(if $(strip $(GSLT2PARSE_HE_GENERATED_C_OUTPUT)),--generated-c-output "$(GSLT2PARSE_HE_GENERATED_C_OUTPUT)",)

.PHONY: refresh-he-compiled-reader-projection-v1 \
	test-he-compiled-reader-projection-generated-v1 \
	refresh-he-compiled-reader-cursor-v1 \
	test-he-compiled-reader-cursor-generated-v1 \
	refresh-he-compiled-reader-direct-v1 \
	test-he-compiled-reader-direct-generated-v1 \
	test-gslt-direct-reader-compiler-v1 \
	test-he-compiled-reader-generation-v1 \
	refresh-petta-compiled-reader-direct-v1 \
	test-petta-compiled-reader-direct-generated-v1 \
	refresh-prime-compiled-reader-direct-v1 \
	test-prime-compiled-reader-direct-generated-v1 \
	test-gslt-prefix-reader-compiler-v1

refresh-he-compiled-reader-projection-v1:
	@python3 tools/generate_compiled_atom_projection_v1.py \
		--profile cetta-he-v1 \
		--prefix cetta_he_projection_v1 \
		--projection "$(GSLT2PARSE_HE_PROJECTION_SOURCE)" \
		--reader "$(GSLT2PARSE_HE_READER_SOURCE)" \
		--output "$(GSLT2PARSE_HE_PROJECTION_GENERATED_H)"

test-he-compiled-reader-projection-generated-v1:
	@mkdir -p "$(GSLT2PARSE_GENERATED_CHECK_DIR)"
	@candidate="$(GSLT2PARSE_GENERATED_CHECK_DIR)/cetta_he_projection_v1.generated.h"; \
	python3 tools/generate_compiled_atom_projection_v1.py \
		--profile cetta-he-v1 \
		--prefix cetta_he_projection_v1 \
		--projection "$(GSLT2PARSE_HE_PROJECTION_SOURCE)" \
		--reader "$(GSLT2PARSE_HE_READER_SOURCE)" \
		--output "$$candidate"; \
	if ! cmp -s "$$candidate" "$(GSLT2PARSE_HE_PROJECTION_GENERATED_H)"; then \
		echo "FAIL: checked-in HE host projection is not generated from its LanguageDefs"; \
		sha256sum "$(GSLT2PARSE_HE_PROJECTION_GENERATED_H)" "$$candidate"; \
		exit 1; \
	fi; \
	echo "PASS: checked-in HE host projection regenerates byte-for-byte"

refresh-he-compiled-reader-cursor-v1:
	@$(MAKE) --no-print-directory \
		GSLT2PARSE_HE_ROOT="$(GSLT2PARSE_HE_ROOT)" \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		GSLT2PARSE_HE_GENERATED_C_OUTPUT="$(GSLT2PARSE_HE_CURSOR_GENERATED_C)" \
		test-gslt2parse-he-reader-guarded-lexical-v1

test-he-compiled-reader-cursor-generated-v1:
	@mkdir -p "$(GSLT2PARSE_GENERATED_CHECK_DIR)"
	@candidate="$(GSLT2PARSE_GENERATED_CHECK_DIR)/he_reader_cursor_v1.generated.c"; \
	$(MAKE) --no-print-directory \
		GSLT2PARSE_HE_ROOT="$(GSLT2PARSE_HE_ROOT)" \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		GSLT2PARSE_HE_GENERATED_C_OUTPUT="$$candidate" \
		test-gslt2parse-he-reader-guarded-lexical-v1; \
	if ! cmp -s "$$candidate" "$(GSLT2PARSE_HE_CURSOR_GENERATED_C)"; then \
		echo "FAIL: checked-in HE cursor is not generated from its LanguageDef"; \
		sha256sum "$(GSLT2PARSE_HE_CURSOR_GENERATED_C)" "$$candidate"; \
		exit 1; \
	fi; \
	echo "PASS: checked-in HE cursor regenerates byte-for-byte"

refresh-he-compiled-reader-direct-v1:
	@python3 tools/compile_gslt_direct_reader_v1.py \
		--syntax "$(GSLT2PARSE_HE_READER_SOURCE)" \
		--classes "$(GSLT2PARSE_HE_SCALAR_SOURCE)" \
		--projection "$(GSLT2PARSE_HE_PROJECTION_SOURCE)" \
		--profile cetta-he-v1 \
		--c-prefix he_reader_direct_v1 \
		--output-c "$(GSLT2PARSE_HE_DIRECT_GENERATED_C)" \
		--output-h "$(GSLT2PARSE_HE_DIRECT_GENERATED_H)"

test-he-compiled-reader-direct-generated-v1:
	@mkdir -p "$(GSLT2PARSE_GENERATED_CHECK_DIR)"
	@candidate_c="$(GSLT2PARSE_GENERATED_CHECK_DIR)/he_reader_direct_v1.generated.c"; \
	candidate_h="$(GSLT2PARSE_GENERATED_CHECK_DIR)/he_reader_direct_v1.generated.h"; \
	python3 tools/compile_gslt_direct_reader_v1.py \
		--syntax "$(GSLT2PARSE_HE_READER_SOURCE)" \
		--classes "$(GSLT2PARSE_HE_SCALAR_SOURCE)" \
		--projection "$(GSLT2PARSE_HE_PROJECTION_SOURCE)" \
		--profile cetta-he-v1 \
		--c-prefix he_reader_direct_v1 \
		--output-c "$$candidate_c" \
		--output-h "$$candidate_h"; \
	if ! cmp -s "$$candidate_c" "$(GSLT2PARSE_HE_DIRECT_GENERATED_C)" || \
	   ! cmp -s "$$candidate_h" "$(GSLT2PARSE_HE_DIRECT_GENERATED_H)"; then \
		echo "FAIL: checked-in HE direct reader is not generated from its LanguageDefs"; \
		sha256sum "$(GSLT2PARSE_HE_DIRECT_GENERATED_C)" "$$candidate_c"; \
		sha256sum "$(GSLT2PARSE_HE_DIRECT_GENERATED_H)" "$$candidate_h"; \
		exit 1; \
	fi; \
	echo "PASS: checked-in HE direct reader regenerates byte-for-byte"

test-gslt-direct-reader-compiler-v1: $(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@python3 tools/test_gslt_direct_reader_compiler_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN)

test-he-compiled-reader-generation-v1: \
	test-he-compiled-reader-projection-generated-v1 \
	test-he-compiled-reader-cursor-generated-v1 \
	test-he-compiled-reader-direct-generated-v1 \
	test-gslt-direct-reader-compiler-v1

refresh-petta-compiled-reader-direct-v1:
	@python3 tools/compile_gslt_petta_direct_reader_v1.py \
		--splitter-syntax "$(GSLT2PARSE_PETTA_SPLITTER_SOURCE)" \
		--splitter-classes "$(GSLT2PARSE_PETTA_SPLITTER_SCALAR_SOURCE)" \
		--form-syntax "$(GSLT2PARSE_PETTA_FORM_SOURCE)" \
		--form-classes "$(GSLT2PARSE_PETTA_FORM_SCALAR_SOURCE)" \
		--projection "$(GSLT2PARSE_PETTA_PROJECTION_SOURCE)" \
		--profile cetta-petta-v1 \
		--c-prefix petta_reader_direct_v1 \
		--output-c "$(GSLT2PARSE_PETTA_DIRECT_GENERATED_C)" \
		--output-h "$(GSLT2PARSE_PETTA_DIRECT_GENERATED_H)"

test-petta-compiled-reader-direct-generated-v1:
	@mkdir -p "$(GSLT2PARSE_GENERATED_CHECK_DIR)"
	@candidate_c="$(GSLT2PARSE_GENERATED_CHECK_DIR)/petta_reader_direct_v1.generated.c"; \
	candidate_h="$(GSLT2PARSE_GENERATED_CHECK_DIR)/petta_reader_direct_v1.generated.h"; \
	python3 tools/compile_gslt_petta_direct_reader_v1.py \
		--splitter-syntax "$(GSLT2PARSE_PETTA_SPLITTER_SOURCE)" \
		--splitter-classes "$(GSLT2PARSE_PETTA_SPLITTER_SCALAR_SOURCE)" \
		--form-syntax "$(GSLT2PARSE_PETTA_FORM_SOURCE)" \
		--form-classes "$(GSLT2PARSE_PETTA_FORM_SCALAR_SOURCE)" \
		--projection "$(GSLT2PARSE_PETTA_PROJECTION_SOURCE)" \
		--profile cetta-petta-v1 \
		--c-prefix petta_reader_direct_v1 \
		--output-c "$$candidate_c" \
		--output-h "$$candidate_h"; \
	if ! cmp -s "$$candidate_c" "$(GSLT2PARSE_PETTA_DIRECT_GENERATED_C)" || \
	   ! cmp -s "$$candidate_h" "$(GSLT2PARSE_PETTA_DIRECT_GENERATED_H)"; then \
		echo "FAIL: checked-in PeTTa direct reader is not generated from its composed LanguageDefs"; \
		sha256sum "$(GSLT2PARSE_PETTA_DIRECT_GENERATED_C)" "$$candidate_c"; \
		sha256sum "$(GSLT2PARSE_PETTA_DIRECT_GENERATED_H)" "$$candidate_h"; \
		exit 1; \
	fi; \
	echo "PASS: checked-in PeTTa direct reader regenerates byte-for-byte"

refresh-prime-compiled-reader-direct-v1:
	@python3 tools/compile_gslt_prefix_reader_v1.py \
		--syntax "$(GSLT2PARSE_PRIME_READER_SOURCE)" \
		--classes "$(GSLT2PARSE_PRIME_SCALAR_SOURCE)" \
		--projection "$(GSLT2PARSE_PRIME_PROJECTION_SOURCE)" \
		--profile cetta-prime-v1 \
		--c-prefix prime_reader_direct_v1 \
		--output-c "$(GSLT2PARSE_PRIME_DIRECT_GENERATED_C)" \
		--output-h "$(GSLT2PARSE_PRIME_DIRECT_GENERATED_H)"

test-prime-compiled-reader-direct-generated-v1:
	@mkdir -p "$(GSLT2PARSE_GENERATED_CHECK_DIR)"
	@candidate_c="$(GSLT2PARSE_GENERATED_CHECK_DIR)/prime_reader_direct_v1.generated.c"; \
	candidate_h="$(GSLT2PARSE_GENERATED_CHECK_DIR)/prime_reader_direct_v1.generated.h"; \
	python3 tools/compile_gslt_prefix_reader_v1.py \
		--syntax "$(GSLT2PARSE_PRIME_READER_SOURCE)" \
		--classes "$(GSLT2PARSE_PRIME_SCALAR_SOURCE)" \
		--projection "$(GSLT2PARSE_PRIME_PROJECTION_SOURCE)" \
		--profile cetta-prime-v1 \
		--c-prefix prime_reader_direct_v1 \
		--output-c "$$candidate_c" \
		--output-h "$$candidate_h"; \
	if ! cmp -s "$$candidate_c" "$(GSLT2PARSE_PRIME_DIRECT_GENERATED_C)" || \
	   ! cmp -s "$$candidate_h" "$(GSLT2PARSE_PRIME_DIRECT_GENERATED_H)"; then \
		echo "FAIL: checked-in Prime reader is not generated from its LanguageDefs"; \
		sha256sum "$(GSLT2PARSE_PRIME_DIRECT_GENERATED_C)" "$$candidate_c"; \
		sha256sum "$(GSLT2PARSE_PRIME_DIRECT_GENERATED_H)" "$$candidate_h"; \
		exit 1; \
	fi; \
	echo "PASS: checked-in Prime direct reader regenerates byte-for-byte"

test-gslt-prefix-reader-compiler-v1:
	@python3 tools/test_gslt_prefix_reader_compiler_v1.py

test-gslt2parse-he-document-pipeline-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(HE_DOCUMENT_PIPELINE_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_document_pipeline_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(HE_DOCUMENT_PIPELINE_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)"

PREPARED_FINAL_FOREST_PROBE_ITERATIONS ?= 3

bench-gslt2parse-prepared-final-forest-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--probe-final-forest-growth \
		--probe-iterations $(PREPARED_FINAL_FOREST_PROBE_ITERATIONS)
	@python3 tools/test_he_reader_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--scalar-exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--probe-final-forest-growth \
		--probe-iterations $(PREPARED_FINAL_FOREST_PROBE_ITERATIONS)

test-gslt2parse-petta-form-guard-exec-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_petta_form_guard_exec_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-petta-document-splitter-v1: \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_petta_document_splitter_v1.py \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

ifeq ($(ENABLE_PIC),1)
test-gslt2parse-petta-document-pipeline-v1-body: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PETTA_DOCUMENT_PIPELINE_V1_BIN) \
		$(PETTA_DOCUMENT_PIPELINE_V1_LIB)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@$(MAKE) --no-print-directory ENABLE_SANITIZERS=0 ENABLE_PIC=1 \
		runtime/libcetta_parser_pack_native_v1-$(BUILD_CANON).pic.so \
		runtime/libcetta_petta_document_pipeline_v1-$(BUILD_CANON).pic.so
	@$(MAKE) -C \
		"$(GSLT2PARSE_PETTA_ROOT)/experiments/gslt2parse_foundation/native" \
		all CETTA_ROOT="$(CURDIR)" CETTA_OBJ_TAG="$(BUILD_CANON).pic"
	@$(GSLT2PARSE_SHARED_ASAN_ENV) python3 \
		tools/test_petta_document_pipeline_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(PETTA_DOCUMENT_PIPELINE_V1_BIN) \
		--pipeline-library $(PETTA_DOCUMENT_PIPELINE_V1_LIB) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		$(GSLT2PARSE_SHARED_ASAN_ARGS)
else
test-gslt2parse-petta-document-pipeline-v1-body:
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@$(MAKE) --no-print-directory ENABLE_PIC=1 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-document-pipeline-v1-body
endif

test-gslt2parse-petta-ffi-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=1 ENABLE_SANITIZERS=0 ENABLE_PIC=0 \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_RECEIPT_PRIMARY_INDEX=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_NEED_CLOSURE_CAPTURE=0 ENABLE_PRIME_EVAL_STACK=0 \
		ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-ffi-v1-body

test-gslt2parse-petta-document-pipeline-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=1 ENABLE_SANITIZERS=0 ENABLE_PIC=0 \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_RECEIPT_PRIMARY_INDEX=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_NEED_CLOSURE_CAPTURE=0 ENABLE_PRIME_EVAL_STACK=0 \
		ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-document-pipeline-v1-body

test-gslt2parse-petta-ffi-v1-body:
	@$(MAKE) --no-print-directory ENABLE_PIC=1 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-parser-pack-native-petta-v1
	@$(MAKE) --no-print-directory \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-document-pipeline-v1-body

test-gslt2parse-petta-parser-authority-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_petta_parser_authority_v1.py \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

$(GSLT2PARSE_PARSER_PACK_ABI_V1_NATIVE_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_pack_abi_v1.c \
		src/symbol.c src/atom.c src/native_sha256.c src/native_sha256.h \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		src/symbol.c src/atom.c src/native_sha256.c $(LDFLAGS)

test-gslt2parse-parser-pack-abi-v1-native: \
		$(GSLT2PARSE_PARSER_PACK_ABI_V1_NATIVE_BIN)
	@$(GSLT2PARSE_PARSER_PACK_ABI_V1_NATIVE_BIN)
	@if rg -ni 'metamath|megalodon|tptp' \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h; then \
		echo 'guest-language name leaked into the generic ParserPack ABI'; \
		exit 1; \
	fi

$(GSLT2PARSE_TERM_PROJECTION_V1_NATIVE_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_term_projection_v1.c \
		src/symbol.c src/atom.c src/native_sha256.c src/native_sha256.h \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_term_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		src/symbol.c src/atom.c src/native_sha256.c $(LDFLAGS)

test-gslt2parse-parser-term-projection-v1-native: \
		$(GSLT2PARSE_TERM_PROJECTION_V1_NATIVE_BIN)
	@$(GSLT2PARSE_TERM_PROJECTION_V1_NATIVE_BIN)
	@if rg -ni 'metamath|megalodon|tptp|rhocalc|rholang|petta|prime' \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.h; then \
		echo 'guest-language name leaked into the generic term projector'; \
		exit 1; \
	fi

$(GSLT2PARSE_ATOM_PROJECTION_V1_NATIVE_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_atom_projection_v1.c \
		$(PARSER_ATOM_PROJECTION_V1_OBJ) \
		$(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) \
		$(PARSER_ATOM_PROJECTION_DOMAIN_V1_OBJ) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_atom_projection_v1.c \
		$(PARSER_ATOM_PROJECTION_V1_OBJ) \
		$(PARSER_ATOM_PROJECTION_EVENTS_V1_OBJ) \
		$(PARSER_ATOM_PROJECTION_DOMAIN_V1_OBJ) \
		$(FALLBACK_EVAL_TEST_LINK_OBJ) $(LDFLAGS)

test-gslt2parse-parser-atom-projection-v1-native: \
		$(GSLT2PARSE_ATOM_PROJECTION_V1_NATIVE_BIN) \
		tools/sexpr_atom_projection_plan_v1.py \
		tools/gslt2parse_schema_v1.py \
		experiments/gslt2parse_foundation/presentations/shared/sexpr_atom_projection_v1.metta \
		experiments/gslt2parse_foundation/presentations/languages/he_reader_v1.metta
	@set -o pipefail; \
		python3 tools/sexpr_atom_projection_plan_v1.py --profile he-v1 | \
		$(GSLT2PARSE_ATOM_PROJECTION_V1_NATIVE_BIN) --plan-stdin
	@if rg -ni 'metamath|megalodon|tptp|rhocalc|rholang|petta|prime' \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_atom_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_atom_projection_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_atom_projection_events_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_atom_projection_events_v1.h; then \
		echo 'guest-language name leaked into the generic Atom projector'; \
		exit 1; \
	fi

test-gslt2parse-parser-atom-projection-closure-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_BIN) \
		tools/test_parser_atom_projection_closure_v1.py \
		tools/sexpr_atom_projection_plan_v1.py \
		experiments/gslt2parse_foundation/presentations/compiler/semantic_text_span_compiler_v1.metta
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the paired PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_atom_projection_closure_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--closure-binary $(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-semantic-mask-span-compiler-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(SEMANTIC_MASK_NFA_V1_STREAM_BIN) \
		tools/test_semantic_mask_span_compiler_v1.py \
		experiments/gslt2parse_foundation/presentations/compiler/semantic_mask_span_compiler_v1.metta \
		experiments/gslt2parse_foundation/presentations/canaries/semantic_mask_v1.metta
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the paired PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_semantic_mask_span_compiler_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--stream-binary $(SEMANTIC_MASK_NFA_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

$(GSLT2PARSE_TERM_PROJECTION_V1_STREAM_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1_stream.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h \
		src/symbol.c src/atom.c src/native_sha256.c src/native_sha256.h \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1_stream.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_term_projection_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		src/symbol.c src/atom.c src/native_sha256.c $(LDFLAGS)

$(GSLT2PARSE_PARSER_PACK_ABI_V1_STREAM_BIN): \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_pack_abi_v1_stream.c \
		src/symbol.c src/atom.c src/native_sha256.c src/native_sha256.h \
		$(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR) -o $@ \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_pack_abi_v1_stream.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		src/symbol.c src/atom.c src/native_sha256.c $(LDFLAGS)

test-gslt2parse-parser-pack-abi-v1-matrix: \
		$(GSLT2PARSE_PARSER_PACK_ABI_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_abi_v1.py \
		--binary $(GSLT2PARSE_PARSER_PACK_ABI_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"
	@if rg -ni 'metamath|megalodon|tptp' \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_pack_abi_v1_stream.c; then \
		echo 'guest-language name leaked into the native ParserPack ABI path'; \
		exit 1; \
	fi

refresh-he-matrices:
	@python3 scripts/refresh_he_runtime_matrices.py
	@python3 -m json.tool specs/he_runtime_impl_matrix.json > /dev/null
	@python3 -m json.tool specs/he_runtime_3layer_matrix.json > /dev/null
	@echo "refreshed HE runtime parity matrices"

.PHONY: test-gslt2parse-schema-v1 test-gslt2parse-schema-v1-native test-gslt2parse-c-horn-v1-native test-gslt2parse-c-horn-v1-differential test-gslt2parse-parser-action-bytecode-v1 test-gslt2parse-lookahead-semantics-v1 test-gslt2parse-parser-pack-guard-compiler-v1 test-gslt2parse-parser-pack-guard-regular-v1 test-gslt2parse-parser-pack-lr1-v1 test-gslt2parse-parser-pack-guard-plan-he-v1 test-gslt2parse-parser-pack-guard-plan-prime-v1 test-gslt2parse-parser-pack-abi-v1-native test-gslt2parse-parser-pack-abi-v1-matrix test-gslt2parse-parser-term-projection-v1-native test-gslt2parse-parser-atom-projection-v1-native test-gslt2parse-parser-atom-projection-closure-v1 test-gslt2parse-semantic-mask-span-compiler-v1 test-gslt2parse-parser-pack-gll-v1-native test-gslt2parse-parser-pack-gll-v1-matrix test-gslt2parse-regular-span-dfa-v1-native test-gslt2parse-regular-span-dfa-v1-matrix test-term-universe-backend-add-abi bench-space-scale-ladder
.PHONY: list bench-index FORCE all core python mork main pathmap full profile clean bridge-setup doctor-bridge doctor-gmp test-bigint-no-gmp-fallback test-rational-no-gmp-fallback test test-light test-correctness test-heavy test-heavy-golden list-heavy-diagnostics probe-heavy-diagnostics test-correctness-all test-manifest test-manifest-check test-manifest-sync test-runtime-stats test-runtime-stats-lane test-runtime-stats-metta-suite test-backends test-he-contract-suite refresh-he-contract-tests refresh-he-compat-catalog test-he-compat-semantic-suite probe-he-compat-tier2 probe-he-compat-runnable-corpus test-mork-lane test-mork-lane-core test-mork-basic-pathmap-guard test-mork-runtime-stats-lane test-mork-runtime-stats-isolation test-closed-stream-fastpath test-closed-stream-runtime-stats test-parse-depth-guard test-stdlib-growth-memory-regression test-asan test-asan-main test-asan-mork test-pathmap-lane test-pathmap-lane-body test-pathmap-runtime-stats-lane test-pathmap-runtime-stats-lane-body test-mm2-mork-program-space test-mm2-exec-basic test-mm2-kiss-suite test-mm2-conformance-var-binding test-mm2-conformance-lean-suite test-mm2-sink-suite test-pathmap-bridge-v2 test-pathmap-long-string-regression test-pathmap-match-chain test-mork-lib-pathmap test-mork-open-act test-pretty-vars-flags test-pretty-namespaces-flags test-help-flags test-rhocalc test-lib-parse-oracles test-rhocalc-lib-parse-reference test-lib-parse-shared-cert test-lib-parse-slr-prepared test-lib-parse-gll-utf8-forest test-lib-parse-native-gparse test-lib-parse-generalized-native-integration test-lib-parse-generalized-cli test-lib-parse-generalized test-lib-parse-bounded test-rhocalc-runtime-stats test-variant-shape-roundtrip test-rhometta-payload-map-capacity-c test-space-term-universe-membership test-term-universe-store-abi test-pathmap-backend-primary-destructive-abi test-pathmap-backend-primary-replace-abi test-pathmap-typed-query-abi test-fallback-eval-session test-import-modes bench bench-light bench-correctness bench-performance-light bench-optional-bridge-light bench-capacity bench-heavy bench-prime-light bench-prime-heavy prepare-bio-eqtl-act bench-bio-eqtl-act-modes prepare-bio-1m-act bench-bio-1m-act-attach bench-bio-1m-act-modes test-duplicate-multiplicity-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends probe-d3-nodup probe-d3-nodup-backends probe-fc-native-memory bench-conj-backends bench-conj12-backends bench-dup-conj-backends bench-d4 bench-d4-nodup bench-d4-backends bench-d4-nodup-backends bench-rho-fanout bench-rho-comm-frontier bench-rho-comm-contention bench-rho-pipeline-forward bench-rho-route-synthesis bench-rho-demand-index bench-rho-indexed-demand bench-rho-route-policy bench-rho-certificate-quorum bench-compare-petta bench-mork-add-interface bench-mork-add-interface-timing bench-mork-bridge-add bench-mork-bridge-query bench-mork-bridge-scalar-cursor bench-mork-bridge-space-ops bench-answer-ref-demand bench-space-backend-matrix bench-space-transfer-matrix bench-ffi-friction-light bench-ffi-friction-basic bench-ffi-friction-stress bench-ffi-friction-heavy bench-closed-stream-fastpath bench-weird-audit tail-recursion-check compile-test refresh-he-matrices promote-runtime perf-list perf-show-baselines perf-capacity-tu perf-bench-tu perf-compare-tu probe-epoch-runtime-witness
.PHONY: refresh-he-native-contracts test-he-compat-catalog-guards test-step-rules test-he-prime-search-mutation test-he-prime-scheme-mutation test-prime test-prime-all test-prime-coverage test-prime-crossdialect test-prime-internal-graduality test-prime-practical test-prime-occurs-check-mutation test-prime-completion-mutation test-prime-delayed-ambiguity-mutation test-prime-variable-mutation test-prime-canonical-binder-mutation test-prime-abt-chain-mutation test-prime-abt-let-mutation test-prime-abt-sealed-mutation test-prime-applicability-capacity-mutation test-prime-type-capacity-mutation test-prime-budget-monotonicity test-prime-package-validation
.PHONY: list bench-index FORCE all core python mork main pathmap full profile clean bridge-setup doctor-bridge doctor-gmp test-bigint-no-gmp-fallback test-rational-no-gmp-fallback test test-light test-correctness test-heavy test-heavy-golden list-heavy-diagnostics probe-heavy-diagnostics test-correctness-all test-manifest test-manifest-check test-manifest-sync test-runtime-stats test-runtime-stats-lane test-runtime-stats-metta-suite test-backends test-he-contract-suite refresh-he-contract-tests refresh-he-compat-catalog test-he-compat-semantic-suite probe-he-compat-tier2 probe-he-compat-runnable-corpus test-mork-lane test-mork-lane-core test-mork-basic-pathmap-guard test-mork-runtime-stats-lane test-mork-runtime-stats-isolation test-closed-stream-fastpath test-closed-stream-runtime-stats test-parse-depth-guard test-stdlib-growth-memory-regression test-asan test-asan-main test-asan-mork test-pathmap-lane test-pathmap-lane-body test-pathmap-runtime-stats-lane test-pathmap-runtime-stats-lane-body test-mm2-mork-program-space test-mm2-exec-basic test-mm2-kiss-suite test-mm2-conformance-var-binding test-mm2-conformance-lean-suite test-mm2-sink-suite test-pathmap-bridge-v2 test-pathmap-long-string-regression test-pathmap-match-chain test-mork-lib-pathmap test-mork-open-act test-pretty-vars-flags test-pretty-namespaces-flags test-help-flags test-rhocalc test-lib-parse-oracles test-rhocalc-lib-parse-reference test-lib-parse-shared-cert test-lib-parse-native-gparse test-lib-parse-generalized-native-integration test-lib-parse-generalized-cli test-lib-parse-generalized test-lib-parse-bounded test-rhocalc-runtime-stats test-variant-shape-roundtrip test-rhometta-payload-map-capacity-c test-space-term-universe-membership test-term-universe-store-abi test-term-universe-backend-add-abi test-pathmap-backend-primary-destructive-abi test-pathmap-backend-primary-replace-abi test-pathmap-typed-query-abi test-fallback-eval-session test-import-modes bench bench-light bench-correctness bench-performance-light bench-optional-bridge-light bench-capacity bench-heavy bench-prime-light bench-prime-heavy prepare-bio-eqtl-act bench-bio-eqtl-act-modes prepare-bio-1m-act bench-bio-1m-act-attach bench-bio-1m-act-modes test-duplicate-multiplicity-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends probe-d3-nodup probe-d3-nodup-backends probe-fc-native-memory bench-conj-backends bench-conj12-backends bench-dup-conj-backends bench-d4 bench-d4-nodup bench-d4-backends bench-d4-nodup-backends bench-rho-fanout bench-rho-comm-frontier bench-rho-comm-contention bench-rho-pipeline-forward bench-rho-route-synthesis bench-rho-demand-index bench-rho-indexed-demand bench-rho-route-policy bench-rho-certificate-quorum bench-compare-petta bench-mork-add-interface bench-mork-add-interface-timing bench-mork-bridge-add bench-mork-bridge-query bench-mork-bridge-scalar-cursor bench-mork-bridge-space-ops bench-answer-ref-demand bench-space-backend-matrix bench-space-transfer-matrix bench-space-scale-ladder bench-ffi-friction-light bench-ffi-friction-basic bench-ffi-friction-stress bench-ffi-friction-heavy bench-closed-stream-fastpath bench-weird-audit tail-recursion-check compile-test refresh-he-matrices promote-runtime perf-list perf-show-baselines perf-capacity-tu perf-bench-tu perf-compare-tu probe-epoch-runtime-witness
.PHONY: refresh-he-native-contracts test-he-compat-catalog-guards test-step-rules test-he-prime-search-mutation test-he-prime-scheme-mutation test-prime test-prime-all test-prime-coverage test-prime-crossdialect test-prime-internal-graduality test-prime-practical test-runtime-named-var test-prime-bare-dollar-parser test-prime-bare-dollar-gslt test-prime-bare-dollar-reference test-prime-bare-dollar-evaluator test-prime-bare-dollar-mutations test-prime-bare-dollar-tournament test-prime-need-algebra test-prime-need-he-noninterference test-prime-need-correspondence probe-prime-need-observation-boundary probe-prime-equation-call-sharing-tournament test-prime-equation-call-sharing-tournament test-prime-equation-call-constitution test-prime-need-gc-lifetime test-prime-need-boundaries test-prime-suspension-rights test-prime-contexts test-prime-context-tutorial test-prime-rewrite-frontier-tutorial test-prime-need-effect-isolation test-prime-need-equation-choice-sharing test-prime-need-equation-choice-sharing-body test-prime-need-equation-choice-sharing-mutation test-prime-need-equation-choice-sharing-mutation-body test-prime-evaluation-strategy-contrast test-prime-need-mutations test-prime-universal-name-compile test-prime-universal-name-surface test-prime-universal-name-mutation test-prime-syntax-mutation test-prime-universal-name-metadata test-prime-universal-name-metadata-mutation test-prime-universal-name-syntax-gslt test-registry-resolver test-prime-universal-name-resolver bench-prime-universal-name-resolver test-prime-occurs-check-mutation test-prime-completion-mutation test-prime-delayed-ambiguity-mutation test-prime-variable-mutation test-prime-canonical-binder-mutation test-prime-abt-chain-mutation test-prime-abt-let-mutation test-prime-abt-sealed-mutation test-prime-applicability-capacity-mutation test-prime-type-capacity-mutation test-prime-budget-monotonicity test-prime-package-validation
.PHONY: test-rhocalc-cost-differential
.PHONY: test-atom-deep-copy-iterative test-name-key test-abt test-abt-mm2-boundary test-rhocalc-abt-substitution test-abt-mutations test-abt-default-signatures test-abt-differential test-abt-integration-ledger test-abt-scope-construction-candidates bench-abt bench-lib-parse-inference-native
.PHONY: test-rhometta-macro-audit test-eval-gc-adversarial test-eval-gc-survivor-reset test-eval-gc-asan-selected test-eval-gc-asan-selected-body test-eval-gc-asan-full-differential test-eval-gc-asan-full-differential-body test-tsan test-tsan-main test-tsan-mork bench-rho-rhometta-deduction-farm bench-rho-hot-frontier bench-rho-hot-successors bench-rho-threaded bench-rho-threaded-heavy bench-rho-threaded-corpus bench-rho-threaded-generated bench-rho-threaded-generated-runtime-stats
.PHONY: test-backends-lanes test-manifest-strict test-mork-lane-core-body test-mork-add-atoms-runtime-stats-body test-mork-bridge-contextual-exact-rows test-mork-cursor-byte-buffer-count-abi test-mork-cursor-expr-row-stream-abi test-mork-query-row-stream-abi probe-core-lane probe-pathmap-lane probe-pathmap-lane-body test-list-lanes test-syn-lanes bench-list
.PHONY: test-lib-parse-abt-bridge
.PHONY: test-lib-parse-glr-utf8-forest
.PHONY: test-gslt2parse-parser-pack-glr-v1-native test-gslt2parse-parser-pack-glr-v1-matrix test-gslt2parse-parser-pack-wide-scale-v1 test-gslt2parse-parser-pack-lexical-v1-native test-gslt2parse-parser-pack-lexical-v1-matrix test-gslt2parse-generic-engine-purity-v1 test-gslt2parse-c-production-v1 test-gslt2parse-c-production-v1-body
.PHONY: test-gslt2parse-parser-pack-native-api-v1-matrix test-gslt2parse-parser-pack-native-petta-v1 test-gslt2parse-parser-pack-native-petta-v1-body test-gslt2parse-he-parser-authority-v1 test-gslt2parse-he-unicode-residual-dfa-v1 test-gslt2parse-he-string-slr-specialization-v1 test-gslt2parse-he-reader-escape-differential-v1 test-gslt2parse-he-reader-source-faithfulness-v1 test-gslt2parse-he-reader-source-correspondence-v1 test-gslt2parse-rho-abstract-syntax-v1 test-gslt2parse-rhocalc-reader-authority-v1 test-gslt2parse-rhocalc-parser-pack-v1 test-gslt2parse-he-reader-guard-exec-v1 test-gslt2parse-he-reader-guarded-lexical-v1 test-gslt2parse-he-document-pipeline-v1 test-gslt2parse-he-gslt-parse-only-v1 bench-gslt2parse-he-gslt-parse-only-v1 test-gslt2parse-petta-form-guard-exec-v1 test-gslt2parse-petta-document-splitter-v1 test-gslt2parse-petta-document-pipeline-v1 test-gslt2parse-petta-document-pipeline-v1-body test-gslt2parse-petta-ffi-v1 test-gslt2parse-petta-ffi-v1-body test-gslt2parse-petta-parser-authority-v1 test-gslt2parse-stable-parser-parse-only-v1 bench-gslt2parse-stable-parser-parse-only-v1 bench-gslt2parse-prepared-final-forest-v1
.PHONY: test-gslt2parse-parser-pack-lexical-plan-v1 test-gslt2parse-parser-pack-guarded-lexical-v1
.PHONY: test-gslt2parse-rho-surface-convergence-v1
.PHONY: list bench-index FORCE all core python mork main pathmap full profile clean bridge-setup doctor-bridge doctor-gmp test-bigint-no-gmp-fallback test-rational-no-gmp-fallback test test-light test-correctness test-heavy test-heavy-golden list-heavy-diagnostics probe-heavy-diagnostics test-correctness-all test-manifest test-manifest-check test-manifest-sync test-runtime-stats test-runtime-stats-lane test-runtime-stats-metta-suite test-backends test-he-contract-suite refresh-he-contract-tests refresh-he-compat-catalog test-he-compat-semantic-suite probe-he-compat-tier2 probe-he-compat-runnable-corpus test-mork-lane test-mork-lane-core test-mork-basic-pathmap-guard test-mork-runtime-stats-lane test-mork-runtime-stats-isolation test-closed-stream-fastpath test-closed-stream-runtime-stats test-parse-depth-guard test-stdlib-growth-memory-regression test-asan test-asan-main test-asan-mork test-pathmap-lane test-pathmap-lane-body test-pathmap-runtime-stats-lane test-pathmap-runtime-stats-lane-body test-mm2-mork-program-space test-mm2-exec-basic test-mm2-kiss-suite test-mm2-conformance-var-binding test-mm2-var-scope-across-exprs test-mm2-conformance-lean-suite test-mm2-sink-suite test-pathmap-bridge-v2 test-pathmap-long-string-regression test-pathmap-match-chain test-mork-lib-pathmap test-mork-open-act test-pretty-vars-flags test-pretty-namespaces-flags test-help-flags test-rhocalc test-rhocalc-cost-differential test-lib-parse-oracles test-rhocalc-lib-parse-reference test-lib-parse-shared-cert test-lib-parse-native-gparse test-lib-parse-generalized-native-integration test-lib-parse-generalized-cli test-lib-parse-generalized test-lib-parse-bounded test-rhocalc-runtime-stats test-variant-shape-roundtrip test-rhometta-payload-map-capacity-c test-space-term-universe-membership test-term-universe-store-abi test-term-universe-backend-add-abi test-pathmap-backend-primary-destructive-abi test-pathmap-backend-primary-replace-abi test-pathmap-typed-query-abi test-fallback-eval-session test-import-modes bench bench-light bench-correctness bench-performance-light bench-optional-bridge-light bench-capacity bench-heavy prepare-bio-eqtl-act bench-bio-eqtl-act-modes prepare-bio-1m-act bench-bio-1m-act-attach bench-bio-1m-act-modes test-duplicate-multiplicity-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends probe-d3-nodup probe-d3-nodup-backends probe-fc-native-memory bench-conj-backends bench-conj12-backends bench-dup-conj-backends bench-d4 bench-d4-nodup bench-d4-backends bench-d4-nodup-backends bench-rho-fanout bench-rho-comm-frontier bench-rho-comm-contention bench-rho-pipeline-forward bench-rho-route-synthesis bench-rho-demand-index bench-rho-indexed-demand bench-rho-route-policy bench-rho-certificate-quorum bench-compare-petta bench-mork-add-interface bench-mork-add-interface-timing bench-mork-bridge-add bench-mork-bridge-query bench-mork-bridge-scalar-cursor bench-mork-bridge-space-ops bench-answer-ref-demand bench-space-backend-matrix bench-space-transfer-matrix bench-space-scale-ladder bench-ffi-friction-light bench-ffi-friction-basic bench-ffi-friction-stress bench-ffi-friction-heavy bench-closed-stream-fastpath bench-weird-audit tail-recursion-check compile-test refresh-he-matrices promote-runtime perf-list perf-show-baselines perf-capacity-tu perf-bench-tu perf-compare-tu probe-epoch-runtime-witness
.PHONY: refresh-he-native-contracts test-he-compat-catalog-guards test-step-rules
.PHONY: test-rhocalc-cost-parallel-stress test-rhocalc-canonical-selector-differential
.PHONY: test-atom-deep-copy-iterative bench-lib-parse-inference-native
.PHONY: test-rhometta-macro-audit test-eval-gc-adversarial test-eval-gc-survivor-reset test-eval-gc-asan-selected test-eval-gc-asan-selected-body test-eval-gc-asan-full-differential test-eval-gc-asan-full-differential-body test-tsan test-tsan-main test-tsan-mork test-rhocalc-cost-differential-required test-rhocalc-cost-observer-transparency test-rhocalc-cost-commit-audit test-rhocalc-cost-commit-audit-asan test-rhocalc-cost-commit-audit-tsan test-rhocalc-cost-commit-audit-body bench-rho-rhometta-deduction-farm bench-rho-hot-frontier bench-rho-hot-successors bench-rho-threaded bench-rho-threaded-heavy bench-rho-cost-threaded bench-rho-cost-threaded-heavy bench-rho-threaded-corpus bench-rho-threaded-generated bench-rho-threaded-generated-runtime-stats perf-bench-rhocalc test-main-readiness-model main-readiness-space-ladders main-readiness-thresholds main-readiness-mutation-qualification main-readiness-rho-adaptive main-readiness-routine main-readiness-routine-authoritative main-readiness-exhaustive main-readiness-calibration-status main-readiness-calibrate main-readiness-cost-rho
.PHONY: probe-d4-nodup-capability-backends
.PHONY: test-backends-lanes test-manifest-strict test-mork-lane-core-body test-mork-add-atoms-runtime-stats-body test-mork-bridge-contextual-exact-rows test-mork-cursor-byte-buffer-count-abi test-mork-cursor-expr-row-stream-abi test-mork-query-row-stream-abi probe-core-lane probe-pathmap-lane probe-pathmap-lane-body
