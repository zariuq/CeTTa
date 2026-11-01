CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Wpedantic -Werror
REFERENCE_SYNTAX ?= ../hyperon/MeTTaIL/GSLT/src/main/bnfc/metta_venus.cf
REFERENCE_FIXTURES ?= ../hyperon/MeTTaIL/GSLT/src/test/module
CETTA_SOURCE ?= ../hyperon/cetta-cesta-corpus-opt-106cc2f2/src
C11PARSER_TESTS ?= ../repos/C11parser/tests
LIBSOURCES = src/base.c src/atom.c src/wire.c src/grammar.c src/dfa.c src/forest.c src/lr.c src/scope.c src/tree.c src/module.c src/compile.c src/metadata.c src/view.c src/bind.c src/ctype.c src/elaborate.c
SOURCES = $(LIBSOURCES) src/main.c

.PHONY: all test test-reference test-c test-examples test-driver test-literal-bytes
all: build/cesta

build/test-foundation build/test-modules build/test-c-scope build/test-grammar-trees build/syntax-probe build/test-presentations build/test-presentation-coverage build/test-whole-program-trees: src/sema.h

build/cesta-bootstrap: $(SOURCES) src/cesta.h src/sema.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(SOURCES) -o $@

build/embed: tools/embed.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< -o $@

build/CPPGNU.cf: build/cesta-bootstrap grammar/CPreprocessing.module grammar/GNUPreprocessing.module grammar/TheoryModule.cf
	./build/cesta-bootstrap module-grammar grammar/TheoryModule.cf grammar/GNUPreprocessing.module CPPGNU > $@

build/language-data.c: build/embed build/C11.cf build/CGNU.cf build/CPPGNU.cf grammar/TheoryModule.cf grammar/C11.profile grammar/CGNU.profile grammar/CGNUC11.profile grammar/CClang.profile grammar/CClangC11.profile presentations/CompactC.module presentations/CompactC2.module presentations/CMeraStyle.module
	./build/embed C11Grammar build/C11.cf CGNUGrammar build/CGNU.cf CPPGNUGrammar build/CPPGNU.cf TheoryModuleSyntax grammar/TheoryModule.cf C11Profile grammar/C11.profile CGNUProfile grammar/CGNU.profile CGNUC11Profile grammar/CGNUC11.profile CClangProfile grammar/CClang.profile CClangC11Profile grammar/CClangC11.profile CompactCModule presentations/CompactC.module CompactC2Module presentations/CompactC2.module CMeraStyleModule presentations/CMeraStyle.module > $@

build/cesta: $(SOURCES) src/cesta.h src/sema.h src/pp.h src/native.h src/macro.h src/export.h src/layout.h src/operation.h src/table.h src/protocol.h src/diagnose.h src/project.h src/checked.h src/ownership.h src/ownership.c src/driver.c src/diagnose.c src/project.c src/checked.c src/ownership.c src/view_driver.c src/native.c src/macro.c src/export.c src/layout.c src/operation.c src/table.c src/protocol.c src/sema.c src/source.c src/pp.c build/language-data.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Isrc -DCE_BUILTIN_LANGUAGES $(SOURCES) src/driver.c src/diagnose.c src/project.c src/checked.c src/ownership.c src/view_driver.c src/native.c src/macro.c src/export.c src/layout.c src/operation.c src/table.c src/protocol.c src/sema.c src/source.c src/pp.c build/language-data.c -o $@

build/test-foundation: tests/foundation.c $(LIBSOURCES) src/cesta.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/foundation.c $(LIBSOURCES) -o $@


build/test-modules: tests/modules.c $(LIBSOURCES) src/cesta.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/modules.c $(LIBSOURCES) -o $@

test-reference: build/test-modules
	./build/test-modules $(REFERENCE_SYNTAX) $(REFERENCE_FIXTURES)


.PHONY: test-modules test-workbench
test-modules: build/test-modules grammar/TheoryModule.cf
	./build/test-modules grammar/TheoryModule.cf

test-workbench: build/cesta
	python3 tests/workbench.py


build/C11.cf: build/cesta-bootstrap grammar/CLex.module grammar/CExpr.module grammar/CDecl.module grammar/CStmt.module grammar/CExternal.module grammar/TheoryModule.cf
	./build/cesta-bootstrap module-grammar grammar/TheoryModule.cf grammar/CExternal.module C11 > $@

build/test-c-scope: tests/c_scope.c $(LIBSOURCES) src/cesta.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/c_scope.c $(LIBSOURCES) -o $@

test-c: build/C11.cf build/test-c-scope


build/CGNU.cf: build/cesta-bootstrap build/C11.cf grammar/GNU.module grammar/TheoryModule.cf
	./build/cesta-bootstrap module-grammar grammar/TheoryModule.cf grammar/GNU.module CGNU > $@

test-c: build/CGNU.cf
	./build/test-c-scope build/C11.cf grammar/C11.profile
	./build/test-c-scope build/CGNU.cf grammar/CGNU.profile

test-examples: build/cesta build/C11.cf build/CGNU.cf
	python3 tests/examples.py


test-driver: build/cesta
	python3 tests/driver.py


.PHONY: test-native-authoring
test-native-authoring: build/cesta
	python3 -B tests/native_authoring.py


.PHONY: test-one-language
test-one-language: build/cesta
	python3 -B tests/one_language.py


.PHONY: test-project-build
test-project-build: build/cesta
	python3 -B tests/project_build.py
	python3 -B tests/project_generators.py
	python3 -B tests/read_warnings.py


.PHONY: test-macro-engine
test-macro-engine: build/cesta
	python3 -B tests/macro_engine.py


.PHONY: test-macro-native-names
test-macro-native-names: build/cesta
	python3 -B tests/macro_native_names.py


.PHONY: test-macro-definition-ports
test-macro-definition-ports: build/cesta
	python3 -B tests/macro_definition_ports.py


.PHONY: test-macro-port-admission
test-macro-port-admission: build/cesta
	python3 -B tests/macro_port_admission.py


.PHONY: test-macro-types test-macro-narrowing
test-macro-types: build/cesta
	python3 -B tests/macro_types.py

test-macro-narrowing: build/cesta
	python3 -B tests/macro_narrowing.py


.PHONY: test-macro-worklists
test-macro-worklists: build/cesta
	python3 -B tests/macro_worklists.py


.PHONY: test-macro-sizes test-macro-probes
test-macro-sizes: build/cesta
	python3 -B tests/macro_sizes.py

test-macro-probes: build/cesta
	python3 -B tests/macro_probes.py


.PHONY: test-macro-prescan
test-macro-prescan: build/cesta
	python3 -B tests/macro_prescan.py


.PHONY: test-macro-admission
test-macro-admission: build/cesta
	python3 -B tests/macro_admission.py


test-literal-bytes: build/cesta
	python3 tests/literal_bytes.py


.PHONY: test-c-printer
test-c-printer: build/cesta
	python3 -B tests/c_printer.py


build/test-grammar-trees: tests/grammar_trees.c $(LIBSOURCES) src/cesta.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/grammar_trees.c $(LIBSOURCES) -o $@

.PHONY: test-grammar-trees
test-grammar-trees: build/test-grammar-trees build/C11.cf build/CGNU.cf
	./build/test-grammar-trees build/C11.cf grammar/C11.profile 10000
	./build/test-grammar-trees build/CGNU.cf grammar/CGNU.profile 10000


.PHONY: test-coverage
test-coverage:
	python3 -B tests/n1570_coverage.py --self-test --report build/audit-coverage/normative_coverage_report.json


.PHONY: test-metadata test-petta
test-metadata: build/cesta build/C11.cf build/CGNU.cf
	python3 -B tests/metadata_admission.py $(METADATA_TEST_FLAGS)

test-petta: build/cesta
	python3 -B tests/petta_analysis.py $(PETTA_TEST_FLAGS)


build/syntax-probe: tests/syntax_probe.c $(LIBSOURCES) src/cesta.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/syntax_probe.c $(LIBSOURCES) -o $@

.PHONY: test-mutants
test-mutants: build/syntax-probe build/CGNU.cf
	python3 tests/syntax_mutants.py


CETTA_READER_SOURCES = atom.c symbol.c name_key.c parser.c binding/frame_identity.c
build/cetta-data-reader: tests/cetta_data_reader.c $(addprefix $(CETTA_SOURCE)/,$(CETTA_READER_SOURCES)) $(wildcard $(CETTA_SOURCE)/*.h)
	$(CC) -std=c11 -O2 -g0 -pthread -ffunction-sections -fdata-sections -I$(CETTA_SOURCE) tests/cetta_data_reader.c $(addprefix $(CETTA_SOURCE)/,$(CETTA_READER_SOURCES)) -Wl,--gc-sections -lgmp -lm -o $@

.PHONY: test-corpora test-c11parser
test-corpora: build/cesta build/cetta-data-reader build/syntax-probe
	python3 -B tests/corpus_suite.py

test-c11parser: build/cesta
	python3 -B tests/c11parser_oracle.py $(C11PARSER_TESTS)


.PHONY: test-lean
test-lean:
	python3 -B tests/lean_contracts.py $(LEAN_TEST_FLAGS)


build/test-presentations: tests/presentations.c $(LIBSOURCES) src/cesta.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/presentations.c $(LIBSOURCES) -o $@

.PHONY: test-presentations
test-presentations: build/test-presentations build/CGNU.cf
	./build/test-presentations


.PHONY: test-presentation-driver
test-presentation-driver: build/cesta
	python3 -B tests/presentation_driver.py


build/test-wire: tests/wire.c src/base.c src/atom.c src/wire.c src/cesta.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/wire.c src/base.c src/atom.c src/wire.c -o $@

.PHONY: test-wire
test-wire: build/test-wire
	./build/test-wire


.PHONY: test-portable-readers
test-portable-readers: build/cesta build/test-wire build/cetta-data-reader
	python3 -B tests/portable_readers.py $(PORTABLE_READER_TEST_FLAGS)


.PHONY: test-presentation-admission
test-presentation-admission: test-presentations test-presentation-coverage test-lean
	python3 -B tests/presentation_admission.py $(PRESENTATION_TEST_FLAGS)
	python3 -B tests/presentation_admission.py --input-prefix presentation-complete --output build/presentation-complete-admission $(COMPLETE_PRESENTATION_TEST_FLAGS)
	python3 -B tests/presentation_admission.py --input-prefix presentation-cmera-complete --output build/presentation-cmera-complete-admission $(COMPLETE_PRESENTATION_TEST_FLAGS)
	python3 -B tests/presentation_admission.py --input-prefix presentation-cmera-controls --output build/presentation-cmera-controls-admission $(COMPLETE_PRESENTATION_TEST_FLAGS)
	python3 -B tests/presentation_admission.py --input-prefix presentation-compact2-complete --output build/presentation-compact2-complete-admission $(COMPLETE_PRESENTATION_TEST_FLAGS)
	python3 -B tests/presentation_admission.py --input-prefix presentation-compact2-controls --output build/presentation-compact2-controls-admission $(COMPLETE_PRESENTATION_TEST_FLAGS)


build/test-presentation-coverage: tests/presentation_coverage.c $(LIBSOURCES) src/cesta.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/presentation_coverage.c $(LIBSOURCES) -o $@

.PHONY: test-presentation-coverage
test-presentation-coverage: build/test-presentation-coverage build/CGNU.cf
	./build/test-presentation-coverage
	./build/test-presentation-coverage presentation-cmera-complete cmera-style/1
	./build/test-presentation-coverage presentation-compact2-complete compact-c/2


build/test-whole-program-trees: tests/whole_program_trees.c $(LIBSOURCES) src/cesta.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/whole_program_trees.c $(LIBSOURCES) -o $@

.PHONY: test-whole-program-trees
test-whole-program-trees: build/test-whole-program-trees build/CGNU.cf
	./build/test-whole-program-trees 10000


.PHONY: test-build-dependencies
test-build-dependencies: build/cesta
	python3 -B tests/build_dependencies.py


.PHONY: test-compact-examples
test-compact-examples: build/cesta
	python3 -B tests/compact_examples.py


.PHONY: test-presentation-corpora
test-presentation-corpora: build/cesta build/cetta-data-reader
	python3 -B tests/presentation_qualification.py --reuse $(PRESENTATION_CORPUS_TEST_FLAGS)


.PHONY: test-presentation-semantics
test-presentation-semantics: build/cesta
	python3 -B tests/presentation_semantics.py


.PHONY: test-semantics test-semantic-corpora test-type-admission
test-semantics: build/cesta
	python3 -B tests/semantic_controls.py
	python3 -B tests/conversion_boundaries.py


test-semantic-corpora: build/cesta
	python3 -B tests/semantic_corpus.py $(SEMANTIC_CORPUS_TEST_FLAGS)


test-type-admission: test-semantic-corpora test-lean
	python3 -B tests/type_admission.py $(TYPE_ADMISSION_TEST_FLAGS)


.PHONY: check test-qualification-controls test-foundation
test-foundation: build/test-foundation
	./build/test-foundation

test-qualification-controls:
	python3 -B tests/qualification_controls.py


.PHONY: test-source test-source-core test-pp-core test-pp-corpora
test-source: build/cesta
	python3 -B tests/source_trees.py

test-source-core: build/cesta
	python3 -B tests/source_trees.py --core-only

test-pp-core: build/cesta
	python3 -B tests/pp_core.py

test-pp-corpora: build/cesta
	python3 -B tests/preprocessor_corpus.py $(PP_CORPUS_TEST_FLAGS)



.PHONY: test-compiler-builds test-provenance-corpora test-provenance-core
test-compiler-builds: build/cesta
	python3 -B tests/compiler_builds.py

test-provenance-corpora: build/cesta
	python3 -B tests/provenance_corpus.py $(PROVENANCE_CORPUS_TEST_FLAGS)

test-provenance-core: build/cesta
	python3 -B tests/provenance_controls.py


.PHONY: test-gnu-extensions test-half-floats
test-half-floats: build/cesta
	python3 -B tests/half_floats.py

test-gnu-extensions: build/cesta
	python3 -B tests/gnu_extensions.py


.PHONY: test-unicode-names
test-unicode-names: build/cesta
	python3 -B tests/unicode_names.py


.PHONY: test-source-driver
test-source-driver: build/cesta
	python3 -B tests/source_driver.py


.PHONY: test-schema-extension-admission
test-schema-extension-admission: build/cesta test-lean
	python3 -B tests/schema_extension_admission.py $(SCHEMA_EXTENSION_TEST_FLAGS)


build/conflict-certificate: tests/conflict_certificate.c $(LIBSOURCES) src/cesta.h src/sema.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/conflict_certificate.c $(LIBSOURCES) -o $@

.PHONY: test-conflict-admission
test-conflict-admission: build/conflict-certificate build/C11.cf build/CGNU.cf
	python3 -B tests/conflict_admission.py $(CONFLICT_TEST_FLAGS)


.PHONY: test-macro-export test-macro-program-export test-export-counters test-export-counters-cetta test-tiers
test-macro-program-export: build/cesta
	python3 -B tests/macro_program_export.py

test-macro-export: build/cesta
	python3 -B tests/macro_export.py

test-export-counters: build/cesta
	python3 -B tests/export_counters.py $(EXPORT_COUNTER_TEST_FLAGS)

test-export-counters-cetta: build/cesta
	python3 -B tests/export_counters.py --cetta $(CETTA_SOURCE) $(EXPORT_COUNTER_TEST_FLAGS)

.PHONY: test-export-admission
test-export-admission: build/cesta
	python3 -B tests/export_admission.py $(EXPORT_ADMISSION_TEST_FLAGS)

.PHONY: test-layouts test-layout-admission test-layout-cetta test-layout-edges test-layout-sanitizers test-layout-bridge test-layout-model
test-layouts: build/cesta
	python3 -B tests/layouts.py

test-layout-edges: build/cesta
	python3 -B tests/layout_edges.py

test-layout-sanitizers: build/cesta
	python3 -B tests/layout_edges.py --sanitize

test-layout-model: build/cesta
	python3 -B tests/lean_contracts.py --layouts-only $(LEAN_TEST_FLAGS)

test-layout-bridge: test-layout-model
	python3 -B tests/lean_contracts.py --layouts-only --layout-bridge $(LEAN_TEST_FLAGS)

test-layout-admission: build/cesta test-layout-model
	python3 -B tests/layout_admission.py $(LAYOUT_ADMISSION_TEST_FLAGS)

test-layout-cetta: build/cesta
	python3 -B tests/layout_cetta.py $(LAYOUT_CETTA_TEST_FLAGS)

.PHONY: test-operation-audit
test-operation-audit:
	python3 -B tests/operation_audit.py $(OPERATION_AUDIT_TEST_FLAGS)

.PHONY: test-operation-model test-operations
test-operation-model: build/cesta
	python3 -B tests/lean_contracts.py --operations-only $(LEAN_TEST_FLAGS)

test-operations: build/cesta test-operation-model test-table-model
	python3 -B tests/operations.py $(OPERATIONS_TEST_FLAGS)

.PHONY: test-operation-proposal
test-operation-proposal: build/cesta test-operation-model
	python3 -B tests/operation_proposal.py $(OPERATION_PROPOSAL_TEST_FLAGS)

.PHONY: test-tables test-table-model test-table-admission test-operation-protocol
test-tables: build/cesta
	python3 -B tests/tables.py

test-table-model: build/cesta
	python3 -B tests/lean_contracts.py --tables-only $(LEAN_TEST_FLAGS)

test-table-admission: build/cesta test-table-model
	python3 -B tests/table_admission.py $(TABLE_ADMISSION_TEST_FLAGS)

test-operation-protocol: build/cesta test-operation-model
	python3 -B tests/operation_protocol.py $(OPERATION_PROTOCOL_TEST_FLAGS)

.PHONY: test-protocols test-protocol-model test-protocol-admission test-protocol-loops test-protocol-loop-admission test-protocol-certificates test-protocol-call-once
test-protocols: build/cesta
	python3 -B tests/protocols.py

test-protocol-loops: build/cesta
	python3 -B tests/protocol_loops.py

test-protocol-certificates: build/cesta
	python3 -B tests/protocol_certificates.py

test-protocol-call-once: build/cesta
	python3 -B tests/protocol_call_once.py

test-protocol-loop-admission: build/cesta test-protocol-model
	python3 -B tests/protocol_loop_admission.py $(PROTOCOL_ADMISSION_TEST_FLAGS)

test-protocol-model: build/cesta
	python3 -B tests/lean_contracts.py --protocols-only $(LEAN_TEST_FLAGS)

test-protocol-admission: build/cesta test-protocol-model
	python3 -B tests/protocol_admission.py $(PROTOCOL_ADMISSION_TEST_FLAGS)

.PHONY: test-checked-core test-checked-libraries test-checked-imports test-checked-model test-checked-admission test-checked-sanitizers test-checked-generated test-checked-hints
test-checked-core: build/cesta
	python3 -B tests/checked_core.py

test-checked-hints: build/cesta
	python3 -B tests/checked_hints.py

test-checked-libraries: build/cesta
	python3 -B tests/checked_libraries.py

test-checked-imports: build/cesta
	python3 -B tests/checked_imports.py

test-checked-model: build/cesta
	python3 -B tests/lean_contracts.py --checked-only $(LEAN_TEST_FLAGS)

test-checked-admission: build/cesta test-checked-model
	python3 -B tests/checked_admission.py $(CHECKED_ADMISSION_TEST_FLAGS)

test-checked-sanitizers: build/cesta
	python3 -B tests/checked_core.py --sanitizers
	python3 -B tests/checked_libraries.py --sanitizers

test-checked-generated: build/cesta test-checked-model
	python3 -B tests/checked_fuzz.py $(CHECKED_GENERATED_TEST_FLAGS)

test-tiers:
	python3 -B tests/test_tiers.py

# Test tiers. check is bounded local smoke and refusal coverage with no
# external corpus or Lean build; test is the normal regression suite over both
# native compilers; qualify runs every gate, including corpora, compiler
# matrices and actual-data Lean admissions. test_tiers.py keeps every
# previously required gate inside qualify.
CHECK_GATES := test-tiers test-foundation test-source-core test-pp-core test-qualification-controls test-ownership-smoke \
	test-reference test-wire test-literal-bytes test-coverage test-presentations test-presentation-coverage \
	test-unicode-names test-source-driver test-driver test-macro-prescan test-macro-definition-ports \
	test-macro-export test-macro-program-export test-export-counters test-layouts test-layout-edges test-tables test-protocols test-protocol-loops \
	test-checked-core test-checked-libraries
TEST_GATES := $(CHECK_GATES) test-c test-modules test-semantics test-compiler-builds test-gnu-extensions test-half-floats test-ownership-core test-ownership-runtime test-ownership-views test-checked-hints test-protocol-certificates test-protocol-call-once \
	test-checked-imports \
	test-native-authoring test-one-language test-project-build test-macro-engine test-macro-native-names test-macro-types test-macro-narrowing \
	test-macro-worklists test-macro-sizes test-macro-probes test-provenance-core test-c-printer test-examples \
	test-compact-examples test-grammar-trees test-whole-program-trees test-presentation-driver \
	test-presentation-semantics test-build-dependencies
QUALIFY_GATES := $(TEST_GATES) test-corpora test-c11parser test-lean test-macro-admission \
	test-macro-port-admission test-metadata test-conflict-admission test-schema-extension-admission \
	test-type-admission test-presentation-admission test-presentation-corpora test-semantic-corpora \
	test-pp-corpora test-provenance-corpora test-mutants test-petta test-portable-readers test-workbench \
	test-source test-export-counters-cetta test-export-admission test-layout-admission test-layout-cetta \
	test-layout-sanitizers test-layout-bridge test-layout-model test-operation-audit test-operation-model \
	test-operations test-operation-proposal test-table-model test-table-admission test-operation-protocol \
	test-protocol-model test-protocol-admission test-protocol-loop-admission test-checked-model test-checked-admission test-checked-sanitizers test-checked-generated \
	test-ownership-model test-ownership-admission test-ownership-views-admission

.PHONY: test-ownership-smoke test-ownership-core test-ownership-runtime test-ownership-model test-ownership-admission test-ownership-views test-ownership-views-admission
test-ownership-smoke: build/cesta
	python3 -B tests/ownership_core.py --quick --output build/ownership-smoke.json

test-ownership-core: build/cesta
	python3 -B tests/ownership_core.py

test-ownership-runtime: build/cesta
	python3 -B tests/ownership_runtime.py

test-ownership-views: build/cesta
	python3 -B tests/ownership_views.py

test-ownership-views-admission: build/cesta test-ownership-model test-checked-model
	python3 -B tests/ownership_views.py --admit --output build/ownership-views-admission

test-ownership-model: build/cesta
	python3 -B tests/lean_contracts.py --ownership-only --output build/ownership-model

test-ownership-admission: build/cesta test-ownership-model
	python3 -B tests/ownership_admission.py

.PHONY: check test qualify
check: $(CHECK_GATES)
test: $(TEST_GATES)
qualify: $(QUALIFY_GATES)
