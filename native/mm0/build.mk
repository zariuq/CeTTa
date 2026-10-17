NATIVE_MM0_CORE_SRC = $(addprefix native/mm0/,mm0_base.c mm0_mmb.c mm0_spec.c mm0_semantic.c)
SRC += $(NATIVE_MM0_CORE_SRC) native/mm0/mm0_cetta_module.c

NATIVE_MM0_TEST_DIR ?= runtime/native-mm0
NATIVE_MM0_TEST_CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror
NATIVE_MM0_SANITIZE_CFLAGS ?= -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
NATIVE_MM0_HEADERS = $(wildcard native/mm0/*.h)
NATIVE_MM0_FIXTURES ?= tests/mm0/fixtures

$(NATIVE_MM0_TEST_DIR):
	mkdir -p $@

$(NATIVE_MM0_TEST_DIR)/mm0_check: tests/mm0/mm0_check.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_TEST_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) -o $@

$(NATIVE_MM0_TEST_DIR)/test_mm0: tests/mm0/test_mm0.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_TEST_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) -o $@

$(NATIVE_MM0_TEST_DIR)/test_mm0_sanitized: tests/mm0/test_mm0.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_SANITIZE_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) -o $@

$(NATIVE_MM0_TEST_DIR)/mm0_check_sanitized: tests/mm0/mm0_check.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_SANITIZE_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) -o $@

# The repaired boundaries: ownership of conversions and expressions, step
# charging of every row and witness, atomic admission under exhaustion.
$(NATIVE_MM0_TEST_DIR)/test_boundary: tests/mm0/test_boundary.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_TEST_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) -o $@

$(NATIVE_MM0_TEST_DIR)/test_boundary_sanitized: tests/mm0/test_boundary.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_SANITIZE_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) -o $@

# Helpers of tests/mm0/bench.py: a small launcher measuring one process, and
# an allocation counter for LD_PRELOAD (glibc).
$(NATIVE_MM0_TEST_DIR)/measure: tests/mm0/measure.c | $(NATIVE_MM0_TEST_DIR)
	$(CC) -O2 -std=c11 -Wall -Wextra $< -o $@

$(NATIVE_MM0_TEST_DIR)/alloc_count.so: tests/mm0/alloc_count.c | $(NATIVE_MM0_TEST_DIR)
	$(CC) -O2 -shared -fPIC $< -o $@

.PHONY: native-mm0-bench-tools
native-mm0-bench-tools: $(NATIVE_MM0_TEST_DIR)/mm0_check $(NATIVE_MM0_TEST_DIR)/measure $(NATIVE_MM0_TEST_DIR)/alloc_count.so

# Every allocation of the core can be made to fail (tests/mm0/test_alloc_failure.c).
$(NATIVE_MM0_TEST_DIR)/test_alloc_failure: tests/mm0/test_alloc_failure.c $(NATIVE_MM0_CORE_SRC) $(NATIVE_MM0_HEADERS) | $(NATIVE_MM0_TEST_DIR)
	$(CC) $(NATIVE_MM0_SANITIZE_CFLAGS) -Inative/mm0 $< $(NATIVE_MM0_CORE_SRC) \
		-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@

# The core alone: corpus loading, the semantic route, admission, and the
# MMB and specification controls (tests/mm0/test_mmb.py).
.PHONY: test-native-mm0-core test-native-mm0-sanitized test-native-mm0-alloc test-native-mm0-host \
	test-native-mm0-mutation
test-native-mm0-core: $(NATIVE_MM0_TEST_DIR)/test_mm0 $(NATIVE_MM0_TEST_DIR)/mm0_check \
		$(NATIVE_MM0_TEST_DIR)/test_boundary
	$(NATIVE_MM0_TEST_DIR)/test_mm0 $(NATIVE_MM0_FIXTURES)/hol.mmb $(NATIVE_MM0_FIXTURES)/hol.mm0
	$(NATIVE_MM0_TEST_DIR)/test_boundary
	python3 tests/mm0/test_mmb.py $(NATIVE_MM0_TEST_DIR)/mm0_check
	python3 tests/mm0/test_dummy_identity.py $(NATIVE_MM0_TEST_DIR)/mm0_check

test-native-mm0-sanitized: $(NATIVE_MM0_TEST_DIR)/test_mm0_sanitized $(NATIVE_MM0_TEST_DIR)/mm0_check_sanitized \
		$(NATIVE_MM0_TEST_DIR)/test_boundary_sanitized
	$(NATIVE_MM0_TEST_DIR)/test_mm0_sanitized $(NATIVE_MM0_FIXTURES)/hol.mmb $(NATIVE_MM0_FIXTURES)/hol.mm0
	$(NATIVE_MM0_TEST_DIR)/test_boundary_sanitized
	python3 tests/mm0/test_mmb.py $(NATIVE_MM0_TEST_DIR)/mm0_check_sanitized
	python3 tests/mm0/test_dummy_identity.py $(NATIVE_MM0_TEST_DIR)/mm0_check_sanitized

# Allocation failure at every allocation of a load and of two semantic proofs.
test-native-mm0-alloc: $(NATIVE_MM0_TEST_DIR)/test_alloc_failure
	$(NATIVE_MM0_TEST_DIR)/test_alloc_failure $(NATIVE_MM0_FIXTURES)/hol.mmb $(NATIVE_MM0_FIXTURES)/hol.mm0 \
		$(NATIVE_MM0_FIXTURES)/peano.mmb $(NATIVE_MM0_FIXTURES)/peano.mm0 1

# MM0 through CeTTa's public languages: import, stored proofs, handles.
test-native-mm0-host: $(BIN) tests/mm0/test_host.py
	python3 tests/mm0/test_host.py $(abspath $(BIN))

# Reuse the qualified runtime objects; only the adapter enables fault hooks.
NATIVE_MM0_FAULT_BIN ?= runtime/cetta-mm0-faults-$(BUILD_OBJ_TAG)
NATIVE_MM0_ADAPTER_OBJ = native/mm0/mm0_cetta_module.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
NATIVE_MM0_FAULT_OBJ = runtime/bootstrap/mm0_module_fault.$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_RUNTIME_STATS)),.runtime-stats,).o
$(NATIVE_MM0_FAULT_OBJ): native/mm0/mm0_cetta_module.c $(NATIVE_MM0_HEADERS) src/seal.h $(BUILD_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMM0_ADAPTER_FAULTS -c $< -o $@

$(NATIVE_MM0_FAULT_BIN): $(filter-out $(NATIVE_MM0_ADAPTER_OBJ),$(OBJ)) $(NATIVE_MM0_FAULT_OBJ) $(BRIDGE_DEPS)
	$(link_cetta_binary)

.PHONY: test-native-mm0-publication
test-native-mm0-publication: $(NATIVE_MM0_FAULT_BIN)
	python3 tests/mm0/test_publication.py $(abspath $(NATIVE_MM0_FAULT_BIN))

# Qualification, not part of `make test` (minutes): each check of the
# verifier is weakened in turn and the tests above must notice.  Add
# NATIVE_MM0_MUTATION_FLAGS=--host to rebuild CeTTa for the module mutants.
test-native-mm0-mutation:
	python3 tests/mm0/mutation.py $(NATIVE_MM0_MUTATION_FLAGS)

test: test-native-mm0-core test-native-mm0-host

NATIVE_MM0_BRIDGE_SRC = $(NATIVE_MM0_CORE_SRC) native/mm0/mm0_cetta_module.c \
	src/seal.c src/native_handle.c src/atom.c src/symbol.c src/binding/frame_identity.c
NATIVE_MM0_BRIDGE_BIN = runtime/test_mm0_bridge-$(BUILD_OBJ_TAG)
$(NATIVE_MM0_BRIDGE_BIN): tests/mm0/test_bridge.c $(NATIVE_MM0_BRIDGE_SRC) $(NATIVE_MM0_HEADERS) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMM0_ADAPTER_FAULTS -DCETTA_TEST_HOOKS -Inative/mm0 \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-o $@ $< $(NATIVE_MM0_BRIDGE_SRC) $(LDFLAGS)

.PHONY: test-native-mm0-bridge
test-native-mm0-bridge: $(NATIVE_MM0_BRIDGE_BIN)
	$(NATIVE_MM0_BRIDGE_BIN)
test: test-native-mm0-bridge

$(NATIVE_MM0_BRIDGE_BIN)-sanitized: tests/mm0/test_bridge.c $(NATIVE_MM0_BRIDGE_SRC) $(NATIVE_MM0_HEADERS) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -DMM0_ADAPTER_FAULTS -DCETTA_TEST_HOOKS -Inative/mm0 \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-o $@ $< $(NATIVE_MM0_BRIDGE_SRC) $(LDFLAGS)

.PHONY: test-native-mm0-bridge-sanitized
test-native-mm0-bridge-sanitized: $(NATIVE_MM0_BRIDGE_BIN)-sanitized
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 $(NATIVE_MM0_BRIDGE_BIN)-sanitized
