# Shared scoped matching and rewrite components. The vendored source package
# is self-contained; a normal build does not consult another checkout.
KERNEL_VENDOR = vendor/tcalc-native
KERNEL_BUILD = runtime/native-kernel-$(BUILD_OBJ_TAG)$(if $(filter 1,$(ENABLE_SANITIZERS)),-sanitized,)
KERNEL_LIBRARY = $(KERNEL_BUILD)/libnative_match.a
KERNEL_COMPONENT_OBJ = $(KERNEL_BUILD)/native_components.o
KERNEL_WORD_IMAGE = $(KERNEL_BUILD)/native_word_image.o
KERNEL_CONSTRUCTOR_IMAGE = $(KERNEL_BUILD)/native_constructor_image.o
KERNEL_BATCH_IMAGE = $(KERNEL_BUILD)/native_batch_image.o
KERNEL_OBSERVE_IMAGE = $(KERNEL_BUILD)/native_observe_image.o
KERNEL_PAIR_IMAGE = $(KERNEL_BUILD)/native_pair_image.o
KERNEL_TCUNIFY_IMAGE = $(KERNEL_BUILD)/native_tcunify_image.o
KERNEL_TCBUILD_IMAGE = $(KERNEL_BUILD)/native_tcbuild_image.o
KERNEL_TCALC_IMAGES = $(KERNEL_TCUNIFY_IMAGE) $(KERNEL_TCBUILD_IMAGE)
KERNEL_INPUTS = $(shell find $(KERNEL_VENDOR) -type f)
KERNEL_CFLAGS = -O2 -std=c11 -Wall -Wextra -Werror
KERNEL_PRIVATE_INCLUDES = -I$(KERNEL_VENDOR)/native/p1 -I$(KERNEL_VENDOR)/tisa
native/kernel/pair.%.o native/kernel/observe.%.o native/kernel/constructor.%.o native/kernel/tcalc_native.%.o: private CFLAGS += $(KERNEL_PRIVATE_INCLUDES)
ifeq ($(ENABLE_SANITIZERS),1)
KERNEL_CFLAGS = -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
endif
SRC += native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_native.c
OBJ += $(KERNEL_COMPONENT_OBJ) $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE)
OBJ += $(KERNEL_BATCH_IMAGE)

$(KERNEL_LIBRARY): $(KERNEL_INPUTS)
	@python3 tools/check_native_kernel_package.py $(KERNEL_VENDOR) --components native/kernel/IMAGES.json
	@$(MAKE) -s -C $(KERNEL_VENDOR)/native/matching CC="$(CESTA_NATIVE_CC)" \
		CFLAGS="$(KERNEL_CFLAGS)" DEVELOPMENT_REFERENCE=0 BUILD="$(abspath $(KERNEL_BUILD))" "$(abspath $@)"

$(KERNEL_COMPONENT_OBJ): $(KERNEL_LIBRARY)
	$(CESTA_NATIVE_CC) -r -nostdlib -Wl,--whole-archive $< -Wl,--no-whole-archive -o $@

$(KERNEL_TCUNIFY_IMAGE): native/kernel/native_tcunify.S | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_TCBUILD_IMAGE): native/kernel/native_tcbuild.S | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_PAIR_IMAGE): native/kernel/native_pair.S | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_OBSERVE_IMAGE): native/kernel/native_observe.S | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_CONSTRUCTOR_IMAGE): native/kernel/native_constructor.S | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_WORD_IMAGE): native/kernel/native_word.S | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_BATCH_IMAGE): native/kernel/native_batch.S native/kernel/batch.h native/kernel/native_batch_expected.inc | $(KERNEL_BUILD)
	$(CESTA_NATIVE_CC) -c $< -o $@
$(KERNEL_BUILD):
	mkdir -p $@

.PHONY: check-native-kernel-package test-native-kernel-session
check-native-kernel-package:
	@python3 tools/check_native_kernel_package.py $(KERNEL_VENDOR) --components native/kernel/IMAGES.json

$(KERNEL_BUILD)/test_session: tests/test_native_kernel_session.c native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/kernel.h native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c $< $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
test-native-kernel-session: $(KERNEL_BUILD)/test_session
	@$<

$(KERNEL_BUILD)/test_decide: tests/tcalc/tcalc_kernel_decide_test.c tests/tcalc/tcalc_decide_test.c $(TCALC_CORE) $(TCALC_UNIT) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_bridge.h native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -D_POSIX_C_SOURCE=200809L -Inative/tcalc \
		tests/tcalc/tcalc_kernel_decide_test.c $(TCALC_CORE) native/tcalc/tcalc_syntax.c \
		native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-kernel-decide
test-native-kernel-decide: $(KERNEL_BUILD)/test_decide
	@set -e; for mode in c x86 hybrid; do "$<" "$$mode"; done

$(KERNEL_BUILD)/test_tcalc_unify: tests/tcalc/tcalc_native_unify_test.c $(TCALC_CORE) $(TCALC_UNIT) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_bridge.h native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -D_POSIX_C_SOURCE=200809L -Inative/tcalc \
		tests/tcalc/tcalc_native_unify_test.c native/tcalc/tcalc_syntax.c \
		native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-tcalc-unify
test-native-tcalc-unify: $(KERNEL_BUILD)/test_tcalc_unify
	@"$<" 20000 1 && "$<" 20000 2

$(KERNEL_BUILD)/test_resident_failures: tests/tcalc/tcalc_kernel_decide_test.c tests/tcalc/tcalc_decide_test.c $(TCALC_CORE) $(TCALC_UNIT) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_bridge.h native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -D_POSIX_C_SOURCE=200809L -DCETTA_BATCH_ALLOCATION_TEST=1 -Inative/tcalc \
		tests/tcalc/tcalc_kernel_decide_test.c $(TCALC_CORE) native/tcalc/tcalc_syntax.c \
		native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@
.PHONY: test-native-kernel-resident-failures
test-native-kernel-resident-failures: $(KERNEL_BUILD)/test_resident_failures
	@set -e; for mode in c x86 hybrid; do "$<" "$$mode"; done

$(KERNEL_BUILD)/test_pair: tests/test_native_kernel_pair.c native/kernel/pair.c native/kernel/pair.h $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG native/kernel/pair.c native/kernel/observe.c $< $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-kernel-pair
test-native-kernel-pair: $(KERNEL_BUILD)/test_pair
	@$<

KERNEL_MATCH_SUITES = frames patterns numeric lookup
KERNEL_MATCH_TESTS = $(addprefix $(KERNEL_BUILD)/test_match_,$(KERNEL_MATCH_SUITES))
KERNEL_MATCH_SRC = src/symbol.c src/scalar_trie.c src/atom.c src/binding/frame_identity.c src/match.c src/binding/closure.c src/binding/frame_schema.c src/binding/slot_store.c src/binding/activation_view.c src/prime_need.c src/petta_semantics.c src/term_graph.c src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c
$(KERNEL_BUILD)/test_match_%: tests/kernel_match_%.c tests/native_kernel_matching_runner.h $(wildcard tests/test_match*.c tests/test_pattern_bindings.c tests/test_bindings_lookup_index.c) $(KERNEL_MATCH_SRC) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) -DCETTA_TEST_HOOKS=1 -DCETTA_RUNTIME_STATS_IMPL=1 $(CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ $< $(KERNEL_MATCH_SRC) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) $(LDFLAGS) -pthread
.PHONY: test-native-kernel-matching
test-native-kernel-matching: $(KERNEL_MATCH_TESTS)
	@set -e; for test in $(KERNEL_MATCH_TESTS); do for mode in c x86 hybrid; do "$$test" "$$mode"; done; done

$(KERNEL_BUILD)/test_observe: tests/test_native_kernel_observe.c native/kernel/observe.c native/kernel/observe.h $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG native/kernel/observe.c $< $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-kernel-observe
test-native-kernel-observe: $(KERNEL_BUILD)/test_observe
	@$<

$(KERNEL_BUILD)/test_reference: tests/test_native_kernel_reference.c native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/reference.h native/kernel/kernel.h native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $< $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-kernel-reference
test-native-kernel-reference: $(KERNEL_BUILD)/test_reference
	@$<

$(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE): | check-native-kernel-package

$(KERNEL_BUILD)/test_transform_errors: tests/test_kernel_transform_errors.c src/transform_core.c $(KERNEL_MATCH_SRC) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) -DCETTA_TEST_HOOKS=1 -DCETTA_RUNTIME_STATS_IMPL=1 $(CFLAGS) -UNDEBUG -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ $< src/transform_core.c $(KERNEL_MATCH_SRC) $(LDFLAGS)
.PHONY: test-kernel-transform-errors
test-kernel-transform-errors: $(KERNEL_BUILD)/test_transform_errors
	@$<

$(KERNEL_BUILD)/test_failures: tests/test_native_kernel_failures.c native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/reference.h native/kernel/kernel.h native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $< $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -pthread -o $@
.PHONY: test-native-kernel-failures
test-native-kernel-failures: $(KERNEL_BUILD)/test_failures
	@$<

$(KERNEL_BUILD)/test_dag: tests/test_native_kernel_dag.c $(KERNEL_MATCH_SRC) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) -DCETTA_TEST_HOOKS=1 -DCETTA_RUNTIME_STATS_IMPL=1 $(CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ $< $(KERNEL_MATCH_SRC) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) $(LDFLAGS) -pthread
.PHONY: test-native-kernel-dag benchmark-native-kernel-dag
test-native-kernel-dag: $(KERNEL_BUILD)/test_dag
	@$<
benchmark-native-kernel-dag: test-native-kernel-dag
	@set -e; for run in 1 2 3; do $(KERNEL_BUILD)/test_dag --bench; done

$(KERNEL_BUILD)/test_constructor: tests/test_native_kernel_constructor.c native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/constructor.h native/kernel/reference.c native/kernel/reference.h native/kernel/kernel.h native/kernel/kernel_private.h native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $< $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-kernel-constructor
test-native-kernel-constructor: $(KERNEL_BUILD)/test_constructor
	@$<

$(KERNEL_BUILD)/test_session $(KERNEL_BUILD)/test_reference $(KERNEL_BUILD)/test_decide $(KERNEL_BUILD)/test_failures $(KERNEL_MATCH_TESTS) $(KERNEL_BUILD)/test_dag: native/kernel/kernel_private.h native/kernel/constructor.h native/kernel/reference.h

.PHONY: benchmark-native-kernel-constructor
benchmark-native-kernel-constructor: test-native-kernel-constructor
	@set -e; for run in 1 2 3; do $(KERNEL_BUILD)/test_constructor --bench; done

$(KERNEL_BUILD)/matcher_reference_leak.c: src/match.c tools/plant_kernel_mutant.py
	python3 tools/plant_kernel_mutant.py matcher $< $@
$(KERNEL_BUILD)/scope_layout_mutant.h: native/kernel/pair_layout.generated.h tools/plant_kernel_mutant.py
	python3 tools/plant_kernel_mutant.py scope $< $@
$(KERNEL_BUILD)/test_matcher_leak: tests/kernel_match_frames.c tests/native_kernel_matching_runner.h $(KERNEL_BUILD)/matcher_reference_leak.c $(KERNEL_MATCH_SRC) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) $(BUILD_CONFIG_HEADER)
	$(CC) $(CPPFLAGS) -DCETTA_TEST_HOOKS=1 -DCETTA_RUNTIME_STATS_IMPL=1 $(CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ $< $(filter-out src/match.c,$(KERNEL_MATCH_SRC)) $(KERNEL_BUILD)/matcher_reference_leak.c native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/reference.c native/kernel/pair.c native/kernel/observe.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) $(LDFLAGS) -pthread
$(KERNEL_BUILD)/test_scope_layout_mutant: tests/test_native_kernel_pair.c $(KERNEL_BUILD)/scope_layout_mutant.h native/kernel/pair.c $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -include $(KERNEL_BUILD)/scope_layout_mutant.h native/kernel/pair.c $< $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: test-native-kernel-mutations
test-native-kernel-mutations: $(KERNEL_BUILD)/test_matcher_leak $(KERNEL_BUILD)/test_scope_layout_mutant
	@set +e; $(KERNEL_BUILD)/test_matcher_leak x86 > $(KERNEL_BUILD)/matcher_leak.log 2>&1; status=$$?; set -e; \
		test "$$status" = 1; grep -q 'native matching sentinel: C=' $(KERNEL_BUILD)/matcher_leak.log; \
		echo 'PASS: strict native mode detects a reference-matcher leak'
	@set +e; $(KERNEL_BUILD)/test_scope_layout_mutant > $(KERNEL_BUILD)/scope_layout.log 2>&1; status=$$?; set -e; \
		test "$$status" != 0; grep -q 'pair phase' $(KERNEL_BUILD)/scope_layout.log; \
		echo 'PASS: the independent policy oracle detects a changed scope coordinate'

$(KERNEL_BUILD)/test_batch: tests/test_native_kernel_batch.c native/kernel/batch.c native/kernel/batch.h $(KERNEL_BATCH_IMAGE)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) -UNDEBUG tests/test_native_kernel_batch.c native/kernel/batch.c $(KERNEL_BATCH_IMAGE) -o $@
.PHONY: test-native-kernel-batch
test-native-kernel-batch: $(KERNEL_BUILD)/test_batch
	@$<

$(KERNEL_BUILD)/bench_resident: tests/tcalc/tcalc_kernel_decide_test.c tests/tcalc/tcalc_decide_test.c $(TCALC_CORE) $(TCALC_UNIT) native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_bridge.h native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(KERNEL_PRIVATE_INCLUDES) -UNDEBUG -D_POSIX_C_SOURCE=200809L -Inative/tcalc \
		tests/tcalc/tcalc_kernel_decide_test.c $(TCALC_CORE) native/tcalc/tcalc_syntax.c \
		native/kernel/kernel.c native/kernel/batch.c native/kernel/constructor.c native/kernel/pair.c native/kernel/observe.c native/kernel/reference.c native/kernel/tcalc_native.c $(KERNEL_TCALC_IMAGES) $(KERNEL_PAIR_IMAGE) $(KERNEL_WORD_IMAGE) $(KERNEL_OBSERVE_IMAGE) $(KERNEL_CONSTRUCTOR_IMAGE) $(KERNEL_BATCH_IMAGE) $(KERNEL_LIBRARY) -pthread -o $@
.PHONY: benchmark-native-kernel-resident
benchmark-native-kernel-resident: $(KERNEL_BUILD)/bench_resident
	@set -e; for case in --bench --bench-product; do for mode in c x86 hybrid; do "$<" "$$mode" "$$case"; done; done
