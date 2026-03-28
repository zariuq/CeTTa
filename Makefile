SHELL = /bin/bash
CC = gcc
MORK_BRIDGE_DIR ?= $(abspath ../../hyperon/MORK/target/release)
MORK_BRIDGE_STATICLIB := $(MORK_BRIDGE_DIR)/libcetta_space_bridge.a
BRIDGE_DEPS =
BRIDGE_CFLAGS =
BRIDGE_LDFLAGS =
ifneq ($(wildcard $(MORK_BRIDGE_STATICLIB)),)
BRIDGE_DEPS += $(MORK_BRIDGE_STATICLIB)
BRIDGE_CFLAGS += -DCETTA_MORK_BRIDGE_STATIC=1
BRIDGE_LDFLAGS += $(MORK_BRIDGE_STATICLIB) -lrt
endif
PY_CFLAGS = $(shell python3-config --includes)
PY_LDFLAGS = $(shell python3-config --embed --ldflags)
PY_RPATH = -Wl,-rpath,$(shell python3 -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or "")')
CFLAGS = -O3 -Wall -Werror -std=c11 -Isrc -I. $(BRIDGE_CFLAGS) $(PY_CFLAGS)
DEPFLAGS = -MMD -MP
LDFLAGS = $(BRIDGE_LDFLAGS) -ldl -lm $(PY_LDFLAGS) $(PY_RPATH)

SRC = src/symbol.c src/atom.c src/parser.c src/subst_tree.c src/space.c src/space_match_backend.c src/match.c src/stats.c src/eval.c src/grounded.c src/text_source.c src/native_handle.c src/mork_space_bridge_runtime.c src/library.c src/foreign.c src/session.c src/lang.c src/compile.c src/runtime.c src/cetta_stdlib.c native/native_modules.c src/main.c
OBJ = $(SRC:.c=.o)
BIN = cetta
SPACE_MATCH_BACKENDS = native-subst-tree native-candidate-exact pathmap-imported
D4_PROBE_TIMEOUT ?= 60
GIT_TEST_FIXTURE_ROOT = $(CURDIR)/runtime/git_module_fixture
GIT_TEST_CACHE_DIR = $(CURDIR)/runtime/test-git-module-cache
GIT_TEST_URL = file://$(GIT_TEST_FIXTURE_ROOT)
GIT_TEST_DYNAMIC = $(CURDIR)/runtime/test-git-module-dynamic.metta
GIT_TEST_COMPAT_DYNAMIC = $(CURDIR)/runtime/test-git-module-compat.metta

# Two-stage bootstrap: cetta compiles its own stdlib
STDLIB_SRC = lib/stdlib.metta
STDLIB_BLOB = src/stdlib_blob.h

all: $(BIN)

bench-metamath-d5: $(BIN)
	@./scripts/bench_metamath_d5.sh

perf-runtime-stats: $(BIN)
	@./scripts/bench_runtime_stats_probe.sh

perf-stable: perf-runtime-stats

test-symbolid-guard:
	@./scripts/check_symbolid_guards.sh

# Stage 0: kernel-only binary (no precompiled stdlib)
STAGE0_OBJ = $(SRC:.c=.stage0.o)
DEPS = $(OBJ:.o=.d) $(STAGE0_OBJ:.o=.d)

-include $(DEPS)

%.stage0.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -DCETTA_NO_STDLIB -MF $(@:.o=.d) -c -o $@ $<
cetta-stage0: $(STAGE0_OBJ) $(BRIDGE_DEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Stage 1: compile stdlib using stage0
$(STDLIB_BLOB): cetta-stage0 $(STDLIB_SRC)
	./cetta-stage0 --compile-stdlib $(STDLIB_SRC) > $(STDLIB_BLOB)

# Stage 2: full binary with precompiled stdlib
$(BIN): $(OBJ) $(BRIDGE_DEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# stdlib.o depends on the generated blob header
src/cetta_stdlib.o: src/cetta_stdlib.c src/cetta_stdlib.h $(STDLIB_BLOB)
	$(CC) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -c -o $@ $<

clean:
	rm -f $(OBJ) $(STAGE0_OBJ) $(DEPS) $(BIN) cetta-stage0 $(STDLIB_BLOB)

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

test: $(BIN) test-git-module test-symbolid-guard
	@pass=0; fail=0; skip=0; \
	cache_dir="$(GIT_TEST_CACHE_DIR)"; mkdir -p "$$cache_dir"; export CETTA_GIT_MODULE_CACHE_DIR="$$cache_dir"; \
	for f in tests/test_*.metta tests/spec_*.metta tests/he_*.metta; do \
		[ -f "$$f" ] || continue; \
		exp="$${f%.metta}.expected"; \
		if [ ! -f "$$exp" ]; then \
			echo "SKIP: $$f (no .expected file)"; \
			skip=$$((skip + 1)); \
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
	echo "$$pass passed, $$fail failed, $$skip skipped"

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
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_space_kind_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_space_kind_extension.expected)" ]; then \
			echo "PASS: he_extended space-kind extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended space-kind extension"; \
			diff <(cat tests/spec_profile_space_kind_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_space_kind_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface space-kind is unavailable in profile he_compat"; then \
			echo "PASS: he_compat space-kind guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat space-kind guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_space_kind_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'space-kind' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile space-kind guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile space-kind guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_space_kind_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile space-kind"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile space-kind"; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_extended --lang he tests/spec_profile_space_match_backend_extension.metta 2>&1); \
		if [ "$$result" = "$$(cat tests/spec_profile_space_match_backend_extension.expected)" ]; then \
			echo "PASS: he_extended space-match-backend extension"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended space-match-backend extension"; \
			diff <(cat tests/spec_profile_space_match_backend_extension.expected) <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
		result=$$(./$(BIN) --profile he_compat --lang he tests/spec_profile_space_match_backend_extension.metta 2>&1); \
		if printf '%s\n' "$$result" | grep -Fq "surface space-match-backend is unavailable in profile he_compat"; then \
			echo "PASS: he_compat space-match-backend guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat space-match-backend guard"; \
			printf '%s\n' "$$result"; \
			fail=$$((fail + 1)); \
		fi; \
		compile_output=$$(./$(BIN) --profile he_compat --compile tests/support/profile_compile_space_match_backend_extension.metta 2>&1 >/dev/null); \
		status=$$?; \
		if [ $$status -ne 0 ] && printf '%s\n' "$$compile_output" | grep -Fq "surface 'space-match-backend' is unavailable in profile 'he_compat'"; then \
			echo "PASS: he_compat compile space-match-backend guard"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_compat compile space-match-backend guard"; \
			printf '%s\n' "$$compile_output"; \
			fail=$$((fail + 1)); \
		fi; \
		if ./$(BIN) --profile he_extended --compile tests/support/profile_compile_space_match_backend_extension.metta >/dev/null 2>&1; then \
			echo "PASS: he_extended compile space-match-backend"; pass=$$((pass + 1)); \
		else \
			echo "FAIL: he_extended compile space-match-backend"; \
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
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		echo "== backend: $$backend =="; \
		pass=0; fail=0; skip=0; \
		for f in tests/test_*.metta tests/he_*.metta; do \
			[ -f "$$f" ] || continue; \
			exp="$${f%.metta}.expected"; \
			if [ ! -f "$$exp" ]; then \
				echo "SKIP: $$f (no .expected file)"; \
				skip=$$((skip + 1)); \
				continue; \
			fi; \
			result=$$(./$(BIN) --space-match-backend "$$backend" --lang he "$$f" 2>&1); \
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
	@$(MAKE) -s test-pathmap-imported-bridge-v2
	@$(MAKE) -s test-pathmap-imported-match-chain
	@$(MAKE) -s test-mork-lib-pathmap-imported

test-pathmap-imported-bridge-v2: $(BIN)
	@if [ ! -f "$(MORK_BRIDGE_STATICLIB)" ] && [ -z "$$CETTA_MORK_SPACE_BRIDGE_LIB" ]; then \
		echo "SKIP: pathmap-imported bridge v2 regression (no MORK bridge library configured)"; \
		exit 0; \
	fi; \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --profile he_extended --space-match-backend pathmap-imported --lang he tests/test_pathmap_imported_bridge_v2.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: pathmap-imported bridge v2 regression"; \
	else \
		echo "FAIL: pathmap-imported bridge v2 regression"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
			exit 1; \
		fi

test-pathmap-imported-match-chain: $(BIN)
	@if [ ! -f "$(MORK_BRIDGE_STATICLIB)" ] && [ -z "$$CETTA_MORK_SPACE_BRIDGE_LIB" ]; then \
		echo "SKIP: pathmap-imported nested-match chain regression (no MORK bridge library configured)"; \
		exit 0; \
	fi; \
	result=$$(./$(BIN) --space-match-backend pathmap-imported --lang he tests/test_match_chain_imported_regression.metta 2>&1); \
	if [ "$$result" = "$$(cat tests/test_match_chain_imported_regression.expected)" ]; then \
		echo "PASS: pathmap-imported nested-match chain regression"; \
	else \
		echo "FAIL: pathmap-imported nested-match chain regression"; \
		diff <(cat tests/test_match_chain_imported_regression.expected) <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-mork-lib-pathmap-imported: $(BIN)
	@if [ ! -f "$(MORK_BRIDGE_STATICLIB)" ] && [ -z "$$CETTA_MORK_SPACE_BRIDGE_LIB" ]; then \
		echo "SKIP: mork lib pathmap-imported probe (no MORK bridge library configured)"; \
		exit 0; \
	fi; \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	result=$$(./$(BIN) --profile he_extended --space-match-backend pathmap-imported --lang he tests/support/mork_lib_pathmap_imported.metta 2>&1); \
	if [ "$$result" = "$$expected" ]; then \
		echo "PASS: mork lib pathmap-imported probe"; \
	else \
		echo "FAIL: mork lib pathmap-imported probe"; \
		diff <(echo "$$expected") <(echo "$$result") | head -20; \
		exit 1; \
	fi

test-duplicate-multiplicity-backends: $(BIN)
	@if [ ! -f "$(MORK_BRIDGE_STATICLIB)" ] && [ -z "$$CETTA_MORK_SPACE_BRIDGE_LIB" ]; then \
		echo "SKIP: duplicate multiplicity backend probe (no MORK bridge library configured)"; \
		exit 0; \
	fi; \
	expected=$$(printf '%s\n' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]' '[()]'); \
	for backend in native-subst-tree native-candidate-exact pathmap-imported; do \
		result=$$(./$(BIN) --profile he_extended --space-match-backend "$$backend" --lang he tests/support/duplicate_multiplicity_probe.metta 2>&1); \
		if [ "$$result" = "$$expected" ]; then \
			echo "PASS: $$backend duplicate multiplicity probe"; \
		else \
			echo "FAIL: $$backend duplicate multiplicity probe"; \
			diff <(echo "$$expected") <(echo "$$result") | head -20; \
			exit 1; \
		fi; \
	done

# Slow: regenerate .expected files from HE CLI oracle.
# Run ONE AT A TIME to avoid OOM. Requires conda hyperon env.
oracle-refresh:
	@for f in tests/test_*.metta tests/he_*.metta; do \
		[ -f "$$f" ] || continue; \
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
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/nil_pc_fc_d3.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 total: $$count theorems"; \
		if [ "$$count" = "3421" ]; then \
			echo "PASS: $$backend theorem count matches"; \
		else \
			echo "FAIL: expected 3421, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-d3-nodup: $(BIN)
	@count=$$(./$(BIN) --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
	echo "depth-3 nodup total: $$count theorems"; \
	if [ "$$count" = "3268" ]; then \
		echo "PASS: nodup theorem count matches"; \
	else \
		echo "FAIL: expected 3268, got $$count"; exit 1; \
	fi

bench-d3-nodup-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/nil_pc_fc_d3_nodup.metta 2>&1 | tail -1); \
		echo "$$backend depth-3 nodup total: $$count theorems"; \
		if [ "$$count" = "3268" ]; then \
			echo "PASS: $$backend nodup theorem count matches"; \
		else \
			echo "FAIL: expected 3268, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-conj-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/bench_conjunction_he.metta 2>&1 | tail -1); \
		echo "$$backend conjunction total: $$count results"; \
		if [ "$$count" = "216" ]; then \
			echo "PASS: $$backend conjunction count matches"; \
		else \
			echo "FAIL: expected 216, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-conj12-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/bench_conjunction12_he.metta 2>&1 | tail -1); \
		echo "$$backend conjunction12 total: $$count results"; \
		if [ "$$count" = "20736" ]; then \
			echo "PASS: $$backend conjunction12 count matches"; \
		else \
			echo "FAIL: expected 20736, got $$count for $$backend"; exit 1; \
		fi; \
	done

bench-join8-backends: $(BIN)
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		count=$$(./$(BIN) --space-match-backend "$$backend" --count-only tests/bench_matchjoin8_he.metta 2>&1 | tail -1); \
		echo "$$backend join8 total: $$count results"; \
		if [ "$$count" = "4096" ]; then \
			echo "PASS: $$backend join8 count matches"; \
		else \
			echo "FAIL: expected 4096, got $$count for $$backend"; exit 1; \
		fi; \
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
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		out=$$(ulimit -v 6291456; timeout $(D4_PROBE_TIMEOUT) ./$(BIN) --space-match-backend "$$backend" --count-only tests/nil_pc_fc_d4.metta 2>&1); \
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
	@for backend in $(SPACE_MATCH_BACKENDS); do \
		out=$$(ulimit -v 6291456; timeout $(D4_PROBE_TIMEOUT) ./$(BIN) --space-match-backend "$$backend" --count-only tests/nil_pc_fc_d4_nodup.metta 2>&1); \
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

.PHONY: all clean test test-backends test-pathmap-imported-bridge-v2 test-pathmap-imported-match-chain test-mork-lib-pathmap-imported test-duplicate-multiplicity-backends oracle-refresh bench-d3 bench-d3-backends bench-d3-nodup bench-d3-nodup-backends bench-conj-backends bench-conj12-backends bench-d4 bench-d4-nodup bench-d4-backends bench-d4-nodup-backends bench-compare-petta tail-recursion-check compile-test refresh-he-matrices promote-runtime
