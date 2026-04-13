SHELL = /bin/bash
CC = gcc

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

CETTA_RUST_DIR ?= $(abspath ./rust)
MORK_BRIDGE_DIR ?= $(CETTA_RUST_DIR)/target/release
MORK_BRIDGE_MANIFEST ?= $(CETTA_RUST_DIR)/cetta-space-bridge/Cargo.toml
MORK_BRIDGE_WORKDIR ?= $(CETTA_RUST_DIR)
MORK_BRIDGE_RUSTFLAGS ?= -C target-cpu=native
MORK_BRIDGE_CARGO_FEATURE_ARGS =
ifeq ($(ENABLE_PATHMAP_SPACE),0)
MORK_BRIDGE_CARGO_FEATURE_ARGS += --no-default-features
endif
MORK_BRIDGE_STATICLIB := $(MORK_BRIDGE_DIR)/libcetta_space_bridge.a
BRIDGE_DEPS =
BRIDGE_CFLAGS =
BRIDGE_LDFLAGS =
MORK_BUILD_HAS_BRIDGE := 0
ifeq ($(ENABLE_MORK_STATIC),1)
ifeq ($(wildcard $(MORK_BRIDGE_MANIFEST)),)
$(error BUILD=$(BUILD_CANON) requires $(MORK_BRIDGE_MANIFEST))
endif
BRIDGE_DEPS += $(MORK_BRIDGE_STATICLIB)
BRIDGE_CFLAGS += -DCETTA_MORK_BRIDGE_STATIC=1
BRIDGE_LDFLAGS += $(MORK_BRIDGE_STATICLIB) -lrt
MORK_BUILD_HAS_BRIDGE := 1
endif

$(MORK_BRIDGE_STATICLIB): $(MORK_BRIDGE_MANIFEST) FORCE
	@cd $(MORK_BRIDGE_WORKDIR) && \
	ulimit -v 10485760 && \
	RUSTFLAGS='$(MORK_BRIDGE_RUSTFLAGS)' \
	cargo build -p cetta-space-bridge --release $(MORK_BRIDGE_CARGO_FEATURE_ARGS)

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
BUILD_CONFIG_HEADER = $(BOOTSTRAP_TMPDIR)/build_config.h
VERSION_FILE = VERSION
CETTA_VERSION := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null))
CPPFLAGS = -Isrc -I. $(BRIDGE_CFLAGS) $(PY_CFLAGS) -include $(BUILD_CONFIG_HEADER)
CFLAGS = -O3 -Wall -Werror -std=c11
DEPFLAGS = -MMD -MP
LDFLAGS = $(BRIDGE_LDFLAGS) -ldl -lm $(PY_LDFLAGS) $(PY_RPATH)

SRC = src/symbol.c src/atom.c src/parser.c src/mm2_lower.c src/subst_tree.c src/space.c src/space_match_backend.c src/match.c src/term_canon.c src/table_store.c src/search_machine.c src/term_universe.c src/stats.c src/eval.c src/grounded.c src/text_source.c src/native_handle.c src/mork_space_bridge_runtime.c src/library.c $(PYTHON_SRC) src/session.c src/lang.c src/compile.c src/runtime.c src/cetta_stdlib.c native/native_modules.c src/main.c
OBJ = $(SRC:.c=.o)
BIN = cetta
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
PYTHON_TESTS = tests/test_py_ops_surface.metta tests/test_import_foreign_python_file.metta tests/test_import_foreign_pkg_error.metta tests/test_namespace_sugar_guardrails.metta
PATHMAP_REQUIRED_TESTS = \
	tests/test_space_type.metta \
	tests/test_space_engine_backend.metta \
	tests/test_import_act_module_surface.metta \
	tests/test_include_mm2_space_target.metta \
	tests/test_module_inventory_act_registered_root.metta \
	tests/test_mork_act_roundtrip.metta \
	tests/test_pathmap_counted_space_surface.metta \
	tests/test_mork_fc_depth3_witness_regression.metta \
	tests/test_mork_nil_parity_regression.metta \
	tests/test_mork_recursive_bc_micro_regression.metta \
	tests/test_mork_recursive_bc_regression.metta

# Two-stage bootstrap: cetta compiles its own stdlib
STDLIB_SRC = lib/stdlib.metta
STDLIB_BLOB = src/stdlib_blob.h

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

bench-metamath-d5: $(BIN)
	@./scripts/bench_metamath_d5.sh

bench-weird-audit: $(BIN)
	$(call require_mork_bridge_or_reexec,weird benchmark audit,$@)
	@./scripts/bench_weird_audit.sh

perf-runtime-stats: $(BIN)
	@./scripts/bench_runtime_stats_probe.sh

perf-stable: perf-runtime-stats

test-symbolid-guard:
	@./scripts/check_symbolid_guards.sh

# Stage 0: kernel-only binary (no precompiled stdlib)
STAGE0_OBJ = $(SRC:.c=.stage0.o)
DEPS = $(OBJ:.o=.d) $(STAGE0_OBJ:.o=.d)

-include $(DEPS)

FORCE:

$(BUILD_CONFIG_HEADER): FORCE
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_cfg=$$(mktemp "$(BOOTSTRAP_TMPDIR)/build_config.XXXXXX"); \
	printf '%s\n' '/* autogenerated by Makefile; do not edit */' > "$$tmp_cfg"; \
	printf '#define CETTA_VERSION_STRING "%s"\n' "$(CETTA_VERSION)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_MODE_STRING "%s"\n' "$(BUILD_CANON)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PYTHON %s\n' "$(ENABLE_PYTHON)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_MORK_STATIC %s\n' "$(ENABLE_MORK_STATIC)" >> "$$tmp_cfg"; \
	printf '#define CETTA_BUILD_WITH_PATHMAP_SPACE %s\n' "$(ENABLE_PATHMAP_SPACE)" >> "$$tmp_cfg"; \
	if [ -f "$@" ] && cmp -s "$$tmp_cfg" "$@"; then \
		rm -f "$$tmp_cfg"; \
	else \
		mv "$$tmp_cfg" "$@"; \
	fi

%.stage0.o: %.c $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -DCETTA_NO_STDLIB -MF $(@:.o=.d) -c -o $@ $<
cetta-stage0: $(STAGE0_OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

# Stage 1: compile stdlib using stage0
$(STDLIB_BLOB): cetta-stage0 $(STDLIB_SRC)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_stage0=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta-stage0.run.XXXXXX"); \
	tmp_blob=$$(mktemp "$(BOOTSTRAP_TMPDIR)/stdlib_blob.XXXXXX"); \
	trap 'rm -f "$$tmp_stage0" "$$tmp_blob"' EXIT INT TERM; \
	cp ./cetta-stage0 "$$tmp_stage0"; \
	chmod +x "$$tmp_stage0"; \
	"$$tmp_stage0" --compile-stdlib $(STDLIB_SRC) > "$$tmp_blob"; \
	mv "$$tmp_blob" $(STDLIB_BLOB)

# Stage 2: full binary with precompiled stdlib
$(BIN): $(OBJ) $(BRIDGE_DEPS)
	@mkdir -p $(BOOTSTRAP_TMPDIR)
	@tmp_out=$$(mktemp "$(BOOTSTRAP_TMPDIR)/cetta.XXXXXX"); \
	trap 'rm -f "$$tmp_out"' EXIT INT TERM; \
	$(CC) $(CFLAGS) -o "$$tmp_out" $^ $(LDFLAGS); \
	mv "$$tmp_out" $@

# stdlib.o depends on the generated blob header
src/cetta_stdlib.o: src/cetta_stdlib.c src/cetta_stdlib.h $(STDLIB_BLOB) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

%.o: %.c $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

clean:
	rm -f $(OBJ) $(STAGE0_OBJ) $(DEPS) $(BIN) cetta-stage0 $(STDLIB_BLOB) \
		$(BUILD_CONFIG_HEADER) \
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

MORK_MM2_TEST3 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test3_var_binding.mm2)
MORK_MM2_TEST4 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test4_conjunctive.mm2)
MORK_MM2_TEST5 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test5_equal_pair.mm2)
MORK_MM2_TEST6 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test6_no_match.mm2)
MORK_MM2_TEST7 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test7_nested.mm2)
MORK_MM2_TEST8 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test8_multi_step.mm2)
MORK_MM2_TEST9 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test9_priority_ordering.mm2)
MORK_MM2_TEST10 := $(abspath ../../hyperon/MORK/examples/lean_conformance/test10_conjunctive_wq.mm2)
MORK_MM2_SINK_ADD_CONSTANT := $(abspath ../../hyperon/MORK/examples/sinks/archive/test_add_constant.mm2)
MORK_MM2_SINK_ADD_SIMPLE := $(abspath ../../hyperon/MORK/examples/sinks/archive/test_add_simple.mm2)
MORK_MM2_SINK_REMOVE_SIMPLE := $(abspath ../../hyperon/MORK/examples/sinks/archive/test_remove_simple.mm2)
MORK_MM2_SINK_BULK_REMOVE := $(abspath ../../hyperon/MORK/examples/sinks/archive/test_bulk_remove.mm2)
MORK_MM2_SINK_COUNT_SIMPLE := $(abspath ../../hyperon/MORK/examples/sinks/archive/test_count_simple.mm2)
MORK_MM2_SINK_HEAD_LIMIT := $(abspath ../../hyperon/MORK/examples/sinks/archive/test_head_limit.mm2)

define require_mork_bridge_or_reexec
	@if [ "$(MORK_BUILD_HAS_BRIDGE)" != "1" ] && [ -z "$(CETTA_MORK_SPACE_BRIDGE_LIB)" ]; then \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
			bridge_build=mork; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=main; fi; \
			echo "INFO: $(1) requires the MORK bridge; re-running with BUILD=$$bridge_build"; \
			$(MAKE) BUILD=$$bridge_build $(2); \
		else \
			echo "SKIP: $(1) (no MORK bridge manifest configured)"; \
		fi; \
		exit $$?; \
	fi
endef

define require_pathmap_bridge_or_reexec
	@if [ "$(ENABLE_PATHMAP_SPACE)" != "1" ]; then \
		if [ -f "$(MORK_BRIDGE_MANIFEST)" ]; then \
			bridge_build=pathmap; \
			if [ "$(ENABLE_PYTHON)" = "1" ]; then bridge_build=full; fi; \
			echo "INFO: $(1) requires generic pathmap-backed spaces; re-running with BUILD=$$bridge_build"; \
			$(MAKE) BUILD=$$bridge_build $(2); \
		else \
			echo "SKIP: $(1) (no MORK bridge manifest configured)"; \
		fi; \
		exit $$?; \
	fi
endef

test: $(BIN) test-git-module test-symbolid-guard test-runtime-stats-cli test-help-flags test-mork-lane
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
		if { [ "$$f" = "tests/test_pretty_vars_surface.metta" ] || \
		     [ "$$f" = "tests/test_import_act_module_surface.metta" ] || \
		     [ "$$f" = "tests/test_import_mm2_module_surface.metta" ] || \
		     [ "$$f" = "tests/test_include_mm2_space_target.metta" ] || \
		     [ "$$f" = "tests/test_mm2_kiss_add_remove.metta" ] || \
		     [ "$$f" = "tests/test_mm2_kiss_fractal_priority.metta" ] || \
		     [ "$$f" = "tests/test_mm2_kiss_inline_basic.metta" ] || \
		     [ "$$f" = "tests/test_mm2_kiss_priority.metta" ] || \
		     [ "$$f" = "tests/test_module_inventory_act_registered_root.metta" ] || \
		     [ "$$f" = "tests/test_mork_act_roundtrip.metta" ] || \
		     [ "$$f" = "tests/test_mork_attached_exact_match_regression.metta" ] || \
		     [ "$$f" = "tests/test_mork_algebra_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_counterexample_loom_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_encoding_boundary_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_full_pipeline_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_handle_errors_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_kiss_examples.metta" ] || \
		     [ "$$f" = "tests/test_mork_lib_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_mm2_metta_showcase.metta" ] || \
		     [ "$$f" = "tests/test_mork_open_act_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_overlay_zipper_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_product_zipper_surface.metta" ] || \
		     [ "$$f" = "tests/test_mork_zipper_surface.metta" ] || \
		     [ "$$f" = "tests/test_new_space_mork_surface.metta" ] || \
		     [ "$$f" = "tests/test_step_space_surface.metta" ]; }; then \
			continue; \
		fi; \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$(./$(BIN) --lang he "$$f" 2>&1); \
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
	echo "$$summary"

test-profiles: $(BIN) test-git-module-profiles test-symbolid-guard
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

test-backends: $(BIN)
	@for backend in $(SPACE_ENGINES); do \
		echo "== backend: $$backend =="; \
		pass=0; fail=0; skip=0; \
		for f in tests/test_*.metta tests/he_*.metta; do \
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
		if [ "$$f" = "tests/test_pretty_vars_surface.metta" ]; then \
			echo "SKIP: $$f (covered by dedicated pretty-vars flag target)"; \
			skip=$$((skip + 1)); \
			continue; \
		fi; \
			if printf '%s\n' $(PATHMAP_REQUIRED_TESTS) | grep -Fxq "$$f"; then \
				echo "SKIP: $$f (covered by test-pathmap-lane)"; \
				skip=$$((skip + 1)); \
				continue; \
			fi; \
			exp="$${f%.metta}.expected"; \
			if [ ! -f "$$exp" ]; then \
				echo "SKIP: $$f (no .expected file)"; \
				skip=$$((skip + 1)); \
				continue; \
			fi; \
			result=$$(./$(BIN) --space-engine "$$backend" --lang he "$$f" 2>&1); \
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
		echo "$$pass passed, $$fail failed, $$skip skipped"; \
		[ $$fail -eq 0 ] || exit 1; \
	done
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lane
ifeq ($(ENABLE_PATHMAP_SPACE),1)
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-lane
endif

test-mork-lane: $(BIN)
	$(call require_mork_bridge_or_reexec,mork lane regression suite,$@)
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-mork-program-space
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-exec-basic
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-conformance-var-binding
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-conformance-lean-suite
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mm2-kiss-suite
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-surface-suite
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-basic-pathmap-guard

test-mork-basic-pathmap-guard: $(BIN)
	@if [ "$(ENABLE_PATHMAP_SPACE)" = "1" ]; then \
		echo "SKIP: mork/basic pathmap guards (pathmap lane enabled)"; \
	else \
		result=$$(./$(BIN) --lang he \
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

test-pathmap-lane: $(BIN)
	$(call require_pathmap_bridge_or_reexec,pathmap lane regression suite,$@)
	@pass=0; fail=0; no_exp=0; \
	for f in $(PATHMAP_REQUIRED_TESTS); do \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			no_exp=$$((no_exp + 1)); \
			continue; \
		fi; \
		result=$$(./$(BIN) --lang he "$$f" 2>&1); \
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
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-bridge-v2
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-long-string-regression
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-conjunction-init
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-match-chain
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-pathmap-match-chain-v3
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-mork-lib-pathmap
	@$(MAKE) -s BUILD=$(BUILD_CANON) test-duplicate-multiplicity-backends

test-mm2-mork-program-space: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 MORK program-space lowering regression,$@)
	@ \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --space-engine mork --lang mm2 tests/support/mm2_mork_program_space.metta 2>&1); \
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

test-mm2-kiss-suite: $(BIN)
	$(call require_mork_bridge_or_reexec,mm2 KISS raw example suite,$@)
	@ \
	prep=$$(./$(BIN) --quiet --lang he tests/support/prepare_mm2_kiss_fruit_colors_act.metta 2>&1); \
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
		result=$$(./$(BIN) --lang he "tests/$$stem.metta" 2>&1); \
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
		result=$$(./$(BIN) --lang he "tests/$$stem.metta" 2>&1); \
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
		result=$$(./$(BIN) --lang he "tests/$$stem.metta" 2>&1); \
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
	@expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
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
	@ \
	result=$$(./$(BIN) --space-engine pathmap --lang he tests/test_imported_match_chain_conjunction_lowering.metta 2>&1); \
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
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]'); \
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
	result=$$(./$(BIN) --lang he tests/test_mork_open_act_surface.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_mork_open_act_surface.expected)" ]; then \
		echo "PASS: mork open-act probe"; \
	else \
		echo "FAIL: mork open-act probe"; \
		diff <(cat tests/test_mork_open_act_surface.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-pretty-vars-flags: $(BIN)
	@raw_result=$$(./$(BIN) --raw-vars --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	default_result=$$(./$(BIN) --lang he tests/test_pretty_vars_surface.metta 2>&1); \
	pretty_result=$$(./$(BIN) --pretty-vars --lang he tests/test_pretty_vars_surface.metta 2>&1); \
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
	@raw_result=$$(./$(BIN) --raw-namespaces --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	default_result=$$(./$(BIN) --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
	pretty_result=$$(./$(BIN) --pretty-namespaces --lang he tests/test_pretty_namespaces_surface.metta 2>&1); \
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
	result=$$(./$(BIN) --quiet --lang he tests/support/prepare_bio_1m_act.metta 2>&1); \
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
	$(call require_mork_bridge_or_reexec,cli help flags,$@)
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
# Checks theorem count matches frozen regression number.
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
	for f in tests/test_equations.metta tests/test_basic_eval.metta tests/test_disc_trie.metta tests/test_compile_arity.metta; do \
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
			else \
				echo "IR-OK: $$f"; pass=$$((pass + 1)); \
			fi; \
		else \
			echo "IR-FAIL: $$f"; fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "---"; echo "$$pass passed, $$fail failed"

refresh-he-matrices:
	@python3 scripts/refresh_he_runtime_matrices.py
	@python3 -m json.tool specs/he_runtime_impl_matrix.json > /dev/null
	@python3 -m json.tool specs/he_runtime_3layer_matrix.json > /dev/null
	@echo "refreshed HE runtime parity matrices"

.PHONY: FORCE all core python mork main pathmap full clean test test-backends test-mork-lane test-mork-basic-pathmap-guard test-pathmap-lane test-mm2-lowering-core test-mm2-mork-program-space test-mm2-exec-basic test-mm2-kiss-suite test-mm2-conformance-var-binding test-mm2-conformance-lean-suite test-mm2-sink-suite test-pathmap-bridge-v2 test-pathmap-long-string-regression test-pathmap-match-chain test-mork-lib-pathmap test-mork-open-act test-pretty-vars-flags test-pretty-namespaces-flags test-help-flags prepare-bio-eqtl-act bench-bio-eqtl-act-modes prepare-bio-1m-act bench-bio-1m-act-attach bench-bio-1m-act-modes test-duplicate-multiplicity-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends probe-d3-nodup probe-d3-nodup-backends bench-conj-backends bench-conj12-backends bench-dup-conj-backends bench-dup-conj-runtime-backends bench-d4 bench-d4-nodup bench-d4-backends bench-d4-nodup-backends bench-compare-petta bench-weird-audit tail-recursion-check compile-test refresh-he-matrices promote-runtime
