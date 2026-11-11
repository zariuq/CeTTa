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
MEGALODON_KERNEL_SRC = $(filter-out native/megalodon/mg_atom.c native/megalodon/mg_cetta_module.c,$(MEGALODON_SRC)) \
	$(MEGALODON_GSLT_READER_SRC)
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
	$(CC) $(MEGALODON_TEST_CFLAGS) -I. -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

$(addprefix $(MEGALODON_TEST_DIR)/,$(addsuffix _sanitized,$(MEGALODON_UNITS))): \
		$(MEGALODON_TEST_DIR)/%_sanitized: tests/megalodon/%.c $(MEGALODON_KERNEL_SRC) $(MEGALODON_HEADERS) \
		| $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_SANITIZE_CFLAGS) -I. -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

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
	$(CC) $(MEGALODON_TSAN_CFLAGS) -I. -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

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

# Reading documents for the evaluator (mg_source.h): the presentations of
# langdef/megalodon compiled to C, their items made into the evaluator's
# parse trees (native/megalodon/mg_gslt_reader.c).  The fixtures carry
# Megalodon's own readings (tests/megalodon/source_reader: FILE.items, and
# FILE.err, the refusal, empty when Megalodon reads the file to the end).
# The corpus targets, which need a Megalodon checkout, are in mk/external.mk.
MG_SOURCE_DUMP = $(MEGALODON_TEST_DIR)/mg-source-dump

$(MG_SOURCE_DUMP): tests/megalodon/mg_source_dump.c $(MEGALODON_GSLT_READER_SRC) \
		native/megalodon/mg_source.h native/megalodon/mg_gslt_reader.h | $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_TEST_CFLAGS) -I. -Inative/megalodon $< $(MEGALODON_GSLT_READER_SRC) -o $@

.PHONY: test-megalodon-source test-megalodon-gslt
test-megalodon-source: $(MG_SOURCE_DUMP)
	@bash tests/megalodon/run_source_reader_fixtures.sh $(MG_SOURCE_DUMP)

# Document evaluation (native/megalodon/mg_elab.c, mg_proof.c): megalodon.ml's
# items over interpret.ml's elaboration, with Megalodon's identities and
# Proofgold ids, and its tactic engine with megaauto.  mg-sexprinfo prints
# what `megalodon -sexprinfo` prints.  The fixtures
# (tests/megalodon/elab) carry Megalodon's recorded output, failures
# included.
MG_SEXPRINFO = $(MEGALODON_TEST_DIR)/mg-sexprinfo
MG_CHECK = $(MEGALODON_TEST_DIR)/mg-check

$(MG_SEXPRINFO): tests/megalodon/mg_sexprinfo.c $(MEGALODON_KERNEL_SRC) $(MEGALODON_HEADERS) \
		native/megalodon/mg_upstream_tables.inc | $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_TEST_CFLAGS) -I. -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

# mg-check: the verdict of the native kernel on a document (what is verified,
# under which of the document's axioms).
$(MG_CHECK): tests/megalodon/mg_check.c $(MEGALODON_KERNEL_SRC) $(MEGALODON_HEADERS) \
		native/megalodon/mg_upstream_tables.inc | $(MEGALODON_TEST_DIR)
	$(CC) $(MEGALODON_TEST_CFLAGS) -I. -Inative/megalodon $< $(MEGALODON_KERNEL_SRC) -o $@

.PHONY: test-megalodon-elab test-megalodon-verdict
test-megalodon-elab: $(MG_SEXPRINFO)
	@bash tests/megalodon/run_elab_fixtures.sh $(MG_SEXPRINFO)

# The kernel's verdicts (mg-check): verified, assumed, admitted, aby, dependent,
# unfinished and out-of-budget theorems, with expected reports written from the
# verdict rules.
test-megalodon-verdict: $(MG_CHECK)
	@bash tests/megalodon/run_verdict_fixtures.sh $(MG_CHECK)

# The same syntax as GSLT presentations (langdef/megalodon, generated by
# tools/generate_megalodon_gslt_v1.py), compiled to one PeTTa program that
# CeTTa runs on the fixtures; every file reads as Megalodon reads it.
MEGALODON_GSLT_SOURCES = $(addprefix langdef/megalodon/,lexical_v1.metta \
	notation_environment_v1.metta term_parser_v1.metta document_v1.metta \
	proof_script_v1.metta source_fold_v1.metta)
MEGALODON_GSLT_PROGRAM = $(MEGALODON_TEST_DIR)/megalodon_reader_v1.petta.metta
MEGALODON_GSLT_ENTRY_MODES = --closed-entry-mode MgNextTokenV1:10 \
	--closed-entry-mode MgLexV1:10 \
	--closed-entry-mode MgReaderStartV1:10 \
	--closed-entry-mode MgReaderNextFileV1:110 \
	--closed-entry-mode MgReadItemV1:10 \
	--closed-entry-mode MgReadDocumentV1:10 \
	--closed-entry-mode MgReadWithSignaturesV1:110
# The same presentations compiled to C (petta-direct --target c; the
# sources are in the Makefile's MEGALODON_GSLT_READER_SRC, linked into
# CeTTa) and a reader around them (tests/megalodon/mg_gslt_native.c).
MG_GSLT_NATIVE = $(MEGALODON_TEST_DIR)/mg-gslt-native

$(MEGALODON_GSLT_PROGRAM): $(GSLT_GROUND_RELATIONS_V1) $(CETTA_PETTA_GROUND_RELATIONS_V1) \
		$(MEGALODON_GSLT_SOURCES) $(LANGDEF_COMPILER_V1_BIN) | $(MEGALODON_TEST_DIR)
	$(LANGDEF_COMPILER_V1_BIN) petta-direct \
		--source $(GSLT_GROUND_RELATIONS_V1) \
		--source $(CETTA_PETTA_GROUND_RELATIONS_V1) \
		$(foreach source,$(MEGALODON_GSLT_SOURCES),--source $(source)) \
		$(MEGALODON_GSLT_ENTRY_MODES) --out $@ >/dev/null

$(MEGALODON_GSLT_NATIVE_C): $(GSLT_GROUND_RELATIONS_V1) $(CETTA_PETTA_GROUND_RELATIONS_V1) \
		$(MEGALODON_GSLT_SOURCES) $(LANGDEF_COMPILER_V1_BIN)
	@mkdir -p $(dir $@)
	$(LANGDEF_COMPILER_V1_BIN) petta-direct --target c --c-prefix megalodon_reader_v1 \
		--source $(GSLT_GROUND_RELATIONS_V1) \
		--source $(CETTA_PETTA_GROUND_RELATIONS_V1) \
		$(foreach source,$(MEGALODON_GSLT_SOURCES),--source $(source)) \
		$(MEGALODON_GSLT_ENTRY_MODES) --out $@ >/dev/null

$(MG_GSLT_NATIVE): tests/megalodon/mg_gslt_native.c $(MEGALODON_GSLT_READER_SRC) \
		native/gslt_native_runtime_v1.h native/megalodon/mg_gslt_reader.h | $(MEGALODON_TEST_DIR)
	$(CC) -O2 -std=c11 -D_GNU_SOURCE -pthread -Wall -Wextra -Werror -I. -Inative/megalodon \
		$< $(MEGALODON_GSLT_READER_SRC) -o $@

# The same reader built with its trace (GN_TRACE): mg-gslt-native-trace
# --trace FILE writes each mode entered, each rule that answers or misses,
# and each mode without an answer.
MG_GSLT_NATIVE_TRACE = $(MEGALODON_TEST_DIR)/mg-gslt-native-trace
$(MG_GSLT_NATIVE_TRACE): tests/megalodon/mg_gslt_native.c $(MEGALODON_GSLT_READER_SRC) \
		native/gslt_native_runtime_v1.h native/megalodon/mg_gslt_reader.h | $(MEGALODON_TEST_DIR)
	$(CC) -O2 -std=c11 -D_GNU_SOURCE -DGN_TRACE -pthread -Wall -Wextra -Werror -I. -Inative/megalodon \
		$< $(MEGALODON_GSLT_READER_SRC) -o $@

# The C target itself (langdef-compile petta-direct --target c) on small
# presentations: refusals, class runs, and answers equal to the PeTTa
# target's in CeTTa; then the runtime's spans on their own.
.PHONY: test-gslt-native-direct
test-gslt-native-direct: $(LANGDEF_COMPILER_V1_BIN) $(BIN)
	@python3 tests/langdef/run_native_direct_fixtures.py $(LANGDEF_COMPILER_V1_BIN) ./$(BIN)
	@mkdir -p runtime/langdef-native-direct
	@$(CC) -O1 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. tests/langdef/native_direct/test_spans.c \
		native/gslt_native_runtime_v1.c -o runtime/langdef-native-direct/test_spans
	@runtime/langdef-native-direct/test_spans

.PHONY: test-megalodon-gslt-native
test-megalodon-gslt-native: $(MG_GSLT_NATIVE) $(MG_GSLT_NATIVE_TRACE) test-gslt-native-direct
	@for mode in tokens items; do \
		python3 tests/megalodon/compare_gslt_reader.py --engine native --native $(MG_GSLT_NATIVE) \
			--mode $$mode --expected --work $(MEGALODON_TEST_DIR)/gslt-native-$$mode \
			tests/megalodon/source_reader/*.mg || exit 1; \
	done
	@python3 tests/megalodon/check_gslt_trace.py $(MG_GSLT_NATIVE) $(MG_GSLT_NATIVE_TRACE) \
		$(MEGALODON_TEST_DIR)/gslt-trace tests/megalodon/source_reader/*.mg

test-megalodon-gslt: $(BIN) $(MEGALODON_GSLT_PROGRAM)
	@python3 tools/generate_megalodon_gslt_v1.py --check
	@$(LANGDEF_COMPILER_V1_BIN) validate --manifest langdef/megalodon/langdef.metta
	@if grep 'gslt-binding-mode' $(MEGALODON_GSLT_PROGRAM) | grep -q 'functional=relational'; then \
		echo 'a Megalodon presentation lost its deterministic reading'; exit 1; \
	fi
	@for mode in tokens items; do \
		python3 tests/megalodon/compare_gslt_reader.py --cetta "$(abspath $(BIN))" \
			--program $(MEGALODON_GSLT_PROGRAM) --mode $$mode --expected \
			--work $(MEGALODON_TEST_DIR)/gslt-$$mode tests/megalodon/source_reader/*.mg || exit 1; \
	done

# The syntax presentations module by module (tests/langdef/megalodon/,
# named MODULE_CASE.mg as tests/langdef/mm0/ are): each fixture read by the
# PeTTa program in CeTTa and by the compiled reader, tokens and items, must
# give Megalodon's own readings recorded beside it (FILE.tokens,
# FILE.items, FILE.err), signatures from signatures.tsv.  The bytes_*
# fixtures hold bytes outside UTF-8, which Megalodon reads in strings and
# comments: the compiled reader reads them, while CeTTa reads a langdef's
# source as UTF-8 text, so they run on the compiled reader only.
MEGALODON_LANGDEF_DIR = tests/langdef/megalodon
MEGALODON_LANGDEF_MODULES = lexical notation term proof document whole_file
.PHONY: test-langdef-megalodon
test-langdef-megalodon: $(BIN) $(MEGALODON_GSLT_PROGRAM) $(MG_GSLT_NATIVE)
	@for mode in tokens items; do \
		out=$$(python3 tests/megalodon/compare_gslt_reader.py --engine native --native $(MG_GSLT_NATIVE) \
			--mode $$mode --expected --work $(MEGALODON_TEST_DIR)/langdef-native-$$mode \
			$(MEGALODON_LANGDEF_DIR)/bytes_*.mg) || { printf '%s\n' "$$out"; exit 1; }; \
		printf 'bytes native %s: %s\n' "$$mode" "$$(printf '%s\n' "$$out" | tail -1 | cut -d';' -f1)"; \
	done
	@for module in $(MEGALODON_LANGDEF_MODULES); do \
		for mode in tokens items; do \
			out=$$(python3 tests/megalodon/compare_gslt_reader.py --engine native --native $(MG_GSLT_NATIVE) \
				--mode $$mode --expected --includes $(MEGALODON_LANGDEF_DIR)/signatures.tsv \
				--root $(MEGALODON_LANGDEF_DIR) --work $(MEGALODON_TEST_DIR)/langdef-native-$$mode \
				$(MEGALODON_LANGDEF_DIR)/$${module}_*.mg) || { printf '%s\n' "$$out"; exit 1; }; \
			printf '%s native %s: %s\n' "$$module" "$$mode" "$$(printf '%s\n' "$$out" | tail -1 | cut -d';' -f1)"; \
			out=$$(python3 tests/megalodon/compare_gslt_reader.py --cetta "$(abspath $(BIN))" \
				--program $(MEGALODON_GSLT_PROGRAM) --mode $$mode --expected \
				--includes $(MEGALODON_LANGDEF_DIR)/signatures.tsv --root $(MEGALODON_LANGDEF_DIR) \
				--work $(MEGALODON_TEST_DIR)/langdef-cetta-$$mode $(MEGALODON_LANGDEF_DIR)/$${module}_*.mg) \
				|| { printf '%s\n' "$$out"; exit 1; }; \
			printf '%s cetta-petta %s: %s\n' "$$module" "$$mode" "$$(printf '%s\n' "$$out" | tail -1 | cut -d';' -f1)"; \
		done; \
	done

test-megalodon: test-megalodon-core test-megalodon-metta test-seal test-megalodon-source \
	test-megalodon-elab test-megalodon-verdict test-megalodon-gslt test-megalodon-gslt-native \
	test-langdef-megalodon

# Requires the pinned TPTP SyntaxBNF source. Kept separate from the kernel
# tests because it constructs and checks the official parser artifacts.
.PHONY: test-megalodon-tptp
test-megalodon-tptp: $(BIN) prepare-tptp-compact-morphism-v1-body
	$(call megalodon_dual_lane,integration)

test-all: test-megalodon
