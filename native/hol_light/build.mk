HOL_LIGHT_CORE_SRC = $(addprefix native/hol_light/,hol_core.c hol_env.c hol_gc.c hol_api.c hol_print.c hol_export.c hol_stream.c)
SRC += $(HOL_LIGHT_CORE_SRC) native/hol_light/hol_cetta_module.c native/hol_light/hol_cli.c

HOL_LIGHT_TEST_DIR ?= runtime/hol-light
HOL_LIGHT_TEST_CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror
HOL_LIGHT_HEADERS = $(wildcard native/hol_light/*.h) native/hol_light/hol_std_data.inc
HOL_LIGHT_TEST_BINS = $(addprefix $(HOL_LIGHT_TEST_DIR)/,test_kernel test_stream test_boundaries)

$(HOL_LIGHT_TEST_DIR):
	mkdir -p $@

$(HOL_LIGHT_TEST_DIR)/%: tests/hol_light/%.c tests/hol_light/test_util.h $(HOL_LIGHT_CORE_SRC) $(HOL_LIGHT_HEADERS) | $(HOL_LIGHT_TEST_DIR)
	$(CC) $(HOL_LIGHT_TEST_CFLAGS) -D_POSIX_C_SOURCE=200809L -Inative/hol_light $< $(HOL_LIGHT_CORE_SRC) -lpthread -o $@

.PHONY: test-hol-light-kernel test-hol-light-host
test-hol-light-kernel: $(HOL_LIGHT_TEST_BINS)
	$(HOL_LIGHT_TEST_DIR)/test_kernel
	$(HOL_LIGHT_TEST_DIR)/test_stream
	$(HOL_LIGHT_TEST_DIR)/test_boundaries

test-hol-light-host: $(BIN) tests/hol_light/test_host.py
	python3 tests/hol_light/test_host.py $(abspath $(BIN))

HOL_LIGHT_BRIDGE_TEST_BIN = runtime/test_hol_bridge-$(BUILD_OBJ_TAG)
HOL_LIGHT_BRIDGE_SRC = native/hol_light/hol_cetta_module.c native/hol_light/hol_cli.c \
	src/native_handle.c src/atom.c src/symbol.c src/binding/frame_identity.c
$(HOL_LIGHT_BRIDGE_TEST_BIN): tests/hol_light/test_bridge.c tests/hol_light/test_util.h $(HOL_LIGHT_CORE_SRC) $(HOL_LIGHT_BRIDGE_SRC) $(HOL_LIGHT_HEADERS) $(BUILD_CONFIG_HEADER)
	@mkdir -p runtime
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCETTA_NATIVE_HANDLE_TEST_HOOKS -Inative/hol_light -ffunction-sections -fdata-sections -Wl,--gc-sections \
		-o $@ $< $(HOL_LIGHT_CORE_SRC) $(HOL_LIGHT_BRIDGE_SRC) $(LDFLAGS)

.PHONY: test-hol-light-bridge
test-hol-light-bridge: $(HOL_LIGHT_BRIDGE_TEST_BIN)
	$(HOL_LIGHT_BRIDGE_TEST_BIN)

test: test-hol-light-kernel test-hol-light-host test-hol-light-bridge

$(HOL_LIGHT_TEST_DIR)/bench_kernel: benchmarks/hol_light/bench_kernel.c $(HOL_LIGHT_CORE_SRC) $(HOL_LIGHT_HEADERS) | $(HOL_LIGHT_TEST_DIR)
	$(CC) $(HOL_LIGHT_TEST_CFLAGS) -D_POSIX_C_SOURCE=200809L -Inative/hol_light $< $(HOL_LIGHT_CORE_SRC) -lpthread -o $@

$(HOL_LIGHT_TEST_DIR)/gen_streams: benchmarks/hol_light/gen_streams.c $(HOL_LIGHT_CORE_SRC) $(HOL_LIGHT_HEADERS) | $(HOL_LIGHT_TEST_DIR)
	$(CC) $(HOL_LIGHT_TEST_CFLAGS) -D_POSIX_C_SOURCE=200809L -Inative/hol_light $< $(HOL_LIGHT_CORE_SRC) -lpthread -o $@

.PHONY: bench-hol-light
bench-hol-light: $(HOL_LIGHT_TEST_DIR)/bench_kernel
	$(HOL_LIGHT_TEST_DIR)/bench_kernel

$(HOL_LIGHT_TEST_DIR)/bench_lifetimes: benchmarks/hol_light/bench_lifetimes.c $(HOL_LIGHT_CORE_SRC) $(HOL_LIGHT_HEADERS) | $(HOL_LIGHT_TEST_DIR)
	$(CC) $(HOL_LIGHT_TEST_CFLAGS) -D_POSIX_C_SOURCE=200809L -Inative/hol_light $< $(HOL_LIGHT_CORE_SRC) -lpthread -o $@

.PHONY: bench-hol-light-lifetimes
bench-hol-light-lifetimes: $(HOL_LIGHT_TEST_DIR)/bench_lifetimes
	$(HOL_LIGHT_TEST_DIR)/bench_lifetimes check 1000000
	$(HOL_LIGHT_TEST_DIR)/bench_lifetimes record 1000000
