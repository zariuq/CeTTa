SHELL = /bin/bash
CC = gcc
.DEFAULT_GOAL := all

# Build mode:
#   make                   -> BUILD=python      (default: Python foreign-module support enabled)
#   make BUILD=core        -> bare CeTTa (no Python, no static MORK bridge)
#   make BUILD=python      -> CeTTa + Python (no static MORK bridge)
#   make BUILD=mork        -> mainline-safe lib_mork lane (no Python, no generic pathmap spaces)
#   make BUILD=main        -> BUILD=mork + Python
#   make BUILD=pathmap     -> modified-MORK lane with generic pathmap-backed spaces (no Python)
#   make BUILD=full        -> BUILD=pathmap + Python
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
ENABLE_RUNTIME_STATS ?= 0
ENABLE_RUNTIME_TIMING ?= 0
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
ENABLE_PATHMAP_SPACE := 1
endif
ifeq ($(BUILD_CANON),full)
ENABLE_PYTHON := 1
ENABLE_MORK_STATIC := 1
ENABLE_PATHMAP_SPACE := 1
endif

CETTA_REPO_DIR ?= $(CURDIR)
CETTA_RUST_DIR ?= $(abspath ./rust)
MORK_BRIDGE_DIR ?= $(CETTA_RUST_DIR)/target/release
MORK_BRIDGE_MANIFEST ?= $(CETTA_RUST_DIR)/cetta-space-bridge/Cargo.toml
MORK_BRIDGE_WORKDIR ?= $(CETTA_RUST_DIR)
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
MORK_BRIDGE_FEATURE_STATICLIB := runtime/bootstrap/libcetta_space_bridge.$(MORK_BRIDGE_FEATURE_TAG).a
MORK_BRIDGE_BUILD_STAMP := runtime/bootstrap/mork-bridge.$(MORK_BRIDGE_FEATURE_TAG).stamp
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
$(error BUILD=$(BUILD_CANON) requires Rust bridge dependencies. Missing: $(MORK_BRIDGE_MISSING_MANIFESTS). Expected sibling layout: <parent>/CeTTa plus <parent>/MORK and <parent>/PathMap)
endif
BRIDGE_DEPS += $(MORK_BRIDGE_BUILD_STAMP) $(MORK_BRIDGE_FEATURE_STATICLIB)
BRIDGE_CFLAGS += -DCETTA_MORK_BRIDGE_STATIC=1
BRIDGE_LDFLAGS += $(MORK_BRIDGE_FEATURE_STATICLIB) -lrt
MORK_BUILD_HAS_BRIDGE := 1
endif

$(MORK_BRIDGE_BUILD_STAMP): $(MORK_BRIDGE_SOURCE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@cd $(MORK_BRIDGE_WORKDIR) && \
	(ulimit -v 10485760 2>/dev/null || true) && \
	RUSTFLAGS='$(MORK_BRIDGE_RUSTFLAGS)' \
	cargo build -p cetta-space-bridge --release $(MORK_BRIDGE_CARGO_FEATURE_ARGS)
	@test -f "$(MORK_BRIDGE_STATICLIB)"
	@touch "$@"

$(MORK_BRIDGE_FEATURE_STATICLIB): $(MORK_BRIDGE_BUILD_STAMP)
	@mkdir -p $(dir $@)
	@cp "$(MORK_BRIDGE_STATICLIB)" "$@"

PY_CFLAGS =
PY_LDFLAGS =
PY_RPATH =
PYTHON_SRC = src/foreign_stub.c
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
BOOTSTRAP_TMPDIR = runtime/bootstrap
ifeq ($(ENABLE_RUNTIME_STATS),1)
BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_CANON).runtime-stats.h
BUILD_CONFIG_STAMP = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_CANON).runtime-stats.stamp
else
BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_CANON).h
BUILD_CONFIG_STAMP = $(BOOTSTRAP_TMPDIR)/build_config.$(BUILD_CANON).stamp
endif
STAGE0_BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.stage0.$(BUILD_CANON).h
STAGE0_BUILD_CONFIG_STAMP = $(BOOTSTRAP_TMPDIR)/build_config.stage0.$(BUILD_CANON).stamp
VERSION_FILE = VERSION
CETTA_VERSION := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null))
CPPFLAGS = -Isrc -I. $(BRIDGE_CFLAGS) $(PY_CFLAGS) -include $(BUILD_CONFIG_HEADER)
CFLAGS = -O3 -Wall -Werror -std=c11
DEPFLAGS = -MMD -MP
LDFLAGS = $(BRIDGE_LDFLAGS) -ldl -lm $(PY_LDFLAGS) $(PY_RPATH)

SRC = src/symbol.c src/atom.c src/parser.c src/mm2_lower.c src/subst_tree.c src/space.c src/space_match_backend.c src/match.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/answer_bank.c src/table_store.c src/search_machine.c src/term_universe.c src/stats.c src/eval.c src/grounded.c src/text_source.c src/native_handle.c src/mork_space_bridge_runtime.c src/library.c $(PYTHON_SRC) src/session.c src/lang.c src/compile.c src/runtime.c src/cetta_stdlib.c native/native_modules.c src/main.c
ifeq ($(ENABLE_RUNTIME_STATS),1)
OBJ = $(SRC:.c=.$(BUILD_CANON).runtime-stats.o)
BIN = runtime/cetta-$(BUILD_CANON)-runtime-stats
FALLBACK_EVAL_TEST_OBJ = runtime/bootstrap/test_fallback_eval_session.$(BUILD_CANON).runtime-stats.o
FALLBACK_EVAL_TEST_BIN = runtime/test_fallback_eval_session-$(BUILD_CANON)-runtime-stats
BIN_FORCE =
else
OBJ = $(SRC:.c=.$(BUILD_CANON).o)
BIN = cetta
FALLBACK_EVAL_TEST_OBJ = runtime/bootstrap/test_fallback_eval_session.$(BUILD_CANON).o
FALLBACK_EVAL_TEST_BIN = runtime/test_fallback_eval_session-$(BUILD_CANON)
BIN_FORCE = FORCE
endif
FALLBACK_EVAL_TEST_SRC = tests/support/test_fallback_eval_session.c
FALLBACK_EVAL_TEST_LINK_OBJ = $(filter-out src/main.$(BUILD_CANON).runtime-stats.o src/main.$(BUILD_CANON).o,$(OBJ))
STAGE0_BIN = runtime/cetta-stage0-$(BUILD_CANON)
SPACE_ENGINES = native native-candidate-exact
ifeq ($(ENABLE_PATHMAP_SPACE),1)
SPACE_ENGINES += pathmap
endif
D4_PROBE_TIMEOUT ?= 60
GIT_TEST_FIXTURE_ROOT = $(CURDIR)/runtime/git_module_fixture
GIT_TEST_CACHE_DIR = $(CURDIR)/runtime/test-git-module-cache
GIT_TEST_URL = file://$(GIT_TEST_FIXTURE_ROOT)
GIT_TEST_DYNAMIC = $(CURDIR)/runtime/test-git-module-dynamic.metta
GIT_TEST_COMPAT_DYNAMIC = $(CURDIR)/runtime/test-git-module-compat.metta
HE_CONTRACT_GENERATED_DIR = tests/generated/he_contract
TEST_MANIFEST = tests/test_manifest.tsv
PYTHON_TESTS = tests/test_py_ops_surface.metta tests/test_import_foreign_python_file.metta tests/test_import_foreign_pkg_error.metta tests/test_namespace_sugar_guardrails.metta
PATHMAP_REQUIRED_TESTS = \
	tests/test_space_type.metta \
	tests/test_space_engine_backend.metta \
	tests/test_add_atom_nodup_pathmap_alpha_regression.metta \
	tests/test_import_act_module_surface.metta \
	tests/test_include_mm2_space_target.metta \
	tests/test_module_inventory_act_registered_root.metta \
	tests/test_mork_act_roundtrip.metta \
	tests/test_pathmap_counted_space_surface.metta \
	tests/test_pathmap_contextual_var_projection_remove.metta \
	tests/test_space_batch_copy_optimizer_guards.metta \
	tests/test_pathmap_backend_primary_growth_regression.metta \
	tests/test_pathmap_fc_depth3_count_regression.metta \
	tests/test_pathmap_match_copy_var_identity_regression.metta \
	tests/test_pathmap_typed_query_surface.metta \
	tests/test_match_chain_cross_space_pathmap_regression.metta \
	tests/test_effect_append_batch_fastpath.metta \
	tests/test_space_batch_copy_surfaces.metta \
	tests/test_mork_fc_depth3_witness_regression.metta \
	tests/test_mork_recursive_bc_micro_regression.metta \
	tests/test_mork_recursive_bc_regression.metta

PATHMAP_PROBE_TESTS = \
	tests/test_mork_nil_parity_regression.metta \
	tests/test_mm2_match_order_fragile.metta \
	tests/test_pathmap_backend_primary_destructive_regression.metta

CORE_PROBE_TESTS = \
	tests/test_cverify_apply_subst_probe.metta \
	tests/test_cverify_apply_subst_with_unify_probe.metta \
	tests/test_print_nondet_probe.metta

# Empty is intentional; populate only for strict known-failing regressions.
CORE_XFAIL_TESTS =

RUNTIME_STATS_METTA_TESTS = \
	tests/spec_profile_runtime_stats_extension.metta \
	tests/test_dispatch_fastpath_equation_guard_regression.metta \
	tests/test_fc_native_depth3_count_regression.metta \
	tests/test_imported_conjunction_bridge_init_regression.metta \
	tests/test_imported_match_chain_conjunction_lowering.metta \
	tests/test_outcome_variant_composition_regression.metta \
	tests/test_outcome_variant_observation_seam_regression.metta \
	tests/test_pathmap_imported_bridge_v2.metta \
	tests/test_runtime_stats_surface.metta \
	tests/test_table_delayed_query_replay_regression.metta \
	tests/test_table_delayed_single_tail_reenter_regression.metta \
	tests/test_table_incremental_stage.metta \
	tests/test_table_invalidation_add.metta \
	tests/test_table_invalidation_remove.metta \
	tests/test_table_nodup_no_invalidation.metta \
	tests/test_table_reuse_after_stale.metta

BACKEND_DEDICATED_TESTS = \
	tests/test_closed_stream_fastpath.metta \
	tests/test_closed_stream_runtime_stats.metta \
	$(RUNTIME_STATS_METTA_TESTS) \
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
	tests/test_mork_add_atoms_runtime_stats.metta \
	tests/test_mm2_match_order_is_unordered.metta \
	tests/test_mork_mm2_metta_showcase.metta \
	tests/test_mork_open_act_surface.metta \
	tests/test_mork_overlay_zipper_surface.metta \
	tests/test_mork_product_zipper_surface.metta \
	tests/test_mork_zipper_surface.metta \
	tests/test_import_mm2_mork_session_lowering.metta \
	tests/test_mork_runtime_stats_isolation.metta \
	tests/test_new_space_mork_surface.metta \
	tests/test_step_space_surface.metta

BACKEND_HEAVY_TESTS = \
	tests/test_bio_bc_let_hidden_env_regression.metta \
	tests/test_bio_depth10_genuine_regression.metta \
	tests/test_bio_wmpln_checkpoint_regression.metta \
	tests/test_bio_wmpln_checkpoint_petta_flat.metta \
	tests/test_bio_wmpln_checkpoint_petta_top.metta \
	tests/test_bio_wmpln_pathway_route_regression.metta \
	tests/test_bio_wmpln_revise_stv_tuple_stack_regression.metta \
	tests/test_bio_wmpln_supported_key_unique_regression.metta \
	tests/test_checkpoint_disease_route_probe.metta \
	tests/test_checkpoint_group_extract_cross_form_regression.metta \
	tests/test_tilepuzzle.metta \
	tests/test_tilepuzzle_pathmap.metta

BACKEND_DIAGNOSTIC_TESTS = \
	tests/test_cverify_apply_subst_probe.metta \
	tests/test_cverify_apply_subst_with_unify_probe.metta \
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

all: $(BIN)

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

bench-weird-audit: $(BIN)
	$(call require_mork_bridge_or_reexec,weird benchmark audit,$@)
	@./scripts/bench_weird_audit.sh

bench-answer-ref-demand: $(BIN)
	$(call require_runtime_stats_or_reexec,answer-ref demand benchmark,$@)
	@./scripts/bench_answer_ref_demand.sh

bench-pathmap-fc-d3: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap FC depth-3 benchmark,$@)
	@./scripts/bench_pathmap_fc_d3.sh

bench-fc-backend-matrix:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_fc_backend_matrix.sh

bench-space-backend-matrix:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_space_backend_matrix.sh

bench-space-transfer-matrix:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_space_transfer_matrix.sh

bench-space-scale-ladder:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_space_scale_ladder.sh

bench-ffi-friction-light:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_ffi_friction_suite.sh light $(or $(BENCH_FFI_LIGHT_N),1000) $(or $(BENCH_FFI_LIGHT_ROUNDS),1)

bench-ffi-friction-basic:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_ffi_friction_suite.sh basic $(or $(BENCH_FFI_BASIC_N),10000) $(or $(BENCH_FFI_BASIC_ROUNDS),3)

bench-ffi-friction-stress:
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@./scripts/bench_ffi_friction_suite.sh stress $(or $(BENCH_FFI_STRESS_N),50000) $(or $(BENCH_FFI_STRESS_ROUNDS),3)

bench-ffi-friction-heavy:
	@if [ "$(BENCH_FFI_ALLOW_HEAVY)" != "1" ]; then \
		echo "Refusing heavy FFI benchmark without BENCH_FFI_ALLOW_HEAVY=1"; \
		echo "Try: BENCH_FFI_ALLOW_HEAVY=1 make bench-ffi-friction-heavy"; \
		exit 2; \
	fi
	@$(MAKE) -s BUILD=full ENABLE_RUNTIME_STATS=0 $(BIN)
	@BENCH_FFI_ALLOW_HEAVY=1 ./scripts/bench_ffi_friction_suite.sh heavy $(or $(BENCH_FFI_HEAVY_N),100000) $(or $(BENCH_FFI_HEAVY_ROUNDS),3)

perf-runtime-stats:
	$(call require_runtime_stats_or_reexec,runtime-stats probe,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@CETTA_BIN="$(abspath $(BIN))" ./scripts/bench_runtime_stats_probe.sh

probe-epoch-runtime-witness: $(BIN)
	$(call require_runtime_stats_or_reexec,epoch runtime witness,$@)
	@bash ./scripts/probe_epoch_runtime_witness.sh ./$(BIN)

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
	@if [ "$(AUTO_BUILD_OPTIONAL)" = "1" ]; then \
		$(MAKE) -s BUILD=$(BUILD_CANON) bench-optional-bridge-light; \
	fi

bench-optional-bridge-light:
	@$(MAKE) -s BUILD=full bench-ffi-friction-light

bench-capacity:
	@$(MAKE) -s BUILD=$(BUILD_CANON) perf-capacity-tu

bench-heavy:
	@if [ "$(BENCH_ALLOW_HEAVY)" != "1" ]; then \
		echo "Refusing heavy benchmark suite without BENCH_ALLOW_HEAVY=1"; \
		echo "Try: BENCH_ALLOW_HEAVY=1 make bench-heavy"; \
		exit 2; \
	fi
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-ffi-friction-stress
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-space-backend-matrix
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-space-transfer-matrix
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-space-scale-ladder
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d4-backends
	@$(MAKE) -s BUILD=$(BUILD_CANON) bench-d4-nodup-backends

test-symbolid-guard:
	@./scripts/check_symbolid_guards.sh

runtime/test_variant_shape_roundtrip: tests/test_variant_shape_roundtrip.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_variant_shape_roundtrip.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c -lm

test-variant-shape-roundtrip: runtime/test_variant_shape_roundtrip
	@./runtime/test_variant_shape_roundtrip

runtime/bench_mork_bridge_add: tests/bench_mork_bridge_add.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_add.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

runtime/bench_mork_bridge_query: tests/bench_mork_bridge_query.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/parser.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_query.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/parser.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

runtime/bench_mork_bridge_scalar_cursor: tests/bench_mork_bridge_scalar_cursor.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_scalar_cursor.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

runtime/bench_mork_bridge_space_ops: tests/bench_mork_bridge_space_ops.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/bench_mork_bridge_space_ops.c src/symbol.c src/atom.c src/match.c src/term_canon.c src/variant_shape.c src/mm2_lower.c src/term_universe.c src/mork_space_bridge_runtime.c $(LDFLAGS)

runtime/test_mork_bridge_contextual_exact_rows: tests/test_mork_bridge_contextual_exact_rows.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_mork_bridge_contextual_exact_rows.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-mork-bridge-contextual-exact-rows:
	$(call require_pathmap_bridge_or_reexec,mork bridge contextual exact rows packet,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/test_mork_bridge_contextual_exact_rows
	@./runtime/test_mork_bridge_contextual_exact_rows

runtime/test_space_term_universe_membership: tests/test_space_term_universe_membership.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_space_term_universe_membership.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c -lm

test-space-term-universe-membership: runtime/test_space_term_universe_membership
	@./runtime/test_space_term_universe_membership

runtime/test_term_universe_store_abi: CPPFLAGS += -DCETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS=1
runtime/test_term_universe_store_abi: tests/test_term_universe_store_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/parser.c src/cetta_stdlib.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_term_universe_store_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/parser.c src/cetta_stdlib.c -lm

test-term-universe-store-abi:
	$(call require_runtime_stats_or_reexec,term universe store ABI,$@)
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 runtime/test_term_universe_store_abi
	@./runtime/test_term_universe_store_abi

runtime/test_term_universe_backend_add_abi: CPPFLAGS += -DCETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS=1
runtime/test_term_universe_backend_add_abi: tests/test_term_universe_backend_add_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_term_universe_backend_add_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c -lm

test-term-universe-backend-add-abi:
	$(call require_runtime_stats_or_reexec,term universe backend-add ABI,$@)
	@$(MAKE) -B -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 runtime/test_term_universe_backend_add_abi
	@./runtime/test_term_universe_backend_add_abi

runtime/test_pathmap_backend_primary_destructive_abi: tests/test_pathmap_backend_primary_destructive_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_backend_primary_destructive_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-pathmap-backend-primary-destructive-abi:
	$(call require_pathmap_bridge_or_reexec,pathmap backend-primary destructive ABI,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/test_pathmap_backend_primary_destructive_abi
	@./runtime/test_pathmap_backend_primary_destructive_abi

runtime/test_pathmap_backend_primary_replace_abi: tests/test_pathmap_backend_primary_replace_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_backend_primary_replace_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-pathmap-backend-primary-replace-abi:
	$(call require_pathmap_bridge_or_reexec,pathmap backend-primary replace ABI,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/test_pathmap_backend_primary_replace_abi
	@./runtime/test_pathmap_backend_primary_replace_abi

runtime/test_pathmap_typed_query_abi: tests/test_pathmap_typed_query_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c src/mm2_lower.c src/mork_space_bridge_runtime.c $(BUILD_CONFIG_HEADER) $(BRIDGE_DEPS)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_pathmap_typed_query_abi.c src/symbol.c src/atom.c src/match.c src/subst_tree.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c src/grounded.c src/search_machine.c src/space.c src/space_match_backend.c src/parser.c src/mm2_lower.c src/mork_space_bridge_runtime.c $(LDFLAGS)

test-pathmap-typed-query-abi:
	$(call require_pathmap_bridge_or_reexec,pathmap typed query ABI,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/test_pathmap_typed_query_abi
	@./runtime/test_pathmap_typed_query_abi

# Stage 0: kernel-only binary (no precompiled stdlib)
STAGE0_OBJ = $(SRC:.c=.$(BUILD_CANON).stage0.o)
BUILD_CONFIG_INPUTS = Makefile $(VERSION_FILE)
DEPS = $(OBJ:.o=.d) $(STAGE0_OBJ:.o=.d) $(FALLBACK_EVAL_TEST_OBJ:.o=.d)

-include $(DEPS)

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
	printf '#define CETTA_BUILD_WITH_RUNTIME_STATS %s\n' "$(ENABLE_RUNTIME_STATS)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_RUNTIME_TIMING %s\n' "$(ENABLE_RUNTIME_TIMING)" >> "$$tmp_cfg"; \
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
	printf '#define CETTA_BUILD_WITH_RUNTIME_STATS 0\n' >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_RUNTIME_TIMING 0\n' >> "$$tmp_cfg"; \
	if [ -f "$(STAGE0_BUILD_CONFIG_HEADER)" ] && cmp -s "$$tmp_cfg" "$(STAGE0_BUILD_CONFIG_HEADER)"; then \
		rm -f "$$tmp_cfg"; \
	else \
		mv "$$tmp_cfg" "$(STAGE0_BUILD_CONFIG_HEADER)"; \
	fi; \
	touch "$@"

%.$(BUILD_CANON).stage0.o: %.c $(STAGE0_BUILD_CONFIG_HEADER)
	$(CC) -Isrc -I. $(BRIDGE_CFLAGS) $(PY_CFLAGS) -include $(STAGE0_BUILD_CONFIG_HEADER) $(CFLAGS) $(DEPFLAGS) -DCETTA_NO_STDLIB -MF $(@:.o=.d) -c -o $@ $<

$(STAGE0_BIN): $(STAGE0_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

# Stage 1: compile stdlib using stage0
$(STDLIB_BLOB): $(STDLIB_BLOB_STAMP)

$(STDLIB_BLOB_STAMP): $(STAGE0_BIN) $(STDLIB_SRC)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_stage0=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.run.XXXXXX"); \
	tmp_blob=$$(mktemp "$(BOOTSTRAP_TMPDIR)/stdlib_blob.XXXXXX"); \
	trap 'rm -f "$$tmp_stage0" "$$tmp_blob"' EXIT INT TERM; \
	cp "$(STAGE0_BIN)" "$$tmp_stage0"; \
	chmod +x "$$tmp_stage0"; \
	"$$tmp_stage0" --compile-stdlib $(STDLIB_SRC) > "$$tmp_blob"; \
	if [ -f "$(STDLIB_BLOB)" ] && cmp -s "$$tmp_blob" "$(STDLIB_BLOB)"; then \
		rm -f "$$tmp_blob"; \
	else \
		mv "$$tmp_blob" "$(STDLIB_BLOB)"; \
	fi; \
	touch "$@"

# Stage 2: full binary with precompiled stdlib
$(BIN): $(OBJ) $(BRIDGE_DEPS) $(BIN_FORCE)
	@mkdir -p $(BOOTSTRAP_TMPDIR) $(dir $@)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $(filter-out FORCE,$^) $(LDFLAGS); \
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

# stdlib objects depend on the generated blob header
src/cetta_stdlib.$(BUILD_CANON).o: src/cetta_stdlib.c src/cetta_stdlib.h $(STDLIB_BLOB) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

src/cetta_stdlib.$(BUILD_CANON).runtime-stats.o: src/cetta_stdlib.c src/cetta_stdlib.h $(STDLIB_BLOB) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

%.$(BUILD_CANON).o: %.c $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

%.$(BUILD_CANON).runtime-stats.o: %.c $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

clean:
	rm -f $(OBJ) $(STAGE0_OBJ) $(DEPS) $(BIN) $(STAGE0_BIN) cetta-stage0 \
		runtime/cetta-*-runtime-stats runtime/cetta-stage0-* \
		runtime/test_fallback_eval_session-* runtime/bootstrap/test_fallback_eval_session.*.o \
		runtime/bootstrap/test_fallback_eval_session.*.d \
		src/*.runtime-stats.o src/*.runtime-stats.d \
		native/*.runtime-stats.o native/*.runtime-stats.d \
		$(STDLIB_BLOB) runtime/bootstrap/mork-bridge.*.stamp \
		runtime/bootstrap/libcetta_space_bridge.*.a \
		$(BUILD_CONFIG_HEADER) $(STAGE0_BUILD_CONFIG_HEADER) runtime/bootstrap/build_config.h runtime/bootstrap/build_config.*.h runtime/bootstrap/build_config.stage0.h runtime/bootstrap/build_config.stage0.*.h \
		$(BUILD_CONFIG_STAMP) $(STAGE0_BUILD_CONFIG_STAMP) $(STDLIB_BLOB_STAMP) \
		src/foreign.o src/foreign.d src/foreign.stage0.o src/foreign.stage0.d \
		src/foreign_stub.o src/foreign_stub.d src/foreign_stub.stage0.o src/foreign_stub.stage0.d

promote-runtime: $(BIN)
	@./scripts/promote_runtime.sh

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
	result=$$(CETTA_GIT_MODULE_CACHE_DIR="$(GIT_TEST_CACHE_DIR)" ./$(BIN) --profile he_extended --lang he "$(GIT_TEST_DYNAMIC)" 2>&1); \
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
	result=$$(CETTA_GIT_MODULE_CACHE_DIR="$(GIT_TEST_CACHE_DIR)" ./$(BIN) --profile he_extended --lang he "$(GIT_TEST_COMPAT_DYNAMIC)" 2>&1); \
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
		'; he_compat should still expose the public HE git-module! surface.' \
		'!(git-module! "$(GIT_TEST_URL)")' \
		'!(import! &gitdb git_module_fixture)' \
		'!(assertEqualToResult (match &gitdb (git-root $$x) $$x) (loaded))' \
		> "$(GIT_TEST_COMPAT_DYNAMIC)"; \
	result=$$(CETTA_GIT_MODULE_CACHE_DIR="$(GIT_TEST_CACHE_DIR)" ./$(BIN) --profile he_compat --lang he "$(GIT_TEST_COMPAT_DYNAMIC)" 2>&1); \
	expected=$$'[()]\n[()]\n[()]'; \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: he_compat git-module! surface"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_compat git-module! surface"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

MORK_MM2_TEST3 := $(abspath ../MORK/examples/lean_conformance/test3_var_binding.mm2)
MORK_MM2_TEST4 := $(abspath ../MORK/examples/lean_conformance/test4_conjunctive.mm2)
MORK_MM2_TEST5 := $(abspath ../MORK/examples/lean_conformance/test5_equal_pair.mm2)
MORK_MM2_TEST6 := $(abspath ../MORK/examples/lean_conformance/test6_no_match.mm2)
MORK_MM2_TEST7 := $(abspath ../MORK/examples/lean_conformance/test7_nested.mm2)
MORK_MM2_TEST8 := $(abspath ../MORK/examples/lean_conformance/test8_multi_step.mm2)
MORK_MM2_TEST9 := $(abspath ../MORK/examples/lean_conformance/test9_priority_ordering.mm2)
MORK_MM2_TEST10 := $(abspath ../MORK/examples/lean_conformance/test10_conjunctive_wq.mm2)
MORK_MM2_SINK_ADD_CONSTANT := $(abspath ../MORK/examples/sinks/archive/test_add_constant.mm2)
MORK_MM2_SINK_ADD_SIMPLE := $(abspath ../MORK/examples/sinks/archive/test_add_simple.mm2)
MORK_MM2_SINK_REMOVE_SIMPLE := $(abspath ../MORK/examples/sinks/archive/test_remove_simple.mm2)
MORK_MM2_SINK_BULK_REMOVE := $(abspath ../MORK/examples/sinks/archive/test_bulk_remove.mm2)
MORK_MM2_SINK_COUNT_SIMPLE := $(abspath ../MORK/examples/sinks/archive/test_count_simple.mm2)
MORK_MM2_SINK_HEAD_LIMIT := $(abspath ../MORK/examples/sinks/archive/test_head_limit.mm2)

define require_mork_bridge_or_reexec
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" != "1" ] && [ -z "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
			bridge_build=mork; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
			echo "INFO: $(1) requires the MORK bridge; re-running with BUILD=$$bridge_build"; \
			$(MAKE) BUILD=$$bridge_build $(2); \
		else \
			if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
				echo "SKIP: $(1) (no MORK bridge manifest configured)"; \
			else \
				echo "SKIP: $(1) (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
			fi; \
		fi; \
		exit $$?; \
	fi
endef

define require_pathmap_bridge_or_reexec
	@if [ "$(ENABLE_PATHMAP_SPACE)" != "1" ]; then \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
			bridge_build=pathmap; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=full; fi; \
			echo "INFO: $(1) requires generic pathmap-backed spaces; re-running with BUILD=$$bridge_build"; \
			$(MAKE) BUILD=$$bridge_build $(2); \
		else \
			if [ ! -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
				echo "SKIP: $(1) (no MORK bridge manifest configured)"; \
			else \
				echo "SKIP: $(1) (Rust bridge deps unavailable; missing: $(MORK_BRIDGE_MISSING_MANIFESTS))"; \
			fi; \
		fi; \
		exit $$?; \
	fi
endef

define require_runtime_stats_or_reexec
	@if [ "$(ENABLE_RUNTIME_STATS)" != "1" ]; then \
		echo "INFO: $(1) requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"; \
		$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(2); \
		exit $$?; \
	fi
endef

test: $(BIN) test-manifest-strict test-git-module test-symbolid-guard test-variant-shape-roundtrip test-space-term-universe-membership test-help-flags test-he-contract-suite test-mork-lane-core test-closed-stream-fastpath
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
			result=$$(./$(BIN) --profile he_extended --lang he "$$f" 2>&1); \
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
		result=$$(./$(BIN) --profile he_extended --lang he "$$f" 2>&1); \
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
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-runtime-stats-lane

test-light: test

test-correctness: test

probe-core-lane: $(BIN)
	@for f in $(CORE_PROBE_TESTS); do \
		echo "PROBE: $$f"; \
		./$(BIN) --profile he_extended --lang he "$$f"; \
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
		result=$$(./$(BIN) --profile he_extended --lang he "$$f" 2>&1); \
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

test-correctness-all:
	@$(MAKE) -s BUILD=$(BUILD_CANON) test
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-heavy

test-runtime-stats-lane:
	@if [ "$(ENABLE_RUNTIME_STATS)" != "1" ]; then \
		echo "INFO: runtime-stats test lane requires compile-time runtime stats; re-running with ENABLE_RUNTIME_STATS=1"; \
		$(MAKE) BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-lane-body; \
	else \
		$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-lane-body; \
	fi

test-runtime-stats-lane-body:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-term-universe-store-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-term-universe-backend-add-abi
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-cli
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-closed-stream-runtime-stats
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-runtime-stats-metta-suite
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-mork-runtime-stats-lane
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-runtime-stats-lane
endif

test-runtime-stats-metta-suite:
	$(call require_runtime_stats_or_reexec,runtime-stats MeTTa suite,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 $(BIN)
	@pass=0; fail=0; no_exp=0; \
	for f in $(RUNTIME_STATS_METTA_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he "$$f" 2>&1); \
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

perf-list:
	@./scripts/run_witness.sh --list

perf-show-baselines:
	@./scripts/run_witness.sh --show-baselines

perf-capacity-tu:
	@./scripts/run_witness.sh tu_tail_special_forms
	@./scripts/run_witness.sh tu_tilepuzzle
	@./scripts/run_witness.sh tu_metamath_stream_basic

perf-bench-tu:
	@out=$$(./scripts/run_witness.sh tu_fc_d3_variant); \
	printf '%s\n' "$$out"; \
	status=$$(printf '%s\n' "$$out" | awk -F= '/^STATUS=/{ print $$2; exit }'); \
	test "$$status" = "pass"

perf-compare-tu:
	@./scripts/compare_witness.sh tu_fc_d3_variant
	@./scripts/compare_witness.sh tu_tail_special_forms
	@./scripts/compare_witness.sh tu_tilepuzzle
	@./scripts/compare_witness.sh tu_metamath_stream_basic

test-manifest test-manifest-check:
	@./scripts/sync_test_manifest.py --check

test-manifest-sync:
	@./scripts/sync_test_manifest.py --write

test-manifest-strict: test-manifest-check

test-profiles: $(BIN) test-manifest test-git-module-profiles test-symbolid-guard test-fallback-eval-session test-import-modes
	@pass=0; fail=0; \
	cache_dir="$(GIT_TEST_CACHE_DIR)"; mkdir -p "$$cache_dir"; export CETTA_GIT_MODULE_CACHE_DIR="$$cache_dir"; \
	profiles=$$(./$(BIN) --list-profiles 2>&1); \
	if printf '%s\n' "$$profiles" | grep -Eq '^he_compat[[:space:]]' && \
	   printf '%s\n' "$$profiles" | grep -Eq '^he_extended[[:space:]]' && \
	   printf '%s\n' "$$profiles" | grep -Eq '^he_prime[[:space:]]'; then \
		echo "PASS: profile inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: profile inventory"; \
		printf '%s\n' "$$profiles"; \
		fail=$$((fail + 1)); \
	fi; \
	base_result=$$(./$(BIN) --lang he tests/spec_profile_once_alias_extension.metta 2>&1); \
	if printf '%s\n' "$$base_result" | grep -Fq "surface once is unavailable in language he"; then \
		echo "PASS: he base surface uses compat policy"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he base surface uses compat policy"; \
		printf '%s\n' "$$base_result"; \
		fail=$$((fail + 1)); \
	fi; \
	mm2_profiles=$$(./$(BIN) --lang mm2 --list-profiles 2>&1); \
	if printf '%s\n' "$$mm2_profiles" | grep -Fq "language 'mm2' has no named profiles"; then \
		echo "PASS: mm2 has no named profiles"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2 has no named profiles"; \
		printf '%s\n' "$$mm2_profiles"; \
		fail=$$((fail + 1)); \
	fi; \
	mm2_profile_err=$$(./$(BIN) --lang mm2 --profile he_compat -e '()' 2>&1 || true); \
	if printf '%s\n' "$$mm2_profile_err" | grep -Fq "error: language 'mm2' has no named profiles"; then \
		echo "PASS: mm2 rejects foreign profiles"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2 rejects foreign profiles"; \
		printf '%s\n' "$$mm2_profile_err"; \
		fail=$$((fail + 1)); \
	fi; \
	for profile in he_compat he_extended he_prime; do \
		result=$$(./$(BIN) --profile "$$profile" --lang he tests/test_import_modules.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/test_import_modules.expected)" ]; then \
			echo "PASS: $$profile import modules"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile import modules"; \
			diff <(cat tests/test_import_modules.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_count_atoms.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/spec_profile_count_atoms.expected)" ]; then \
		echo "PASS: he_extended count-atoms extension"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_extended count-atoms extension"; \
		diff <(cat tests/spec_profile_count_atoms.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_count_atoms.metta 2>&1); \
	if printf '%s\n' "$$result" | grep -Fq "surface count-atoms is unavailable in profile he_compat"; then \
		echo "PASS: he_compat count-atoms guard"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_compat count-atoms guard"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_size_extension.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/spec_profile_size_extension.expected)" ]; then \
		echo "PASS: he_extended size extension"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_extended size extension"; \
		diff <(cat tests/spec_profile_size_extension.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_size_extension.metta 2>&1); \
	if printf '%s\n' "$$result" | grep -Fq "surface size is unavailable in profile he_compat"; then \
		echo "PASS: he_compat size guard"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_compat size guard"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_size_extension.metta 2>&1 >/dev/null); \
	if printf '%s\n' "$$compile_output" | grep -Fq "surface 'size' is unavailable in profile 'he_compat'"; then \
		echo "PASS: he_compat size compile guard"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_compat size compile guard"; \
		printf '%s\n' "$$compile_output"; \
		fail=$$((fail + 1)); \
	fi; \
	if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_size_extension.metta >/dev/null 2>&1; then \
		echo "PASS: he_extended size compile"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_extended size compile"; \
		fail=$$((fail + 1)); \
	fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_foldl_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_foldl_extension.expected)" ]; then \
			echo "PASS: he_extended foldl-atom-in-space extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended foldl-atom-in-space extension"; \
		diff <(cat tests/spec_profile_foldl_extension.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_foldl_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface foldl-atom-in-space is unavailable in profile he_compat"; then \
			echo "PASS: he_compat foldl-atom-in-space guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat foldl-atom-in-space guard"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_foldl_public.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_foldl_public.expected)" ]; then \
			echo "PASS: he_compat foldl-atom public surface"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat foldl-atom public surface"; \
			diff <(cat tests/spec_profile_foldl_public.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_collect_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_collect_extension.expected)" ]; then \
			echo "PASS: he_extended collect extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended collect extension"; \
			diff <(cat tests/spec_profile_collect_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_collect_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface collect is unavailable in profile he_compat"; then \
			echo "PASS: he_compat collect guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat collect guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_select_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_select_extension.expected)" ]; then \
			echo "PASS: he_extended select extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended select extension"; \
			diff <(cat tests/spec_profile_select_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_select_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface select is unavailable in profile he_compat"; then \
			echo "PASS: he_compat select guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat select guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_fold_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_fold_extension.expected)" ]; then \
			echo "PASS: he_extended fold extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended fold extension"; \
			diff <(cat tests/spec_profile_fold_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_fold_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface fold is unavailable in profile he_compat"; then \
			echo "PASS: he_compat fold guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat fold guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_fold_by_key_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_fold_by_key_extension.expected)" ]; then \
			echo "PASS: he_extended fold-by-key extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended fold-by-key extension"; \
			diff <(cat tests/spec_profile_fold_by_key_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_fold_by_key_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface fold-by-key is unavailable in profile he_compat"; then \
			echo "PASS: he_compat fold-by-key guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat fold-by-key guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_reduce_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_reduce_extension.expected)" ]; then \
			echo "PASS: he_extended reduce extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended reduce extension"; \
			diff <(cat tests/spec_profile_reduce_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_reduce_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface reduce is unavailable in profile he_compat"; then \
			echo "PASS: he_compat reduce guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat reduce guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_runtime_stats_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_runtime_stats_extension.expected)" ]; then \
			echo "PASS: he_extended runtime-stats extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended runtime-stats extension"; \
			diff <(cat tests/spec_profile_runtime_stats_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/support/profile_runtime_stats_runtime.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface runtime-stats! is unavailable in profile he_compat"; then \
			echo "PASS: he_compat runtime-stats guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat runtime-stats guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_once_alias_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_once_alias_extension.expected)" ]; then \
			echo "PASS: he_extended once alias"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended once alias"; \
			diff <(cat tests/spec_profile_once_alias_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_once_alias_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface once is unavailable in profile he_compat"; then \
			echo "PASS: he_compat once guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat once guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_search_policy_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_search_policy_extension.expected)" ]; then \
			echo "PASS: he_extended search-policy capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended search-policy capability"; \
			diff <(cat tests/spec_profile_search_policy_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_search_policy_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface search-policy is unavailable in profile he_compat"; then \
			echo "PASS: he_compat search-policy guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat search-policy guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_search_policy_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'search-policy' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile search-policy guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile search-policy guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_search_policy_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile search-policy"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile search-policy"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_space_set_match_backend_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_space_set_match_backend_extension.expected)" ]; then \
			echo "PASS: he_extended space-set-match-backend! extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended space-set-match-backend! extension"; \
			diff <(cat tests/spec_profile_space_set_match_backend_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_space_set_match_backend_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface space-set-match-backend! is unavailable in profile he_compat"; then \
			echo "PASS: he_compat space-set-match-backend! guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat space-set-match-backend! guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_space_set_match_backend_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'space-set-match-backend!' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile space-set-match-backend! guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile space-set-match-backend! guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_space_set_match_backend_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile space-set-match-backend!"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile space-set-match-backend!"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'count-atoms' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile guard"; pass=$$((pass + 1)); \
		else \
		echo "FAIL: he_compat compile guard"; \
		printf '%s\n' "$$compile_output"; \
		fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile extension"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_collect_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'collect' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile collect guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile collect guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_collect_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile collect"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile collect"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_select_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'select' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile select guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile select guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_select_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile select"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile select"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_fold_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'fold' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile fold guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile fold guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_fold_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile fold"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile fold"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_fold_by_key_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'fold-by-key' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile fold-by-key guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile fold-by-key guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_fold_by_key_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile fold-by-key"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile fold-by-key"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_reduce_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'reduce' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile reduce guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile reduce guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_reduce_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile reduce"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile reduce"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_runtime_stats_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'runtime-stats!' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile runtime-stats guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile runtime-stats guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_runtime_stats_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile runtime-stats"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile runtime-stats"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_module_inventory.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_module_inventory.expected)" ]; then \
			echo "PASS: he_extended module-inventory extension"; pass=$$((pass + 1)); \
		else \
		echo "FAIL: he_extended module-inventory extension"; \
		diff <(cat tests/spec_module_inventory.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$(./$(BIN) --profile he_compat --lang he tests/support/profile_module_inventory_runtime.metta 2>&1); \
	if printf '%s\n' "$$result" | grep -Fq "surface module-inventory! is unavailable in profile he_compat"; then \
		echo "PASS: he_compat module-inventory guard"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_compat module-inventory guard"; \
		printf '%s\n' "$$result"; \
		fail=$$((fail + 1)); \
	fi; \
	for profile in he_compat he_extended he_prime; do \
		result=$$(./$(BIN) --profile "$$profile" --lang he tests/spec_profile_system_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_system_extension.expected)" ]; then \
			echo "PASS: $$profile system capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile system capability"; \
			diff <(cat tests/spec_profile_system_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile "$$profile" --compile tests/support/profile_compile_system_extension.metta >/dev/null 2>&1; then \
			echo "PASS: $$profile compile system capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile compile system capability"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for profile in he_compat he_extended he_prime; do \
		result=$$(./$(BIN) --profile "$$profile" --lang he tests/spec_profile_fs_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_fs_extension.expected)" ]; then \
			echo "PASS: $$profile fs capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile fs capability"; \
			diff <(cat tests/spec_profile_fs_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile "$$profile" --compile tests/support/profile_compile_fs_extension.metta >/dev/null 2>&1; then \
			echo "PASS: $$profile compile fs capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile compile fs capability"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	for profile in he_compat he_extended he_prime; do \
		result=$$(./$(BIN) --profile "$$profile" --lang he tests/spec_profile_str_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_str_extension.expected)" ]; then \
			echo "PASS: $$profile str capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile str capability"; \
			diff <(cat tests/spec_profile_str_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile "$$profile" --compile tests/support/profile_compile_str_extension.metta >/dev/null 2>&1; then \
			echo "PASS: $$profile compile str capability"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$profile compile str capability"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	result=$$(./$(BIN) --profile he_extended --lang he tests/profile_he_prime_dependent_binders_compat.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dependent_binders_compat.expected)" ]; then \
		echo "PASS: he_extended keeps literal binder-domain behavior"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_extended keeps literal binder-domain behavior"; \
		diff <(cat tests/profile_he_prime_dependent_binders_compat.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$(./$(BIN) --profile he_prime --lang he tests/profile_he_prime_dependent_binders.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_dependent_binders.expected)" ]; then \
		echo "PASS: he_prime dependent binder telescope"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_prime dependent binder telescope"; \
		diff <(cat tests/profile_he_prime_dependent_binders.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	result=$$(./$(BIN) --profile he_prime --lang he tests/profile_he_prime_recursive_search.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/profile_he_prime_recursive_search.expected)" ]; then \
		echo "PASS: he_prime recursive dependent search"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_prime recursive dependent search"; \
		diff <(cat tests/profile_he_prime_recursive_search.expected) <(echo "$$result") | head -10; \
		fail=$$((fail + 1)); \
	fi; \
	compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_module_inventory.metta 2>&1 >/dev/null); \
	status=$$?; \
	if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'module-inventory!' is unavailable in profile 'he_compat'"; then \
		echo "PASS: he_compat module-inventory compile guard"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_compat module-inventory compile guard"; \
		printf '%s\n' "$$compile_output"; \
		fail=$$((fail + 1)); \
	fi; \
	if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_module_inventory.metta >/dev/null 2>&1; then \
		echo "PASS: he_extended compile module-inventory"; pass=$$((pass + 1)); \
	else \
		echo "FAIL: he_extended compile module-inventory"; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "$$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

test-fallback-eval-session: $(FALLBACK_EVAL_TEST_BIN)
	@result=$$(./$(FALLBACK_EVAL_TEST_BIN) 2>&1); \
	expected='(Error (once (superpose (1 2))) surface once is unavailable in language he)'; \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: fallback eval session uses base HE semantics"; \
	else \
		echo "FAIL: fallback eval session uses base HE semantics"; \
		diff <(printf '%s\n' "$$expected") <(printf '%s\n' "$$result") | head -20; \
		exit 1; \
	fi

test-import-modes: $(BIN)
	@default_result=$$(./$(BIN) --profile he_extended --lang he tests/support/import_mode/nested/use_parent_helper.metta 2>&1); \
	if printf '%s\n' "$$default_result" | grep -Fq "Failed to resolve module Helper"; then \
		echo "PASS: default relative import mode stays local"; \
	else \
		echo "FAIL: default relative import mode stays local"; \
		printf '%s\n' "$$default_result"; \
		exit 1; \
	fi; \
	ancestor_result=$$(./$(BIN) --profile he_extended --lang he --import-mode ancestor-walk tests/support/import_mode/nested/use_parent_helper.metta 2>&1); \
	if [ "$$ancestor_result" = "$$(cat tests/support/import_mode/nested/use_parent_helper.expected)" ]; then \
		echo "PASS: ancestor-walk import mode finds parent helper"; \
	else \
		echo "FAIL: ancestor-walk import mode finds parent helper"; \
		diff <(cat tests/support/import_mode/nested/use_parent_helper.expected) <(printf '%s\n' "$$ancestor_result") | head -20; \
		exit 1; \
	fi; \
	expected_inventory=$$'[()]\n[()]'; \
	inventory_result=$$(./$(BIN) --profile he_extended --lang he --import-mode ancestor-walk \
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
		--cetta ./$(BIN) \
		--lang he \
		--profile he_extended \
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

test-he-contract-suite: $(BIN)
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
		result=$$(./$(BIN) --profile he_extended --lang he "$$f" 2>&1); \
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
	@result=$$(./$(BIN) --profile he_extended --lang he tests/test_mork_add_atoms_runtime_stats.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_add_atoms_runtime_stats.expected)" ]; then \
		echo "PASS: test_mork_add_atoms_runtime_stats"; \
	else \
		echo "FAIL: test_mork_add_atoms_runtime_stats"; \
		diff <(cat tests/test_mork_add_atoms_runtime_stats.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-deprecated-space-engine-mork-guard: $(BIN)
	@status=0; \
	result=$$(./$(BIN) --space-engine mork --lang he tests/test_space_type.metta 2>&1) || status=$$?; \
	if [ "$(ENABLE_PATHMAP_SPACE)" = "1" ]; then \
		pathmap_line="  pathmap                flattened PathMap-style CeTTa engine without bridge rows"; \
	else \
		pathmap_line="  pathmap                flattened PathMap-style CeTTa engine without bridge rows (requires BUILD=pathmap or BUILD=full)"; \
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
		result=$$(./$(BIN) --profile he_extended --lang he \
			-e '!(assertEqualToResult (new-space pathmap) ((Error (new-space pathmap) "generic pathmap-backed spaces require BUILD=pathmap or BUILD=full")))' \
			-e '!(bind! &h (new-space hash))' \
			-e '!(assertEqualToResult (space-set-backend! &h pathmap) ((Error (space-set-backend! &h pathmap) "generic pathmap-backed spaces require BUILD=pathmap or BUILD=full")))' \
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
		bridge_build=pathmap; \
		if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=full; fi; \
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
		result=$$(./$(BIN) --profile he_extended --lang he "$$f" 2>&1); \
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
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-long-string-regression
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-match-chain
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lib-pathmap
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-duplicate-multiplicity-backends

probe-pathmap-lane:
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) probe-pathmap-lane-body
else
	@if [ -f "$(MORK_BRIDGE_MANIFEST)" ] && [ "$(MORK_BRIDGE_DEPS_READY)" = "1" ]; then \
		bridge_build=pathmap; \
		if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=full; fi; \
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
		./$(BIN) --profile he_extended --lang he "$$f"; \
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
		bridge_build=pathmap; \
		if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=full; fi; \
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
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-bridge-v2
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-conjunction-init
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_STATS=1 test-pathmap-match-chain-v3

test-mm2-mork-program-space: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 MORK program-space lowering regression,$@)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --lang mm2 tests/support/mm2_mork_program_space.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: mm2 MORK program-space lowering regression"; \
	else \
		echo "FAIL: mm2 MORK program-space lowering regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mm2-exec-basic: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 direct execution seam,$@)
	@ \
	result=$$(./$(BIN) --lang mm2 tests/mm2_exec_basic.mm2 2>&1); \
	if [ "$$result" = "$$(cat tests/mm2_exec_basic.expected)" ]; then \
		echo "PASS: mm2 direct execution seam"; \
	else \
		echo "FAIL: mm2 direct execution seam"; \
		diff <(cat tests/mm2_exec_basic.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-import-mm2-mork-session-lowering: $(BIN)
	$(call require_mork_bridge_or_reexec,mork-space sugar over explicit handles,$@)
	@ \
	result=$$(./$(BIN) --profile he_extended --lang he \
		tests/test_import_mm2_mork_session_lowering.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_import_mm2_mork_session_lowering.expected)" ]; then \
		echo "PASS: mork-space sugar over explicit handles"; \
	else \
		echo "FAIL: mork-space sugar over explicit handles"; \
		diff <(cat tests/test_import_mm2_mork_session_lowering.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mm2-kiss-suite: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 KISS raw example suite,$@)
	@ \
	prep=$$(./$(BIN) --quiet --profile he_extended --lang he tests/support/prepare_mm2_kiss_fruit_colors_act.metta 2>&1); \
	if [ -n "$$prep" ]; then \
		echo "FAIL: mm2 KISS ACT prepare"; \
		printf '%s\n' "$$prep"; \
		exit 1; \
	fi; \
	pass=0; fail=0; \
	for stem in mm2_kiss_add_remove mm2_kiss_priority mm2_kiss_fractal_priority mm2_kiss_count_groupby mm2_kiss_act_join; do \
		result=$$(./$(BIN) --lang mm2 "tests/$$stem.mm2" 2>&1); \
		if [ "$$result" = "$$(cat "tests/$$stem.expected")" ]; then \
			echo "PASS: $$stem"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$stem"; \
			diff <(cat "tests/$$stem.expected") <(echo "$$result") | head -20; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	step_result=$$(./$(BIN) --lang mm2 --steps 1 tests/mm2_kiss_fractal_priority.mm2 2>&1); \
	if [ "$$step_result" = "$$(cat tests/mm2_kiss_fractal_priority.step1.expected)" ]; then \
		echo "PASS: mm2_kiss_fractal_priority --steps 1"; \
		pass=$$((pass + 1)); \
	else \
		echo "FAIL: mm2_kiss_fractal_priority --steps 1"; \
		diff <(cat tests/mm2_kiss_fractal_priority.step1.expected) <(echo "$$step_result") | head -20; \
		fail=$$((fail + 1)); \
	fi; \
	for stem in test_import_mm2_module_surface; do \
		result=$$(./$(BIN) --profile he_extended --lang he "tests/$$stem.metta" 2>&1); \
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
		result=$$(./$(BIN) --profile he_extended --lang he "tests/$$stem.metta" 2>&1); \
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

test-mork-surface-suite: $(BIN)
	$(call require_mork_bridge_or_reexec,mork surface suite,$@)
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
		test_mork_open_act_surface \
		test_mork_overlay_zipper_surface \
		test_mork_product_zipper_surface \
		test_mork_zipper_surface \
		test_new_space_mork_surface \
		test_step_space_surface; do \
		result=$$(./$(BIN) --profile he_extended --lang he "tests/$$stem.metta" 2>&1); \
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
	@result=$$(./$(BIN) --profile he_extended --lang he tests/test_mork_runtime_stats_isolation.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_runtime_stats_isolation.expected)" ]; then \
		echo "PASS: test_mork_runtime_stats_isolation"; \
	else \
		echo "FAIL: test_mork_runtime_stats_isolation"; \
		diff <(cat tests/test_mork_runtime_stats_isolation.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-closed-stream-fastpath: $(BIN)
	@result=$$(./$(BIN) --quiet --profile he_extended --lang he tests/test_closed_stream_fastpath.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_closed_stream_fastpath.expected)" ]; then \
		echo "PASS: test_closed_stream_fastpath"; \
	else \
		echo "FAIL: test_closed_stream_fastpath"; \
		diff <(cat tests/test_closed_stream_fastpath.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-closed-stream-runtime-stats: $(BIN)
	$(call require_runtime_stats_or_reexec,closed-stream runtime-stats regression,$@)
	@result=$$(./$(BIN) --quiet --profile he_extended --lang he tests/test_closed_stream_runtime_stats.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_closed_stream_runtime_stats.expected)" ]; then \
		echo "PASS: test_closed_stream_runtime_stats"; \
	else \
		echo "FAIL: test_closed_stream_runtime_stats"; \
		diff <(cat tests/test_closed_stream_runtime_stats.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mm2-conformance-var-binding: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 var-binding conformance seam,$@)
	@ \
	result=$$(./$(BIN) --lang mm2 "$(MORK_MM2_TEST3)" 2>&1); \
	if [ "$$result" = "$$(cat tests/mm2_conformance_var_binding.expected)" ]; then \
		echo "PASS: mm2 var-binding conformance seam"; \
	else \
		echo "FAIL: mm2 var-binding conformance seam"; \
		diff <(cat tests/mm2_conformance_var_binding.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mm2-conformance-lean-suite: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 lean conformance suite,$@)
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
		result=$$(./$(BIN) --lang mm2 "$$file" 2>&1); \
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

test-mm2-sink-suite: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 sink suite,$@)
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
		result=$$(./$(BIN) --lang mm2 "$$file" 2>&1); \
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

test-pathmap-conjunction-init: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap conjunction init regression,$@)
	$(call require_runtime_stats_or_reexec,pathmap conjunction init regression,$@)
	@ \
	result=$$(./$(BIN) --profile he_extended --space-engine pathmap --lang he tests/test_imported_conjunction_bridge_init_regression.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_imported_conjunction_bridge_init_regression.expected)" ]; then \
		echo "PASS: pathmap conjunction init regression"; \
	else \
		echo "FAIL: pathmap conjunction init regression"; \
		diff <(cat tests/test_imported_conjunction_bridge_init_regression.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-pathmap-bridge-v2: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap bridge v2 regression,$@)
	$(call require_runtime_stats_or_reexec,pathmap bridge v2 regression,$@)
	@expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --profile he_extended --space-engine pathmap --lang he tests/test_pathmap_imported_bridge_v2.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: pathmap bridge v2 regression"; \
	else \
		echo "FAIL: pathmap bridge v2 regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
			exit 1; \
		fi

test-pathmap-long-string-regression: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap long-string regression,$@)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --space-engine pathmap --lang he tests/support/pathmap_imported_long_string_probe.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: pathmap long-string regression"; \
	else \
		echo "FAIL: pathmap long-string regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-pathmap-match-chain: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap nested-match chain regression,$@)
	@ \
	result=$$(./$(BIN) --space-engine pathmap --lang he tests/test_match_chain_imported_regression.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_match_chain_imported_regression.expected)" ]; then \
		echo "PASS: pathmap nested-match chain regression"; \
	else \
		echo "FAIL: pathmap nested-match chain regression"; \
		diff <(cat tests/test_match_chain_imported_regression.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-pathmap-match-chain-v3: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap nested-match conjunction lowering regression,$@)
	$(call require_runtime_stats_or_reexec,pathmap nested-match conjunction lowering regression,$@)
	@ \
	result=$$(./$(BIN) --profile he_extended --space-engine pathmap --lang he tests/test_imported_match_chain_conjunction_lowering.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_imported_match_chain_conjunction_lowering.expected)" ]; then \
		echo "PASS: pathmap nested-match conjunction lowering regression"; \
	else \
		echo "FAIL: pathmap nested-match conjunction lowering regression"; \
		diff <(cat tests/test_imported_match_chain_conjunction_lowering.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mork-lib-pathmap: $(BIN)
	$(call require_pathmap_bridge_or_reexec,mork lib pathmap probe,$@)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --profile he_extended --space-engine pathmap --lang he tests/support/mork_lib_pathmap_imported.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: mork lib pathmap probe"; \
	else \
		echo "FAIL: mork lib pathmap probe"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mork-open-act: $(BIN)
	$(call require_mork_bridge_or_reexec,mork open-act probe,$@)
	@ \
	result=$$(./$(BIN) --profile he_extended --lang he tests/test_mork_open_act_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_open_act_surface.expected)" ]; then \
		echo "PASS: mork open-act probe"; \
	else \
		echo "FAIL: mork open-act probe"; \
		diff <(cat tests/test_mork_open_act_surface.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-pretty-vars-flags: $(BIN)
	@raw_result=$$(./$(BIN) --raw-vars --profile he_extended --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	default_result=$$(./$(BIN) --profile he_extended --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	pretty_result=$$(./$(BIN) --pretty-vars --profile he_extended --lang he tests/test_pretty_vars_surface.metta 2>&1); \
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
	@raw_result=$$(./$(BIN) --raw-namespaces --profile he_extended --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	default_result=$$(./$(BIN) --profile he_extended --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	pretty_result=$$(./$(BIN) --pretty-namespaces --profile he_extended --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
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
	$(call require_mork_bridge_or_reexec,bio eqtl ACT prepare,$@)
	@./scripts/bench_mork_act_eqtl.sh prepare
	@echo "PASS: prepared runtime/bench_eqtl_for_mining.act"

bench-bio-eqtl-act-modes: $(BIN)
	$(call require_mork_bridge_or_reexec,bio eqtl ACT benchmark,$@)
	@ \
	./scripts/bench_mork_act_eqtl.sh all

prepare-bio-1m-act: $(BIN)
	$(call require_mork_bridge_or_reexec,bio 1m ACT prepare,$@)
	@ \
	result=$$(./$(BIN) --quiet --profile he_extended --lang he tests/support/prepare_bio_1m_act.metta 2>&1); \
	if [ -z "$$result" ]; then \
		echo "PASS: prepared runtime/bench_bio_1m.act"; \
	else \
		echo "FAIL: bio 1m ACT prepare"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi

bench-bio-1m-act-attach: $(BIN)
	$(call require_mork_bridge_or_reexec,bio 1m ACT attached benchmark,$@)
	@ \
	./scripts/bench_mork_act_bio_1m_attach.sh

bench-bio-1m-act-modes: $(BIN)
	$(call require_mork_bridge_or_reexec,bio 1m ACT benchmark,$@)
	@ \
	echo "NOTE: attached ACT is the verified 1.4M path under the 6GB CeTTa limit; combined source/materialize comparison remains experimental"; \
	./scripts/bench_mork_act_bio_1m_attach.sh

test-duplicate-multiplicity-backends: $(BIN)
	$(call require_pathmap_bridge_or_reexec,duplicate multiplicity backend probe,$@)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	for backend in native native-candidate-exact pathmap; do \
		result=$$(./$(BIN) --profile he_extended --space-engine "$$backend" --lang he tests/support/duplicate_multiplicity_probe.metta 2>&1); \
		if [ "$$result" = "$$expected" ]; then \
			echo "PASS: $$backend duplicate multiplicity probe"; \
		else \
			echo "FAIL: $$backend duplicate multiplicity probe"; \
			diff <(echo "$$expected") <(echo "$$result") | head -20; \
			exit 1; \
		fi; \
	done

test-runtime-stats-cli: $(BIN)
	$(call require_runtime_stats_or_reexec,runtime stats cli flags,$@)
	@result=$$(./$(BIN) --emit-runtime-stats --quiet --lang he tests/support/runtime_stats_cli_probe.metta 2>&1 >/dev/null); \
	if printf '%s\n' "$$result" | grep -Fq 'runtime-counter query-equations ' && \
	   printf '%s\n' "$$result" | grep -Fq 'runtime-counter rename-vars ' && \
	   ! printf '%s\n' "$$result" | grep -Fq '[ok]'; then \
		echo "PASS: runtime stats cli flags"; \
	else \
		echo "FAIL: runtime stats cli flags"; \
		printf '%s\n' "$$result"; \
		exit 1; \
	fi

test-help-flags: $(BIN)
	@help_long=$$(./$(BIN) --help 2>&1); \
	help_short=$$(./$(BIN) -h 2>&1); \
	if printf '%s\n' "$$help_long" | grep -Fq 'usage: cetta [--lang <name>] <file.metta>' && \
	   printf '%s\n' "$$help_long" | grep -Fq 'cetta --lang mm2 --steps <n> <file.mm2>' && \
	   [ "$$help_long" = "$$help_short" ]; then \
		echo "PASS: cli help flags"; \
	else \
		echo "FAIL: cli help flags"; \
		printf '%s\n' '--- --help ---'; \
		printf '%s\n' "$$help_long"; \
		printf '%s\n' '--- -h ---'; \
		printf '%s\n' "$$help_short"; \
		exit 1; \
	fi

probe-imported-conjunction-lanes: $(BIN)
	$(call require_pathmap_bridge_or_reexec,imported conjunction lane probe,$@)
	@ \
	./$(BIN) --profile he_extended --space-engine pathmap --lang he \
		tests/support/imported_conjunction_lane_probe.metta

# Slow: regenerate .expected files from HE CLI oracle.
# Run ONE AT A TIME to avoid OOM. Requires conda hyperon env.
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
		ulimit -v 6291456; \
		source $$HOME/miniconda3/bin/activate hyperon && \
		timeout 30 metta "$$f" > "$$exp" 2>&1; \
	done; \
	echo "done — .expected files updated"

# Benchmark: forward chaining depth 3. Uses --count-only to avoid giant stdout.
# Checks theorem count matches the current pinned regression number.
bench-d3: $(BIN)
	@count=$$(./$(BIN) --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
	echo "depth-3 total: $$count theorems"; \
	if [ "$$count" = "3421" ]; then \
		echo "PASS: theorem count matches"; \
	else \
		echo "FAIL: expected 3421, got $$count"; exit 1; \
	fi

bench-d3-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 total: $$count theorems"; \
		if [ "$$count" = "3421" ]; then \
			echo "PASS: $$backend theorem count matches"; \
		else \
			echo "FAIL: expected 3421, got $$count for $$backend"; exit 1; \
		fi; \
	done

probe-d3-nodup: $(BIN)
	@count=$$(./$(BIN) --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
	echo "depth-3 nodup probe: $$count theorems"; \
	if printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: nodup probe produced a numeric count"; \
		echo "NOTE: historical 3268 expectation is retired; use bench-weird-audit for recorded counts."; \
	else \
		echo "FAIL: nodup probe did not produce a valid count"; exit 1; \
	fi

probe-d3-nodup-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 nodup probe: $$count theorems"; \
		if printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
			echo "PASS: $$backend nodup probe produced a numeric count"; \
		else \
			echo "FAIL: $$backend nodup probe did not produce a valid count"; exit 1; \
		fi; \
	done

bench-d3-nodup: probe-d3-nodup

bench-d3-nodup-backends: probe-d3-nodup-backends

bench-conj-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/bench_conjunction_he.metta 2>&1 | tail -1); \
		echo "$$backend conjunction total: $$count results"; \
		if [ "$$count" = "216" ]; then \
			echo "PASS: $$backend conjunction count matches"; \
		else \
			echo "FAIL: expected 216, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-conj12-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/bench_conjunction12_he.metta 2>&1 | tail -1); \
		echo "$$backend conjunction12 total: $$count results"; \
		if [ "$$count" = "20736" ]; then \
			echo "PASS: $$backend conjunction12 count matches"; \
		else \
			echo "FAIL: expected 20736, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-join8-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/bench_matchjoin8_he.metta 2>&1 | tail -1); \
		echo "$$backend join8 total: $$count results"; \
		if [ "$$count" = "4096" ]; then \
			echo "PASS: $$backend join8 count matches"; \
		else \
			echo "FAIL: expected 4096, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-join12-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/bench_matchjoin12_he.metta 2>&1 | tail -1); \
		echo "$$backend join12 total: $$count results"; \
		if [ "$$count" = "20736" ]; then \
			echo "PASS: $$backend join12 count matches"; \
		else \
			echo "FAIL: expected 20736, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-conj12-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh tests/bench_conjunction12_he.metta "$$backend"; \
		echo "---"; \
	done

bench-dup-conj-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		count=$$(./$(BIN) --space-engine "$$backend" --count-only tests/bench_duplicate_conjunction_he.metta 2>&1 | tail -1); \
		echo "$$backend duplicate conjunction total: $$count results"; \
		if [ "$$count" = "4096" ]; then \
			echo "PASS: $$backend duplicate conjunction count matches"; \
		else \
			echo "FAIL: expected 4096, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-dup-conj-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh tests/bench_duplicate_conjunction_he.metta "$$backend"; \
		echo "---"; \
	done

bench-join8-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh tests/bench_matchjoin8_he.metta "$$backend"; \
		echo "---"; \
	done

bench-join12-runtime-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		./scripts/bench_space_match_runtime.sh tests/bench_matchjoin12_he.metta "$$backend"; \
		echo "---"; \
	done

bench-d4: $(BIN)
	@out=$$(ulimit -v 6291456; timeout 600 ./$(BIN) --count-only tests/nil_pc_fc_d4.metta 2>&1); \
	status=$$?; \
	count=$$(printf '%s\n' "$$out" | tail -1); \
	echo "depth-4 total: $$count theorems"; \
	if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: depth-4 produced a count under the memory cap"; \
	else \
		printf '%s\n' "$$out" | tail -5; \
		echo "FAIL: depth-4 did not produce a valid count"; exit 1; \
	fi

bench-d4-nodup: $(BIN)
	@out=$$(ulimit -v 6291456; timeout 600 ./$(BIN) --count-only tests/nil_pc_fc_d4_nodup.metta 2>&1); \
	status=$$?; \
	count=$$(printf '%s\n' "$$out" | tail -1); \
	echo "depth-4 nodup total: $$count theorems"; \
	if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
		echo "PASS: depth-4 nodup produced a count under the memory cap"; \
	else \
		printf '%s\n' "$$out" | tail -5; \
		echo "FAIL: depth-4 nodup did not produce a valid count"; exit 1; \
	fi

bench-d4-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		out=$$(ulimit -v 6291456; timeout $(D4_PROBE_TIMEOUT) ./$(BIN) --space-engine "$$backend" --count-only tests/nil_pc_fc_d4.metta 2>&1); \
		status=$$?; \
		count=$$(printf '%s\n' "$$out" | grep -E '^[0-9]+$$' | tail -1); \
		checkpoint=$$(printf '%s\n' "$$out" | grep '\[chain\]' | tail -1); \
		if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
			echo "$$backend depth-4 complete: $$count theorems"; \
		elif [ $$status -eq 124 ] && [ -n "$$checkpoint" ]; then \
			echo "$$backend depth-4 probe ($(D4_PROBE_TIMEOUT)s): $$checkpoint"; \
		elif [ $$status -eq 124 ]; then \
			echo "$$backend depth-4 probe ($(D4_PROBE_TIMEOUT)s): no checkpoint yet"; \
		else \
			printf '%s\n' "$$out" | tail -10; \
			echo "FAIL: $$backend depth-4 probe did not produce a valid count or checkpoint"; exit 1; \
		fi; \
	done

bench-d4-nodup-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		out=$$(ulimit -v 6291456; timeout $(D4_PROBE_TIMEOUT) ./$(BIN) --space-engine "$$backend" --count-only tests/nil_pc_fc_d4_nodup.metta 2>&1); \
		status=$$?; \
		count=$$(printf '%s\n' "$$out" | grep -E '^[0-9]+$$' | tail -1); \
		checkpoint=$$(printf '%s\n' "$$out" | grep '\[chain\]' | tail -1); \
		if [ $$status -eq 0 ] && printf '%s' "$$count" | grep -Eq '^[0-9]+$$'; then \
			echo "$$backend depth-4 nodup complete: $$count theorems"; \
		elif [ $$status -eq 124 ] && [ -n "$$checkpoint" ]; then \
			echo "$$backend depth-4 nodup probe ($(D4_PROBE_TIMEOUT)s): $$checkpoint"; \
		elif [ $$status -eq 124 ]; then \
			echo "$$backend depth-4 nodup probe ($(D4_PROBE_TIMEOUT)s): no checkpoint yet"; \
		else \
			printf '%s\n' "$$out" | tail -10; \
			echo "FAIL: $$backend depth-4 nodup probe did not produce a valid count or checkpoint"; exit 1; \
		fi; \
	done

bench-compare-petta: $(BIN)
	@./scripts/bench_compare_cetta_petta.sh

bench-mork-add-interface: $(BIN)
	$(call require_mork_bridge_or_reexec,mork add interface benchmark,$@)
	@./scripts/bench_mork_add_interface.sh

bench-mork-add-interface-timing:
	@$(MAKE) -s BUILD=$(BUILD_CANON) ENABLE_RUNTIME_TIMING=1 bench-mork-add-interface

bench-mork-bridge-add:
	$(call require_mork_bridge_or_reexec,mork low-level bridge add benchmark,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_add
	@for n in $(or $(BENCH_MORK_BRIDGE_SIZES),1000 10000 100000); do \
		echo "=== bridge-add $$n ==="; \
		(ulimit -v 10485760; ./runtime/bench_mork_bridge_add "$$n" $(or $(BENCH_MORK_BRIDGE_REPEAT),3)); \
		echo; \
	done

bench-mork-bridge-query:
	$(call require_mork_bridge_or_reexec,mork low-level bridge query benchmark,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_query
	@for n in $(or $(BENCH_MORK_BRIDGE_QUERY_SIZES),1000 10000 100000); do \
		echo "=== bridge-query $$n ==="; \
		(ulimit -v 10485760; ./runtime/bench_mork_bridge_query "$$n" $(or $(BENCH_MORK_BRIDGE_QUERY_REPEAT),3)); \
		echo; \
	done

bench-mork-bridge-scalar-cursor:
	$(call require_mork_bridge_or_reexec,mork low-level bridge scalar and cursor benchmark,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_scalar_cursor
	@for n in $(or $(BENCH_MORK_BRIDGE_SCALAR_CURSOR_SIZES),1000 10000 100000); do \
		echo "=== bridge-scalar-cursor $$n ==="; \
		(ulimit -v 10485760; ./runtime/bench_mork_bridge_scalar_cursor "$$n" $(or $(BENCH_MORK_BRIDGE_SCALAR_CURSOR_REPEAT),3)); \
		echo; \
	done

bench-mork-bridge-space-ops:
	$(call require_mork_bridge_or_reexec,mork low-level bridge ACT and algebra benchmark,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) runtime/bench_mork_bridge_space_ops
	@for n in $(or $(BENCH_MORK_BRIDGE_SPACE_OPS_SIZES),1000 10000 100000); do \
		echo "=== bridge-space-ops $$n ==="; \
		(ulimit -v 10485760; ./runtime/bench_mork_bridge_space_ops "$$n" $(or $(BENCH_MORK_BRIDGE_SPACE_OPS_REPEAT),3)); \
		echo; \
	done

bench-closed-stream-fastpath: $(BIN)
	@./scripts/bench_closed_stream_fastpath.sh $(or $(BENCH_CLOSED_STREAM_SIZES),1000 10000 100000) $(or $(BENCH_CLOSED_STREAM_REPEAT),3)

tail-recursion-check: $(BIN)
	@result=$$(./$(BIN) tests/tail_recursion_deep.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/tail_recursion_deep.expected)" ]; then \
		echo "PASS: deep tail recursion under explicit fuel"; \
	else \
		echo "FAIL: deep tail recursion mismatch"; \
		diff <(cat tests/tail_recursion_deep.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

# LLVM IR validation: verify emitted IR compiles through opt/llc.
compile-test: $(BIN)
	@pass=0; fail=0; \
	for f in tests/test_equations.metta tests/test_basic_eval.metta tests/test_disc_trie.metta tests/test_compile_arity.metta tests/test_compile_hybrid_interop.metta; do \
		[ -f "$$f" ] || continue; \
		ir=$$(./$(BIN) --compile "$$f" 2>&1); \
		if echo "$$ir" | opt -S -o /dev/null 2>/dev/null; then \
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

refresh-he-matrices:
	@python3 scripts/refresh_he_runtime_matrices.py
	@python3 -m json.tool specs/he_runtime_impl_matrix.json > /dev/null
	@python3 -m json.tool specs/he_runtime_3layer_matrix.json > /dev/null
	@echo "refreshed HE runtime parity matrices"

.PHONY: FORCE all core python mork main pathmap full profile clean test test-light test-correctness test-heavy test-correctness-all test-manifest test-manifest-check test-manifest-sync test-runtime-stats-lane test-runtime-stats-metta-suite test-backends test-he-contract-suite refresh-he-contract-tests test-mork-lane test-mork-lane-core test-mork-basic-pathmap-guard test-mork-runtime-stats-lane test-mork-runtime-stats-isolation test-closed-stream-fastpath test-closed-stream-runtime-stats test-pathmap-lane test-pathmap-lane-body test-pathmap-runtime-stats-lane test-pathmap-runtime-stats-lane-body test-mm2-lowering-core test-mm2-mork-program-space test-mm2-exec-basic test-mm2-kiss-suite test-mm2-conformance-var-binding test-mm2-conformance-lean-suite test-mm2-sink-suite test-pathmap-bridge-v2 test-pathmap-long-string-regression test-pathmap-match-chain test-mork-lib-pathmap test-mork-open-act test-pretty-vars-flags test-pretty-namespaces-flags test-help-flags test-variant-shape-roundtrip test-space-term-universe-membership test-term-universe-store-abi test-term-universe-backend-add-abi test-pathmap-backend-primary-destructive-abi test-pathmap-backend-primary-replace-abi test-pathmap-typed-query-abi test-fallback-eval-session test-import-modes bench bench-light bench-correctness bench-performance-light bench-optional-bridge-light bench-capacity bench-heavy prepare-bio-eqtl-act bench-bio-eqtl-act-modes prepare-bio-1m-act bench-bio-1m-act-attach bench-bio-1m-act-modes test-duplicate-multiplicity-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends probe-d3-nodup probe-d3-nodup-backends bench-conj-backends bench-conj12-backends bench-dup-conj-backends bench-d4 bench-d4-nodup bench-d4-backends bench-d4-nodup-backends bench-compare-petta bench-mork-add-interface bench-mork-add-interface-timing bench-mork-bridge-add bench-mork-bridge-query bench-mork-bridge-scalar-cursor bench-mork-bridge-space-ops bench-answer-ref-demand bench-space-backend-matrix bench-space-transfer-matrix bench-space-scale-ladder bench-ffi-friction-light bench-ffi-friction-basic bench-ffi-friction-stress bench-ffi-friction-heavy bench-closed-stream-fastpath bench-weird-audit tail-recursion-check compile-test refresh-he-matrices promote-runtime perf-list perf-show-baselines perf-capacity-tu perf-bench-tu perf-compare-tu probe-epoch-runtime-witness
.PHONY: test-backends-lanes test-manifest-strict test-mork-lane-core-body test-mork-add-atoms-runtime-stats-body test-mork-bridge-contextual-exact-rows probe-core-lane probe-pathmap-lane probe-pathmap-lane-body
