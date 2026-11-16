# The transform calculus lane: `cetta --lang tcalc`.
#
# The core is the Cesta unit tcalc_core.cesta: ownership contracts over the
# plain C of tcalc_core.c.  What compiles is its export, tcalc_core.export.c,
# which is checked in, so building with CESTA_BUILD=0 needs no Cesta tool.
# With CESTA_BUILD=1 the export is made again whenever the core changes, which
# runs Cesta's ownership check: a core the check refuses does not build.
TCALC_CORE = native/tcalc/tcalc_core.export.c
TCALC_SRC = $(TCALC_CORE) $(addprefix native/tcalc/,tcalc_syntax.c tcalc_surface.c tcalc_cli.c tcalc_x86.c)
TCALC_OWNERSHIP = cd native/tcalc && $(CESTA) export-c --std=c11 tcalc_core.cesta
TCALC_OWNERSHIP_FLAGS = -- -I. -D_POSIX_C_SOURCE=200809L
# The MeTTa module (lib/tcalc.metta) is CeTTa's; the standalone runners do without it.
SRC += $(TCALC_SRC) native/tcalc/tcalc_cetta_module.c

TCALC_HEADERS = $(addprefix native/tcalc/,tcalc_core.h tcalc_flat_layout.h tcalc_records.h tcalc_syntax.h tcalc_surface.h \
	tcalc_cli.h tcalc_x86.h x86/ordinary_flat_cesta.h \
	tcalc_lists.generated.h tcalc_axes_enums.generated.h tcalc_axes_names.generated.h \
	tcalc_fire.generated.h) \
	src/runner.h src/generated/runner_axes.generated.h src/generated/runner_lists.generated.h
# The unit's sources: the calculus, the storage it includes, and the adapter of
# backend x86-flat.
TCALC_UNIT = native/tcalc/tcalc_core.c native/tcalc/tcalc_store.c native/tcalc/tcalc_flat.c

ifeq ($(CESTA_BUILD),1)
$(TCALC_CORE): native/tcalc/tcalc_core.cesta $(TCALC_UNIT) $(TCALC_HEADERS) | $(CESTA)
	@($(TCALC_OWNERSHIP) -o tcalc_core.export.c.new $(TCALC_OWNERSHIP_FLAGS))
	@if cmp -s $@.new $@; then rm -f $@.new; touch $@; else mv $@.new $@; fi
endif

# backend x86-flat: the flat_fire runtime image, copied from the compiled tcalc
# artifacts (native/tcalc/x86/SOURCES), assembled as is and linked on x86-64.
# Without it the decider is absent and backend x86-flat says so.
TCALC_X86_DIR = native/tcalc/x86
TCALC_X86_OBJ = $(TCALC_X86_DIR)/ordinary_flat_cesta_runtime.o
TCALC_COMPILED ?= ../../Mettapedia/MettaKernel/tcalc-compiled
ifeq ($(shell uname -m),x86_64)
TCALC_X86_LINK = $(TCALC_X86_OBJ)
OBJ += $(TCALC_X86_OBJ)
endif

$(TCALC_X86_OBJ): $(TCALC_X86_DIR)/ordinary_flat_cesta_runtime.S $(TCALC_X86_DIR)/ordinary_flat_cesta_linked.text
	as --64 -I $(TCALC_X86_DIR) -o $@ $<
TCALC_TEST_DIR ?= runtime/tcalc
TCALC_TEST_CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow \
	-Wconversion -Wno-sign-conversion -D_POSIX_C_SOURCE=200809L
TCALC_SANITIZE_CFLAGS ?= -O1 -g -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TCALC_FIXTURES = $(sort $(wildcard tests/tcalc/*.tcalc tests/tcalc/*.rho tests/tcalc/*.chr))

$(TCALC_TEST_DIR):
	mkdir -p $@

# The core, the reader and the runner on their own, without the rest of CeTTa.
$(TCALC_TEST_DIR)/tcalc_main: tests/tcalc/tcalc_main.c $(TCALC_SRC) $(TCALC_UNIT) $(TCALC_HEADERS) $(TCALC_X86_LINK) | $(TCALC_TEST_DIR)
	$(CC) $(TCALC_TEST_CFLAGS) -Inative/tcalc $< $(TCALC_SRC) $(TCALC_X86_LINK) -o $@

$(TCALC_TEST_DIR)/tcalc_main_sanitized: tests/tcalc/tcalc_main.c $(TCALC_SRC) $(TCALC_UNIT) $(TCALC_HEADERS) $(TCALC_X86_LINK) | $(TCALC_TEST_DIR)
	$(CC) $(TCALC_SANITIZE_CFLAGS) -Inative/tcalc $< $(TCALC_SRC) $(TCALC_X86_LINK) -o $@

# Each fixture's output, read by the standalone core (with sanitizers) and by
# the cetta binary, must equal its .expected file: with the core's shortcuts
# taken, and with them checked (CETTA_TCALC_SHORTCUTS=checked: every successor
# also computed in full and compared, a disagreement reported in the output).
# The harness is the one the rho lane's fixtures run under, tests/run_fixtures.sh.
define tcalc_check
	@sh tests/run_fixtures.sh "tcalc, $(2)" "$(1)" \
		CETTA_TCALC_SHORTCUTS=taken CETTA_TCALC_SHORTCUTS=checked -- $(TCALC_FIXTURES)
endef

.PHONY: test-tcalc-core test-tcalc
$(TCALC_TEST_DIR)/tcalc_successors_test: tests/tcalc/tcalc_successors_test.c $(TCALC_SRC) $(TCALC_UNIT) $(TCALC_HEADERS) | $(TCALC_TEST_DIR)
	$(CC) $(TCALC_SANITIZE_CFLAGS) -Inative/tcalc $< $(TCALC_CORE) native/tcalc/tcalc_syntax.c -o $@

$(TCALC_TEST_DIR)/tcalc_decide_test: tests/tcalc/tcalc_decide_test.c $(TCALC_SRC) $(TCALC_UNIT) $(TCALC_HEADERS) | $(TCALC_TEST_DIR)
	$(CC) $(TCALC_SANITIZE_CFLAGS) -Inative/tcalc $< $(TCALC_CORE) native/tcalc/tcalc_syntax.c -o $@

$(TCALC_TEST_DIR)/tcalc_allocation_faults: tests/tcalc/tcalc_allocation_faults.c $(TCALC_SRC) $(TCALC_UNIT) $(TCALC_HEADERS) | $(TCALC_TEST_DIR)
	$(CC) $(TCALC_SANITIZE_CFLAGS) -Inative/tcalc $< $(TCALC_CORE) native/tcalc/tcalc_syntax.c \
		-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@

test-tcalc-core: $(TCALC_TEST_DIR)/tcalc_main_sanitized $(TCALC_TEST_DIR)/tcalc_successors_test $(TCALC_TEST_DIR)/tcalc_decide_test $(TCALC_TEST_DIR)/tcalc_allocation_faults
	$(call tcalc_check,$(TCALC_TEST_DIR)/tcalc_main_sanitized,standalone core with sanitizers)
	@$(TCALC_TEST_DIR)/tcalc_successors_test
	@$(TCALC_TEST_DIR)/tcalc_decide_test
	@$(TCALC_TEST_DIR)/tcalc_allocation_faults

TCALC_CETTA = tests/tcalc/run_cetta.sh $(abspath $(BIN))
test-tcalc: $(BIN)
	$(call tcalc_check,$(TCALC_CETTA),cetta --lang tcalc)
	@sh tests/tcalc/notations.sh $(abspath $(BIN)) $(TCALC_TEST_DIR)/notations
	@for lane in he petta; do \
	for gc in "" "CETTA_GC=1 CETTA_GC_BUDGET_MB=1"; do \
		out=$$(env $$gc $(abspath $(BIN)) --lang $$lane tests/tcalc/module.metta 2>&1) || true; \
		if [ "$$out" != "$$(cat tests/tcalc/module.$$lane.expected)" ]; then \
			echo "FAIL: tcalc module from MeTTa ($$lane, $${gc:-ordinary GC})"; \
			printf '%s\n' "$$out" | diff tests/tcalc/module.$$lane.expected - | head -20; exit 1; \
		fi; \
	done; \
	done; \
	echo "PASS: tcalc module from MeTTa (he, petta; ordinary/forced GC): load, outcomes, step, query, refusals"

.PHONY: test-tcalc-asan
test-tcalc-asan:
	@$(MAKE) -s BUILD=core ENABLE_SANITIZERS=1 SANITIZERS=address,undefined \
		CETTA_PROVENANCE_ASSERT=1 ENABLE_PYTHON=0 ENABLE_LIB_PROLOG=0 \
		ASAN_REPEATABLE=1 test-tcalc

# backend x86-flat decides each flat fixture as the C core does: the same
# output, but for the backend the interpretation names.
TCALC_FLAT_FIXTURES = $(addprefix tests/tcalc/,token_race dining_philosophers petri_buffer \
	transitive_closure gamma_max swap two_tokens_every renamed_copy stale_read cycle vanish \
	flat_identical_rows)
define tcalc_x86_differential
	@set -eu; failures=0; \
	for f in $(TCALC_FLAT_FIXTURES); do \
		c=$$($(1) 'step=one' "$$f.tcalc" 2>&1) || true; \
		x=$$($(1) 'step=one;backend=x86-flat' "$$f.tcalc" 2>&1 | sed 's/(backend x86-flat)/(backend c)/') || true; \
		if [ "$$c" != "$$x" ]; then echo "FAIL: $$f.tcalc ($(2))"; echo "c:"; echo "$$c"; echo "x86-flat:"; echo "$$x"; \
			failures=$$((failures + 1)); fi; \
	done; \
	[ $$failures -eq 0 ]; \
	echo "PASS: backend x86-flat decides as the C core ($(2)): $(words $(TCALC_FLAT_FIXTURES))"
endef

.PHONY: test-tcalc-x86 tcalc-x86-check tcalc-x86-sync
test-tcalc-x86: $(TCALC_TEST_DIR)/tcalc_main_sanitized $(BIN)
	$(call tcalc_x86_differential,$(TCALC_TEST_DIR)/tcalc_main_sanitized,standalone core with sanitizers)
	$(call tcalc_x86_differential,$(TCALC_CETTA),cetta --lang tcalc)

# Each function of the image, as linked into cetta, has exactly its bytes in
# ordinary_flat_cesta_linked.text (offsets from the runtime image).
tcalc-x86-check: $(BIN)
	@set -eu; for spec in flat_fire:0:211 ordinary_flat_pack:211:530 \
		ordinary_flat_adapter:741:704 ordinary_flat_failure:1445:2; do \
		fn=$${spec%%:*}; rest=$${spec#*:}; skip=$${rest%%:*}; count=$${rest#*:}; \
		linked=$$(objdump -d --disassemble="$$fn" $(BIN) | awk -F'\t' '/^ *[0-9a-f]+:\t/ { n = split($$2, b, " "); for (i = 1; i <= n; i++) printf "%s ", b[i] }'); \
		proved=$$(dd if=$(TCALC_X86_DIR)/ordinary_flat_cesta_linked.text bs=1 skip=$$skip count=$$count 2>/dev/null | od -An -tx1 -v | tr -s ' \n' ' ' | sed 's/^ //'); \
		[ "$$linked" = "$$proved" ] || { echo "FAIL: $$fn: the linked bytes differ from the image"; exit 1; }; \
		echo "$$fn: $$count bytes as in the image"; \
	done

# Copies the image one way from the compiled artifacts; a copy whose checksum
# differs from its source is refused.
tcalc-x86-sync:
	@set -eu; for f in ordinary_flat_cesta_runtime.S ordinary_flat_cesta_linked.text build-cesta/ordinary_flat_cesta.h; do \
		cp "$(TCALC_COMPILED)/$$f" "$(TCALC_X86_DIR)/$$(basename $$f)"; \
		[ "$$(sha256sum < "$(TCALC_COMPILED)/$$f")" = "$$(sha256sum < "$(TCALC_X86_DIR)/$$(basename $$f)")" ] || \
			{ echo "checksum mismatch on $$f"; exit 1; }; \
	done; echo "copied; update the sha256 lines of $(TCALC_X86_DIR)/SOURCES"

# The shared axes of the transform step (lib/transform_axes.data) are generated
# by mk/transform_axes.mk, since they are the transform step's, not tcalc's.

# Regenerating the Cesta-declared headers needs the Cesta tool; building them
# does not.  The check regenerates them aside and compares the bytes.  The
# order in which a match is decided is the protocol of Fire.module, exported
# with its plan to tcalc_fire.generated.h; protocol verify checks both.
TCALC_EXPORTS = tcalc_lists tcalc_axes_enums tcalc_axes_names
TCALC_PROTOCOL = cd native/tcalc && $(CESTA) protocol
.PHONY: tcalc-regenerate check-tcalc-generated
tcalc-regenerate: $(CESTA)
	@set -eu; for n in $(TCALC_EXPORTS); do \
		(cd native/tcalc && $(CESTA) export-header $$n.export -o $$n.generated.h); \
	done; \
	($(TCALC_PROTOCOL) plan Fire.module Protocols -o tcalc_fire.plan); \
	($(TCALC_PROTOCOL) export Fire.module Protocols -o tcalc_fire.generated.h --plan tcalc_fire.plan); \
	echo "wrote $(foreach n,$(TCALC_EXPORTS) tcalc_fire,native/tcalc/$(n).generated.h) native/tcalc/tcalc_fire.plan"
check-tcalc-generated: $(CESTA)
	@set -eu; work=$$(mktemp -d $(CURDIR)/runtime/tcalc-generated.XXXXXX); trap 'rm -rf "$$work"' EXIT; \
	for n in $(TCALC_EXPORTS); do \
		(cd native/tcalc && $(CESTA) export-header $$n.export -o "$$work/$$n.generated.h"); \
		cmp -s "$$work/$$n.generated.h" native/tcalc/$$n.generated.h || \
			{ echo "FAIL: native/tcalc/$$n.generated.h is not what $$n.export generates"; exit 1; }; \
	done; \
	($(TCALC_PROTOCOL) verify Fire.module Protocols -o tcalc_fire.generated.h --plan tcalc_fire.plan) || \
		{ echo "FAIL: native/tcalc/tcalc_fire.generated.h or tcalc_fire.plan is not what Fire.module generates"; exit 1; }; \
	($(TCALC_OWNERSHIP) -o "$$work/tcalc_core.export.c" --plan "$$work/tcalc_core.plan" $(TCALC_OWNERSHIP_FLAGS)) || \
		{ echo "FAIL: Cesta's ownership check refuses native/tcalc/tcalc_core.cesta"; exit 1; }; \
	cmp -s "$$work/tcalc_core.export.c" $(TCALC_CORE) || \
		{ echo "FAIL: $(TCALC_CORE) is not what tcalc_core.cesta exports"; exit 1; }; \
	python3 tools/check_tcalc_ownership_coverage.py native/tcalc/tcalc_ownership_coverage.json \
		"$$work/tcalc_core.plan" "$$work/tcalc_core.export.c" vendor/cesta/SOURCE.json; \
	echo "PASS: tcalc's generated headers, its firing protocol and its ownership-checked export regenerate byte for byte"

# Rule modules read as tcalc programs by demand (lib/tcalc_modules.metta, docs/tcalc.md section
# 10): readings, refusals, and rulec's compiled modules against the reading (the lambda client on
# its cases, the Megalodon kernel on calls captured from mgmodule).  The reading of the Megalodon
# kernel is checked in (tests/tcalc/modules/megalodon_kernel.tcalc), since its module lives in
# Mettapedia; tcalc-modules-regenerate MEGALODON_MODULE=<path to MegalodonKernel.module> makes it
# again, and its first line names the module's SHA-256, which is mgmodule's too.
.PHONY: test-tcalc-modules tcalc-modules-regenerate
test-tcalc-modules: $(BIN)
	@sh tests/tcalc/modules/run.sh $(abspath $(BIN)) $(abspath $(TCALC_TEST_DIR))/modules
tcalc-modules-regenerate: $(BIN)
	@set -eu; test -n "$(MEGALODON_MODULE)" || { echo "give MEGALODON_MODULE=<path to MegalodonKernel.module>"; exit 1; }; \
	work=$$(mktemp -d $(CURDIR)/runtime/tcalc-modules.XXXXXX); \
	printf '!(import! &self "%s/lib/tcalc_modules.metta")\n!(tcalc:module-write "%s" "Kernel" "%s/reading.tcalc")\n' \
		"$(CURDIR)" "$(abspath $(MEGALODON_MODULE))" "$$work" > "$$work/regenerate.metta"; \
	answer=$$($(abspath $(BIN)) "$$work/regenerate.metta" | tail -1); \
	case "$$answer" in "(tcalc:module-written "*) ;; *) echo "FAIL: $$answer"; exit 1 ;; esac; \
	{ printf '; generated by lib/tcalc_modules.metta from MegalodonKernel.module (module SHA-256 %s), theory Kernel; do not edit\n' \
		"$$(sha256sum "$(MEGALODON_MODULE)" | cut -d' ' -f1)"; cat "$$work/reading.tcalc"; } > tests/tcalc/modules/megalodon_kernel.tcalc; \
	rm -f "$$work/regenerate.metta" "$$work/reading.tcalc"; rmdir "$$work"; \
	echo "wrote tests/tcalc/modules/megalodon_kernel.tcalc"
