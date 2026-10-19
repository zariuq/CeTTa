# Native Megalodon: kernel unit tests, replay tools, and the MeTTa examples
# run in HE and in PeTTa (tests/megalodon).  Included at the end of the
# Makefile, after $(BIN) is defined.
#
#   make test-megalodon             kernel units, MeTTa examples and the seal
#                                   contract, each in HE and PeTTa
#   make test-megalodon-sanitized   kernel units under ASan/UBSan, with
#                                   allocation-failure injection
#   make test-megalodon-tsan        the concurrency contract under TSan
#   make megalodon-tools            mg_replay and mg_diff (export replay and
#                                   the lazy/reference conversion differential)
#
# The kernel itself (everything but the CeTTa adapter) builds without CeTTa,
# so its tests do too.

MEGALODON_TEST_DIR ?= runtime/megalodon
MEGALODON_KERNEL_SRC = $(filter-out native/megalodon/mg_atom.c native/megalodon/mg_cetta_module.c,$(MEGALODON_SRC))
MEGALODON_HEADERS = $(wildcard native/megalodon/*.h)
MEGALODON_TEST_CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -D_GNU_SOURCE -pthread
# The sanitized units also inject allocation failure at every allocation
# site they exercise (MG_TEST_HOOKS).
MEGALODON_SANITIZE_CFLAGS ?= -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -D_GNU_SOURCE -pthread \
	-DMG_TEST_HOOKS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
MEGALODON_UNITS = test_mg_core test_mg_session test_mg_conv test_mg_lazy test_mg_threads
MEGALODON_TOOLS = mg_replay mg_diff
MEGALODON_METTA_TESTS = $(sort $(basename $(notdir $(wildcard tests/megalodon/metta/*.metta))))
# Files the MeTTa examples write (mg_library saves a library here).
MEGALODON_METTA_SCRATCH ?= runtime/megalodon-test

$(MEGALODON_TEST_DIR):
	mkdir -p $@

$(addprefix $(MEGALODON_TEST_DIR)/,$(MEGALODON_UNITS) $(MEGALODON_TOOLS)): \
		$(MEGALODON_TEST_DIR)/%: tests/megalodon/%.c $(MEGALODON_KERNEL_SRC) $(MEGALODON_HEADERS) \
		| $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_TEST_CFLAGS) -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

$(addprefix $(MEGALODON_TEST_DIR)/,$(addsuffix _sanitized,$(MEGALODON_UNITS))): \
		$(MEGALODON_TEST_DIR)/%_sanitized: tests/megalodon/%.c $(MEGALODON_KERNEL_SRC) $(MEGALODON_HEADERS) \
		| $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_SANITIZE_CFLAGS) -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

.PHONY: test-megalodon test-megalodon-core test-megalodon-sanitized test-megalodon-metta \
	megalodon-tools
megalodon-tools: $(addprefix $(MEGALODON_TEST_DIR)/,$(MEGALODON_TOOLS))

test-megalodon-core: $(addprefix $(MEGALODON_TEST_DIR)/,$(MEGALODON_UNITS))
	@set -e; for t in $(MEGALODON_UNITS); do ./$(MEGALODON_TEST_DIR)/$$t; done

test-megalodon-sanitized: $(addprefix $(MEGALODON_TEST_DIR)/,$(addsuffix _sanitized,$(MEGALODON_UNITS)))
	@set -e; for t in $(MEGALODON_UNITS); do ./$(MEGALODON_TEST_DIR)/$${t}_sanitized; done

# The concurrency contract under ThreadSanitizer (needs a compiler with a
# TSan runtime, e.g. CC=gcc).
MEGALODON_TSAN_CFLAGS ?= -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -D_GNU_SOURCE -pthread \
	-fsanitize=thread
$(MEGALODON_TEST_DIR)/test_mg_threads_tsan: tests/megalodon/test_mg_threads.c $(MEGALODON_KERNEL_SRC) \
		$(MEGALODON_HEADERS) | $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_TSAN_CFLAGS) -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

.PHONY: test-megalodon-tsan
test-megalodon-tsan: $(MEGALODON_TEST_DIR)/test_mg_threads_tsan
	./$(MEGALODON_TEST_DIR)/test_mg_threads_tsan 8 4000

# Each example has its own output directory and expected answers in HE and
# PeTTa. Preserve failed-run evidence and permit concurrent test invocations.
define megalodon_dual_lane
	@set -eu; \
	work_root=$$(mktemp -d "$(MEGALODON_METTA_SCRATCH).XXXXXX"); \
	python3 tests/megalodon/run_examples.py "$$work_root/runs" "$(abspath $(BIN))" --group $(1)
endef

test-megalodon-metta: $(BIN)
	$(call megalodon_dual_lane,megalodon)

# The shared seal layer (lib_seal) against its contract.
SEAL_METTA_TESTS = $(sort $(basename $(notdir $(wildcard tests/seal/*.metta))))
.PHONY: test-seal
test-seal: $(BIN)
	$(call megalodon_dual_lane,seal)

test-megalodon: test-megalodon-core test-megalodon-metta test-seal

# Requires the pinned TPTP SyntaxBNF source. Kept separate from the kernel
# tests because it constructs and checks the official parser artifacts.
.PHONY: test-megalodon-tptp
test-megalodon-tptp: $(BIN) prepare-tptp-compact-morphism-v1-body
	$(call megalodon_dual_lane,integration)

test: test-megalodon
