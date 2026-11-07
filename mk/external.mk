# Gates that need material outside CeTTa
#
# make test and make test-all never run these.  Each one checks CeTTa against
# something CeTTa does not ship: a proof development, a reference
# implementation, an external prover, or a published corpus.  A gate whose
# requirement is missing stops and names the setting it needs.  Run one with
#
#     make <gate> SETTING=<path>
#
# The settings, and where to get what they name:
#
# Proof developments
#   METTAPEDIA_LEAN_ROOT       The Mettapedia Lean project (lean/mettapedia in the
#                              MeTTapedia repository), with the modules a gate
#                              imports built by `lake build`.  By default, a
#                              Mettapedia checkout beside this repository.
#                              The NIK exporter gates (test-prime-nik-*, from the
#                              tests/support/export_prime_nik_*.lean exporters)
#                              need these modules built in that project:
#                                lake build \
#                                  Mettapedia.Languages.MeTTa.PrimeCandidates.MinimalCheckingPackage \
#                                  Mettapedia.Languages.Megalodon.ImplicationalKernel \
#                                  Mettapedia.Languages.Megalodon.TheoryAdmissionKernel \
#                                  Mettapedia.Languages.Megalodon.DefinitionConversionWireRefinement \
#                                  Mettapedia.Languages.Megalodon.EnvironmentKernel \
#                                  Mettapedia.Languages.Megalodon.TermQuantifiedKernel \
#                                  Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender \
#                                  Mettapedia.GSLT.LanguageDef.InferencePresentationWire \
#                                  Mettapedia.GSLT.LanguageDef.InferenceABTWireRefinement
#   METAMATH_MMLEAN4_ROOT_V1,  mm-lean4, the Lean-verified Metamath checker, and
#   METAMATH_MMLEAN4_BIN_V1    its built binary.
#
# Reference implementations
#   GSLT2PARSE_PETTA_ROOT,     SWI-PeTTa at the corrected-stable revision that
#   PETTA_ORACLE_ROOT,         tools/check_petta_release_reference.py pins (the
#   PETTA_REFERENCE_ROOT       stable-fixed branch of PeTTa).  The gslt2parse
#                              gates run their own Prolog oracles from this
#                              repository; the PeTTa parser authority, and the
#                              document splitter that reads with it, run from
#                              GSLT2PARSE_PETTA_ROOT/experiments/gslt2parse_foundation/:
#                              link that directory to this repository's
#                              experiments/gslt2parse_foundation/prolog.
#   GSLT2PARSE_HE_ROOT         A git checkout of hyperon-experimental at its v0.2.10
#                              release (commit 3f76dc46); the gates read its
#                              revision with git.
#   MEGALODON_ROOT             A Megalodon checkout with its examples, and with
#                              its binary built for the elaboration and megaauto
#                              corpora.
#   PETTA_CHAINER_ROOT         A PeTTaChainer checkout.
#   ASP_CLINGO_PYTHON          A Python 3 with the clingo module (pip install
#                              clingo): the answer-set differential compares the
#                              solver's stable models with clingo's.
#   CETTA_CLINGO_LIBRARY       A libclingo shared library (clingo 5.x, built from
#                              the potassco/clingo sources with
#                              CLINGO_BUILD_SHARED): the asp library's clingo
#                              heads ground program texts and answer reference
#                              models through it.
#
# External provers
#   TPTP_EPROVER_V1,           The E prover and Vampire binaries.
#   TPTP_VAMPIRE_V1
#
# Published corpora
#   TPTP_OFFICIAL_SYNTAX_BNF_V1  The TPTP distribution's official SyntaxBNF, at the
#                                pinned release.
#   TPTP_OFFICIAL_CORPUS_ROOT_V1 The TPTP distribution's Problems directory.
#   METAMATH_SET_MM_V1         The canonical set.mm (the metamath/set.mm
#                              repository).
#   METAMATH_TEST_ROOT_V1      The metamath-test conformance corpus.
#   HOPPER_TABLE1_ROOT,        The pinned Hopper, IGGP and infcontrol checkouts
#   IGGP_ROOT, INFCONTROL_REPO the ILP benchmark gates compare against.
#   *_MEMORY_BENCH_SOURCE      A program or Metamath source for a memory
#                              benchmark.

.PHONY: test-execution-theory

.PHONY: test-gslt2parse-rho-syntax-convergence-v1

.PHONY: test-gslt2parse-parser-pack-lexical-plan-v1 test-gslt2parse-parser-pack-guarded-lexical-v1

.PHONY: qualify-json-projection-refinement-lean-v1

.PHONY: qualify-json-gslt-native-forest-exact-v1-body

.PHONY: qualify-metta-atom-actions-lean-native-v1

.PHONY: qualify-grammar-constructor-actions-lean-native-v1

.PHONY: test-tptp-official-syntax-gate-failure-v1

.PHONY: test-tptp-official-compact-resolution-consumer-v1

.PHONY: test-tptp-official-compact-include-resolution-real-v1

.PHONY: test-tptp-official-reader-canonical-print-real-v1

.PHONY: refresh-petta-reference-vocabulary-v1

.PHONY: qualify-tensor-support-lean

.PHONY: qualify-collection-theories-lean

.PHONY: test-tptp-official-reader-utilities-real-v1

.PHONY: test-tptp-official-reusable-reader-v1

.PHONY: qualify-tptp-reader-source-lean-v1

.PHONY: test-tptp-extended-bnf-full-reader-v1-body

.PHONY: test-tptp-extended-bnf-meta-parser-v1-body

.PHONY: qualify-plain-bnf-denotation-lean-native-v1-body

.PHONY: qualify-plain-bnf-authored-source-lean-v1

.PHONY: qualify-plain-bnf-selected-native-type-lean-v1

.PHONY: qualify-ebnf-authored-source-lean-v1

.PHONY: check-tptp-official-semantic-carrier-wire-v1

.PHONY: check-tptp-official-ground-resolution-semantic-canary-v1

.PHONY: check-tptp-ground-resolution-word-canary-v1

.PHONY: check-derivation-word-machine-wire-v1

.PHONY: check-tptp-official-include-input-classification-v1

.PHONY: check-tptp-official-include-environment-lookup-v1

.PHONY: check-tptp-official-include-selection-v1

.PHONY: check-tptp-official-include-resolution-result-carrier-v1

.PHONY: check-tptp-official-include-resolution-carrier-v1

.PHONY: check-tptp-official-include-directive-language-def-v1

.PHONY: check-tptp-fof-transformation-language-defs-v1

.PHONY: check-tptp-fof-skolemization-ntt-wires-v1

.PHONY: check-tptp-fof-cnf-syntax-tree-wire-v1

.PHONY: check-first-order-resolution-example-trace-wire-v1

.PHONY: check-first-order-resolution-input-wire-v1

.PHONY: check-json-value-language-def-wire-v1

.PHONY: check-rfc8259-json-parser-profile-wire-v1

.PHONY: check-rfc8259-json-syntax-wire-v1

.PHONY: check-exact-arithmetic-wire-v1

.PHONY: check-arithmetic-target-coproduct-wire-v1

.PHONY: check-structured-c-wire-v1

.PHONY: check-external-call-machine-wire-v1

.PHONY: check-radix-digit-language-def-wire-v1

.PHONY: check-walters-zantema-da-wire-v1

.PHONY: require-mettapedia-root-for-wire-provenance-v1

.PHONY: test-prime-nik-qualification-v1

.PHONY: test-prime-nik-megalodon-definition-v1

.PHONY: test-prime-nik-megalodon-known-implication-v1

.PHONY: test-prime-nik-megalodon-polymorphic-v1

.PHONY: test-prime-nik-megalodon-term-v1

.PHONY: qualify-prepared-scalar-lean

.PHONY: probe-prime-hopper-table1-source

.PHONY: test-tptp-reader-artifact-reproducibility-v1

.PHONY: qualify-tptp-frozen-corpus-compatible-v1

.PHONY: qualify-tptp-frozen-full-corpus-v1

.PHONY: qualify-tptp-frozen-full-corpus-v1-body

.PHONY: benchmark-tptp-frozen-size-ladder-v1

.PHONY: benchmark-tptp-frozen-size-ladder-v1-body

.PHONY: qualify-tptp-external-prover-fixtures-v1

.PHONY: test-metamath-cogslt-generated-relational-corpus-oracle-v1

.PHONY: test-metamath-cogslt-corpus-differential-v1 \
		test-metamath-cogslt-corpus-differential-core-v1

.PHONY: test-metamath-cogslt-set-mm-v1 \
	test-metamath-cogslt-set-mm-core-v1

.PHONY: test-metamath-cogslt-petta-proof-trace-horn-oracle-v1

.PHONY: test-metamath-cogslt-petta-state-transition-horn-oracle-v1

.PHONY: test-petta-finite-horn-runtime-oracle-v1

.PHONY: bench-metamath-cogslt-petta-direct-program-memory-v1 \
		bench-metamath-cogslt-petta-direct-program-memory-core-v1

.PHONY: bench-metamath-direct-petta-reference-memory-v1

.PHONY: bench-metamath-verifier-memory-v1

.PHONY: test-metamath-cogslt-petta-direct-swi-compat-v1

.PHONY: probe-metamath-cogslt-petta-direct-corpus-v1 \
		probe-metamath-cogslt-petta-direct-corpus-core-v1

.PHONY: benchmark-metamath-state-transition-direct-closed-petta-v1

.PHONY: test-metamath-bulk-actions-direct-swi-compat-v1

.PHONY: test-pattern-lean-models

test-pattern-lean-models:
	@test -n "$(METTAPEDIA_ROOT)" || { echo 'Set METTAPEDIA_ROOT to the Lean project root'; exit 1; }
	@python3 tests/support/check_pattern_lean_models.py --mettapedia "$(METTAPEDIA_ROOT)" \
		--receipt "$(PATTERN_MODEL_RECEIPT)"

test-metamath-bulk-actions-direct-swi-compat-v1: \
		test-metamath-insert-each-matching-direct-petta-v1 \
		test-metamath-copy-actions-direct-petta-v1 \
		$(METAMATH_ROW_ACTIONS_DIRECT_PETTA_PROGRAM_V1)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" || \
		! -x "$(GSLT2PARSE_PETTA_ROOT)/run.sh" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned SWI PeTTa checkout'; \
		exit 2; \
	fi
	@set -euo pipefail; \
	work=$$(mktemp -d runtime/metamath-bulk-actions-swi-compat.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	for item in \
		'$(METAMATH_INSERT_EACH_MATCHING_DIRECT_PETTA_QUERY_V1):1,47p:MetamathInsertEachMatchingMatchedPeTTaV1' \
		'$(METAMATH_INSERT_EACH_MATCHING_DIRECT_PETTA_QUERY_V1):49,78p:MetamathInsertEachMatchingMissedPeTTaV1' \
		'$(METAMATH_INSERT_EACH_MATCHING_DIRECT_PETTA_QUERY_V1):80,119p:MetamathInsertEachMatchingRefusedPeTTaV1' \
		'$(METAMATH_COPY_ACTIONS_DIRECT_PETTA_QUERY_V1):1,34p:MetamathCopyEachRowAcceptedPeTTaV1' \
		'$(METAMATH_COPY_ACTIONS_DIRECT_PETTA_QUERY_V1):36,68p:MetamathCopyEachRowRefusedPeTTaV1' \
		'$(METAMATH_COPY_ACTIONS_DIRECT_PETTA_QUERY_V1):70,121p:MetamathCopyEachRowMatchingAcceptedPeTTaV1' \
		'$(METAMATH_COPY_ACTIONS_DIRECT_PETTA_QUERY_V1):123,167p:MetamathCopyEachRowMatchingRefusedPeTTaV1' \
		'$(METAMATH_COPY_ACTIONS_DIRECT_PETTA_QUERY_V1):169,204p:MetamathCopyEachRowMatchingMissedPeTTaV1'; do \
		source=$${item%%:*}; rest=$${item#*:}; \
		range=$${rest%%:*}; observation=$${rest#*:}; \
		awk '1' $(METAMATH_ROW_ACTIONS_DIRECT_PETTA_PROGRAM_V1) \
			> "$$work/run.metta"; \
		sed -n "$$range" "$$source" >> "$$work/run.metta"; \
		c_output=$$(timeout 30 $(CETTA_PETTA_DIRECT_RUN_V1) \
			"$$work/run.metta" 2>&1); \
		swi_output=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 30 \
			sh run.sh --silent "$(abspath $$work/run.metta)" 2>&1); \
		printf '%s\n' "$$c_output" | rg -F -q "$$observation"; \
		printf '%s\n' "$$swi_output" | rg -F -q "$$observation"; \
		c_observed=$$(printf '%s\n' "$$c_output" | rg '^[(][(]'); \
		swi_observed=$$(printf '%s\n' "$$swi_output" | rg '^[(][(]'); \
		test "$$(printf '%s\n' "$$c_observed" | wc -l)" -eq 1; \
		test "$$(printf '%s\n' "$$swi_observed" | wc -l)" -eq 1; \
		test "$$c_observed" = "$$swi_observed"; \
	done; \
	printf '%s\n' '(MetamathBulkActionsDirectSwiCompatibilityV1Summary 8 8 0)'

benchmark-metamath-state-transition-direct-closed-petta-v1: \
		$(METAMATH_STATE_TRANSITION_DIRECT_ENTRY_MODE_BENCHMARK_V1) \
		$(BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" || \
		! -x "$(GSLT2PARSE_PETTA_ROOT)/run.sh" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned SWI PeTTa checkout'; \
		exit 2; \
	fi
	@set -euo pipefail; \
	swi_output=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 30 \
		sh run.sh --silent \
		"$(abspath $(METAMATH_STATE_TRANSITION_DIRECT_ENTRY_MODE_BENCHMARK_V1))" \
		2>&1); \
	printf '%s\n' "$$swi_output" | rg -F -q \
		'((MetamathStateTransitionDirectEntryModeGoodV1 AcceptedV1 SequenceFourV1))'; \
	printf '%s\n' "$$swi_output" | rg -F -q \
		'((MetamathStateTransitionDirectEntryModeRepeatedV1 RefusedKeyPresentV1 RollbackSequenceZeroV1))'; \
	set +e; \
	c_output=$$(timeout $(METAMATH_STATE_TRANSITION_DIRECT_ENTRY_MODE_TIMEOUT_V1) \
		$(CETTA_PETTA_DIRECT_RUN_V1) \
		$(METAMATH_STATE_TRANSITION_DIRECT_ENTRY_MODE_BENCHMARK_V1) \
		2>&1); \
	c_status=$$?; \
	set -e; \
	if [[ $$c_status -eq 124 ]]; then \
		printf 'c.Petta exceeded %ss on the direct closed residual; SWI PeTTa produced both expected observations.\n' \
			$(METAMATH_STATE_TRANSITION_DIRECT_ENTRY_MODE_TIMEOUT_V1); \
		exit 124; \
	fi; \
	test $$c_status -eq 0; \
	printf '%s\n' "$$c_output" | rg -F -q \
		'((MetamathStateTransitionDirectEntryModeGoodV1 AcceptedV1 SequenceFourV1))'; \
	printf '%s\n' "$$c_output" | rg -F -q \
		'((MetamathStateTransitionDirectEntryModeRepeatedV1 RefusedKeyPresentV1 RollbackSequenceZeroV1))'; \
	printf '%s\n' 'c.Petta and SWI PeTTa produced both expected observations.'

probe-metamath-cogslt-petta-direct-corpus-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		METAMATH_TEST_ROOT_V1="$(METAMATH_TEST_ROOT_V1)" \
		METAMATH_DIRECT_PETTA_CORPUS_RESULTS_V1="$(METAMATH_DIRECT_PETTA_CORPUS_RESULTS_V1)" \
		METAMATH_DIRECT_PETTA_CORPUS_LIMIT_V1="$(METAMATH_DIRECT_PETTA_CORPUS_LIMIT_V1)" \
		METAMATH_DIRECT_PETTA_CORPUS_FILTER_V1="$(METAMATH_DIRECT_PETTA_CORPUS_FILTER_V1)" \
		METAMATH_DIRECT_PETTA_CORPUS_STAGE_TIMEOUT_V1="$(METAMATH_DIRECT_PETTA_CORPUS_STAGE_TIMEOUT_V1)" \
		probe-metamath-cogslt-petta-direct-corpus-core-v1

probe-metamath-cogslt-petta-direct-corpus-core-v1: \
		test-metamath-cogslt-petta-direct-outcome-driver-core-v1 \
		$(LANGDEF_DIRECT_PETTA_CORPUS_DRIVER_V1)
	@if [[ -z "$(strip $(METAMATH_TEST_ROOT_V1))" ]]; then \
		echo 'set METAMATH_TEST_ROOT_V1 to the conformance corpus checkout'; \
		exit 1; \
	fi
	@if [[ "$$(git -C "$(METAMATH_TEST_ROOT_V1)" rev-parse HEAD)" != \
		"$(METAMATH_TEST_COMMIT_V1)" ]]; then \
		echo 'Metamath conformance corpus revision does not match the pinned suite'; \
		exit 1; \
	fi
	@if ! git -C "$(METAMATH_TEST_ROOT_V1)" diff --quiet || \
		! git -C "$(METAMATH_TEST_ROOT_V1)" diff --cached --quiet; then \
		echo 'Metamath conformance corpus tracked source is dirty'; \
		exit 1; \
	fi
	@bash -n $(LANGDEF_DIRECT_PETTA_CORPUS_DRIVER_V1)
	@shellcheck $(LANGDEF_DIRECT_PETTA_CORPUS_DRIVER_V1)
	@CETTA_DIRECT_PETTA_ROOT="$(CURDIR)" \
		CETTA_DIRECT_PETTA_BIN="$(abspath $(BIN))" \
		CETTA_DIRECT_PETTA_PROGRAM="$(abspath $(METAMATH_STATE_PROOF_BRIDGE_DIRECT_PROGRAM_V1))" \
		CETTA_DIRECT_PETTA_QUERY="$(abspath $(METAMATH_STATE_PROOF_OCCURRENCE_OUTCOME_QUERY_V1))" \
		CETTA_DIRECT_PETTA_MANIFEST="$(abspath $(METAMATH_DIRECT_RUNTIME_MANIFEST_V1))" \
		CETTA_DIRECT_PETTA_TIMEOUT="$(METAMATH_DIRECT_PETTA_CORPUS_STAGE_TIMEOUT_V1)" \
		CETTA_DIRECT_PETTA_DRIVER="$(abspath $(LANGDEF_DIRECT_PETTA_OUTCOME_DRIVER_V1))" \
		CETTA_DIRECT_PETTA_TEST_ROOT="$(abspath $(METAMATH_TEST_ROOT_V1))" \
		CETTA_DIRECT_PETTA_RESULTS="$(abspath $(METAMATH_DIRECT_PETTA_CORPUS_RESULTS_V1))" \
		CETTA_DIRECT_PETTA_CORPUS_LIMIT="$(METAMATH_DIRECT_PETTA_CORPUS_LIMIT_V1)" \
		CETTA_DIRECT_PETTA_CORPUS_FILTER="$(METAMATH_DIRECT_PETTA_CORPUS_FILTER_V1)" \
		$(LANGDEF_DIRECT_PETTA_CORPUS_DRIVER_V1)

# Cross-runner target-language canaries only.  Full .mm sources stay runtime
# parser inputs under CeTTa; they are never materialized into PeTTa programs to
# manufacture an SWI compatibility case.
test-metamath-cogslt-petta-direct-swi-compat-v1: \
		test-metamath-cogslt-petta-direct-v1
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" || \
		! -x "$(GSLT2PARSE_PETTA_ROOT)/run.sh" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned SWI PeTTa checkout'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	work=$$(mktemp -d runtime/metamath-direct-swi-compat.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	awk '1' $(METAMATH_STATE_TRANSITION_DIRECT_PROGRAM_V1) \
		$(METAMATH_STATE_TRANSITION_DIRECT_QUERY_V1) \
		> "$$work/state.metta"; \
	awk '1' $(METAMATH_PROOF_TRACE_DIRECT_PROGRAM_V1) \
		$(METAMATH_PROOF_TRACE_DIRECT_QUERY_V1) \
		> "$$work/proof.metta"; \
	for lane in relation state proof; do \
		case "$$lane" in \
			relation) source=$(GSLT_DIRECT_PETTA_QUERY_V1) ;; \
			state) source="$$work/state.metta" ;; \
			proof) source="$$work/proof.metta" ;; \
		esac; \
		timeout 60 $(CETTA_PETTA_DIRECT_RUN_V1) "$$source" \
			> "$$work/$$lane.cetta" 2> "$$work/$$lane.cetta.stderr"; \
		(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 60 \
			sh run.sh --silent "$(abspath $$source)") \
			> "$$work/$$lane.swi" 2> "$$work/$$lane.swi.stderr"; \
		cmp -s "$$work/$$lane.cetta" "$$work/$$lane.swi"; \
	done; \
	printf '%s\n' '(MetamathCoGSLTDirectSWIPeTTaCompatV1Summary 3 0)'

bench-metamath-verifier-memory-v1:
	@if [[ -z "$(strip $(METAMATH_VERIFIER_MEMORY_BENCH_SOURCE))" || \
		! -f "$(METAMATH_VERIFIER_MEMORY_BENCH_SOURCE)" ]]; then \
		echo 'set METAMATH_VERIFIER_MEMORY_BENCH_SOURCE to a Metamath source'; \
		exit 1; \
	fi
	@METAMATH_VERIFIER_BENCH_TIMEOUT="$(METAMATH_VERIFIER_MEMORY_BENCH_TIMEOUT)" \
		tools/benchmark_metamath_verifier_memory_v1.sh \
		"$(METAMATH_VERIFIER_MEMORY_BENCH_SOURCE)"

bench-metamath-direct-petta-reference-memory-v1: \
		$(LANGDEF_DIRECT_PETTA_OUTCOME_DRIVER_V1) \
		$(METAMATH_STATE_PROOF_BRIDGE_DIRECT_PROGRAM_V1) \
		$(METAMATH_STATE_PROOF_OCCURRENCE_OUTCOME_QUERY_V1) \
		$(METAMATH_DIRECT_COMPILED_CURSOR_V1) \
		$(METAMATH_DIRECT_RUNTIME_LOCK_V1) \
		$(METAMATH_DIRECT_RUNTIME_MANIFEST_V1) \
		$(METAMATH_COGSLT_CORE_BIN_V1)
	@if [[ -z "$(strip $(METAMATH_VERIFIER_MEMORY_BENCH_SOURCE))" || \
		! -f "$(METAMATH_VERIFIER_MEMORY_BENCH_SOURCE)" ]]; then \
		echo 'set METAMATH_VERIFIER_MEMORY_BENCH_SOURCE to a Metamath source'; \
		exit 2; \
	fi
	@CETTA_DIRECT_PETTA_DRIVER="$(abspath $(LANGDEF_DIRECT_PETTA_OUTCOME_DRIVER_V1))" \
		CETTA_DIRECT_PETTA_BIN="$(abspath $(METAMATH_COGSLT_CORE_BIN_V1))" \
		CETTA_DIRECT_PETTA_PROGRAM="$(abspath $(METAMATH_STATE_PROOF_BRIDGE_DIRECT_PROGRAM_V1))" \
		CETTA_DIRECT_PETTA_QUERY="$(abspath $(METAMATH_STATE_PROOF_OCCURRENCE_OUTCOME_QUERY_V1))" \
		CETTA_DIRECT_PETTA_MANIFEST="$(abspath $(METAMATH_DIRECT_RUNTIME_MANIFEST_V1))" \
		METAMATH_VERIFIER_BENCH_TIMEOUT="$(METAMATH_VERIFIER_MEMORY_BENCH_TIMEOUT)" \
		tools/benchmark_metamath_verifier_memory_v1.sh \
		"$(METAMATH_VERIFIER_MEMORY_BENCH_SOURCE)"

bench-metamath-cogslt-petta-direct-program-memory-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		METAMATH_DIRECT_PETTA_MEMORY_BENCH_SOURCE="$(METAMATH_DIRECT_PETTA_MEMORY_BENCH_SOURCE)" \
		METAMATH_DIRECT_PETTA_MEMORY_BENCH_STATS="$(METAMATH_DIRECT_PETTA_MEMORY_BENCH_STATS)" \
		PETTA_PROGRAM_MEMORY_BENCH_TIMEOUT="$(PETTA_PROGRAM_MEMORY_BENCH_TIMEOUT)" \
		bench-metamath-cogslt-petta-direct-program-memory-core-v1

bench-metamath-cogslt-petta-direct-program-memory-core-v1: \
		$(LANGDEF_DIRECT_PETTA_OUTCOME_DRIVER_V1) \
		$(METAMATH_STATE_PROOF_BRIDGE_DIRECT_PROGRAM_V1) \
		$(METAMATH_STATE_PROOF_OCCURRENCE_OUTCOME_QUERY_V1) \
		$(METAMATH_DIRECT_COMPILED_CURSOR_V1) \
		$(METAMATH_DIRECT_RUNTIME_LOCK_V1) \
		$(METAMATH_DIRECT_RUNTIME_MANIFEST_V1) \
		$(BIN)
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'the direct PeTTa memory benchmark requires the C-only build'; \
		exit 1; \
	fi
	@if ldd $(BIN) $(METAMATH_DIRECT_COMPILED_CURSOR_V1) | \
		rg -qi 'python|libswipl|prolog'; \
		then exit 1; fi
	@if [[ -z "$(strip $(METAMATH_DIRECT_PETTA_MEMORY_BENCH_SOURCE))" || \
		! -f "$(METAMATH_DIRECT_PETTA_MEMORY_BENCH_SOURCE)" ]]; then \
		echo 'set METAMATH_DIRECT_PETTA_MEMORY_BENCH_SOURCE to a Metamath source'; \
		exit 1; \
	fi
	@mkdir -p "$(dir $(METAMATH_DIRECT_PETTA_MEMORY_BENCH_STATS))"
	@set -euo pipefail; \
	work=$$(mktemp -d runtime/metamath-direct-petta-memory.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	/usr/bin/time -f 'elapsed_seconds=%e maxrss_kb=%M' \
		-o "$(METAMATH_DIRECT_PETTA_MEMORY_BENCH_STATS)" \
		env CETTA_DIRECT_PETTA_ROOT="$(CURDIR)" \
		CETTA_DIRECT_PETTA_BIN="$(abspath $(BIN))" \
		CETTA_DIRECT_PETTA_PROGRAM="$(abspath $(METAMATH_STATE_PROOF_BRIDGE_DIRECT_PROGRAM_V1))" \
		CETTA_DIRECT_PETTA_QUERY="$(abspath $(METAMATH_STATE_PROOF_OCCURRENCE_OUTCOME_QUERY_V1))" \
		CETTA_DIRECT_PETTA_MANIFEST="$(abspath $(METAMATH_DIRECT_RUNTIME_MANIFEST_V1))" \
		CETTA_DIRECT_PETTA_TIMEOUT="$(PETTA_PROGRAM_MEMORY_BENCH_TIMEOUT)" \
		$(LANGDEF_DIRECT_PETTA_OUTCOME_DRIVER_V1) \
		"$(METAMATH_DIRECT_PETTA_MEMORY_BENCH_SOURCE)" \
		>"$$work/outcome"; \
	test "$$(rg -c '^[(]MetamathDirectPeTTaOutcomeV1 ' "$$work/outcome")" -eq 1; \
	cat "$(METAMATH_DIRECT_PETTA_MEMORY_BENCH_STATS)"; \
	cat "$$work/outcome"

test-petta-finite-horn-runtime-oracle-v1: \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1) \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_RECEIPT_V1)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" || \
		! -x "$(GSLT2PARSE_PETTA_ROOT)/run.sh" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	before=$$(sha256sum \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1) | cut -d' ' -f1); \
	receipt_before=$$(sha256sum \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_RECEIPT_V1) | cut -d' ' -f1); \
	$(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(PETTA_FINITE_HORN_RUNTIME_CANARY_V1) \
		--epilogue $(PETTA_FINITE_HORN_RUNTIME_CANARY_QUERY_V1) \
		--out $(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1) \
		--receipt-out \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_RECEIPT_V1) >/dev/null; \
	after=$$(sha256sum \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1) | cut -d' ' -f1); \
	receipt_after=$$(sha256sum \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_RECEIPT_V1) | cut -d' ' -f1); \
	test "$$before" = "$$after"; \
	test "$$receipt_before" = "$$receipt_after"; \
	$(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(PETTA_FINITE_HORN_RUNTIME_CANARY_V1) \
		--epilogue $(PETTA_FINITE_HORN_RUNTIME_CANARY_QUERY_V1) \
		--program $(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1) \
		--receipt $(PETTA_FINITE_HORN_RUNTIME_CANARY_RECEIPT_V1) \
		>/dev/null; \
	work=$$(mktemp -d runtime/petta-horn-joint-mutation.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	sed '$$a; joint-artifact-receipt-mutation-v1' \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1) \
		>"$$work/program.metta"; \
	forged_digest=$$(sha256sum "$$work/program.metta" | cut -d' ' -f1); \
	awk -v digest="$$forged_digest" \
		'$$1 == "(artifact-sha256" { \
			print "  (artifact-sha256 \"" digest "\"))"; next \
		} { print }' \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_RECEIPT_V1) \
		>"$$work/receipt.metta"; \
	if $(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(PETTA_FINITE_HORN_RUNTIME_CANARY_V1) \
		--epilogue $(PETTA_FINITE_HORN_RUNTIME_CANARY_QUERY_V1) \
		--program "$$work/program.metta" \
		--receipt "$$work/receipt.metta" >/dev/null 2>&1; then \
		echo 'joint PeTTa Horn artifact/receipt mutation was admitted' >&2; \
		exit 1; \
	fi; \
	output=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 30 \
		sh run.sh --silent \
		"$(abspath $(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1))" 2>&1); \
	printf '%s\n' "$$output" | rg -F -q \
		'(CanaryAncestorV1 CanaryAliceV1 CanaryBobV1)'; \
	printf '%s\n' "$$output" | rg -F -q \
		'(CanaryAncestorV1 CanaryAliceV1 CanaryCaraV1)'; \
	printf '%s\n' "$$output" | rg -F -q \
		'(CanaryAncestorV1 CanaryAliceV1 CanaryDaraV1)'; \
	receipt=$$(rg -o \
		'FiniteHornPackageDigestV1 sha256-[0-9a-f]{64}' \
		$(PETTA_FINITE_HORN_RUNTIME_CANARY_PROGRAM_V1)); \
	printf '%s\n' "$$output" | rg -F -q "$$receipt"; \
	test "$$(printf '%s\n' "$$output" | rg -c '^[(][)]$$')" -eq 1; \
	printf '%s\n' '(PeTTaFiniteHornRuntimeV1Summary 7 7 0)'

test-metamath-cogslt-petta-state-transition-horn-oracle-v1: \
		test-petta-finite-horn-runtime-oracle-v1 \
		$(METAMATH_STATE_TRANSITION_PETTA_PROGRAM_V1) \
		$(METAMATH_STATE_TRANSITION_PETTA_RECEIPT_V1) \
		$(METAMATH_STATE_TRANSITION_PETTA_SEVERED_PROGRAM_V1) \
		$(METAMATH_STATE_TRANSITION_PETTA_SEVERED_RECEIPT_V1) \
		$(METAMATH_STATE_TRANSITION_PETTA_DRIFTED_PROGRAM_V1)
	@set -euo pipefail; \
	$(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(RELATIONAL_STATE_TRANSITION_CORE_V1) \
		--source $(RELATIONAL_STATE_EXECUTION_LINK_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXECUTION_CANARY_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXEC_IR_FACTS_V1) \
		--epilogue $(METAMATH_STATE_TRANSITION_PETTA_QUERY_V1) \
		--program $(METAMATH_STATE_TRANSITION_PETTA_PROGRAM_V1) \
		--receipt $(METAMATH_STATE_TRANSITION_PETTA_RECEIPT_V1) \
		>/dev/null; \
	if $(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(RELATIONAL_STATE_TRANSITION_CORE_V1) \
		--source $(RELATIONAL_STATE_EXECUTION_LINK_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXECUTION_CANARY_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXEC_IR_FACTS_V1) \
		--epilogue $(METAMATH_STATE_TRANSITION_PETTA_QUERY_V1) \
		--program $(METAMATH_STATE_TRANSITION_PETTA_DRIFTED_PROGRAM_V1) \
		--receipt $(METAMATH_STATE_TRANSITION_PETTA_RECEIPT_V1) \
		>/dev/null 2>&1; then exit 1; fi; \
	if $(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(RELATIONAL_STATE_TRANSITION_CORE_V1) \
		--source $(METAMATH_STATE_TRANSITION_PETTA_DELETED_LINK_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXECUTION_CANARY_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXEC_IR_FACTS_V1) \
		--epilogue $(METAMATH_STATE_TRANSITION_PETTA_QUERY_V1) \
		--program $(METAMATH_STATE_TRANSITION_PETTA_PROGRAM_V1) \
		--receipt $(METAMATH_STATE_TRANSITION_PETTA_RECEIPT_V1) \
		>/dev/null 2>&1; then exit 1; fi; \
	$(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		--source $(RELATIONAL_STATE_TRANSITION_CORE_V1) \
		--source $(METAMATH_STATE_TRANSITION_PETTA_DELETED_LINK_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXECUTION_CANARY_V1) \
		--source $(METAMATH_STATE_TRANSITION_EXEC_IR_FACTS_V1) \
		--epilogue $(METAMATH_STATE_TRANSITION_PETTA_QUERY_V1) \
		--program $(METAMATH_STATE_TRANSITION_PETTA_SEVERED_PROGRAM_V1) \
		--receipt $(METAMATH_STATE_TRANSITION_PETTA_SEVERED_RECEIPT_V1) \
		>/dev/null; \
	output=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 60 \
		sh run.sh --silent \
		"$(abspath $(METAMATH_STATE_TRANSITION_PETTA_PROGRAM_V1))" 2>&1); \
	printf '%s\n' "$$output" | rg -F -q \
		'((MetamathStateTransitionPeTTaGoodV1 AcceptedV1 SequenceFourV1))'; \
	printf '%s\n' "$$output" | rg -F -q \
		'((MetamathStateTransitionPeTTaRepeatedV1 RefusedKeyPresentV1 RollbackSequenceZeroV1))'; \
	severed=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 60 \
		sh run.sh --silent \
		"$(abspath $(METAMATH_STATE_TRANSITION_PETTA_SEVERED_PROGRAM_V1))" 2>&1); \
	test "$$(printf '%s\n' "$$severed" | rg -c '^[(][)]$$')" -eq 2; \
	if printf '%s\n' "$$severed" | \
		rg -q 'MetamathStateTransitionPeTTa(Good|Repeated)V1'; then \
		exit 1; \
	fi; \
	printf '%s\n' '(MetamathStateTransitionPeTTaV1Summary 185 2 2 1 1 0)'

test-metamath-cogslt-petta-proof-trace-horn-oracle-v1: \
		test-metamath-cogslt-proof-trace-semantics-v1 \
		test-petta-finite-horn-runtime-oracle-v1 \
		$(METAMATH_PROOF_TRACE_PETTA_PROGRAM_V1) \
		$(METAMATH_PROOF_TRACE_PETTA_RECEIPT_V1) \
		$(METAMATH_PROOF_TRACE_PETTA_SEVERED_PROGRAM_V1) \
		$(METAMATH_PROOF_TRACE_PETTA_SEVERED_RECEIPT_V1)
	@set -euo pipefail; \
	before=$$(sha256sum \
		$(METAMATH_PROOF_TRACE_PETTA_PROGRAM_V1) | cut -d' ' -f1); \
	receipt_before=$$(sha256sum \
		$(METAMATH_PROOF_TRACE_PETTA_RECEIPT_V1) | cut -d' ' -f1); \
	$(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		$(foreach source,$(METAMATH_PROOF_TRACE_PETTA_SOURCES_V1),--source $(source)) \
		--epilogue $(METAMATH_PROOF_TRACE_PETTA_QUERY_V1) \
		--out $(METAMATH_PROOF_TRACE_PETTA_PROGRAM_V1) \
		--receipt-out $(METAMATH_PROOF_TRACE_PETTA_RECEIPT_V1) \
		>/dev/null; \
	after=$$(sha256sum \
		$(METAMATH_PROOF_TRACE_PETTA_PROGRAM_V1) | cut -d' ' -f1); \
	receipt_after=$$(sha256sum \
		$(METAMATH_PROOF_TRACE_PETTA_RECEIPT_V1) | cut -d' ' -f1); \
	test "$$before" = "$$after"; \
	test "$$receipt_before" = "$$receipt_after"; \
	$(RELATIONAL_STATE_TRANSITION_LANGDEF_CORE_V1) oracle-petta-horn-check \
		--runtime $(PETTA_FINITE_HORN_RUNTIME_V1) \
		$(foreach source,$(METAMATH_PROOF_TRACE_PETTA_SOURCES_V1),--source $(source)) \
		--epilogue $(METAMATH_PROOF_TRACE_PETTA_QUERY_V1) \
		--program $(METAMATH_PROOF_TRACE_PETTA_PROGRAM_V1) \
		--receipt $(METAMATH_PROOF_TRACE_PETTA_RECEIPT_V1) >/dev/null; \
	output=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 60 \
		sh run.sh --silent \
		"$(abspath $(METAMATH_PROOF_TRACE_PETTA_PROGRAM_V1))" 2>&1); \
	printf '%s\n' "$$output" | rg -F -q \
		'((MetamathProofTracePeTTaNormalV1 AcceptedV1))'; \
	printf '%s\n' "$$output" | rg -F -q \
		'((MetamathProofTracePeTTaCompressedV1 AcceptedV1))'; \
	printf '%s\n' "$$output" | rg -F -q \
		'((MetamathProofTracePeTTaNormalEvidenceV1 '; \
	printf '%s\n' "$$output" | rg -F -q \
		'((MetamathProofTracePeTTaCompressedEvidenceV1 '; \
	test "$$(printf '%s\n' "$$output" | rg -c '^[(][)]$$')" -eq 1; \
	severed=$$(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 60 \
		sh run.sh --silent \
		"$(abspath $(METAMATH_PROOF_TRACE_PETTA_SEVERED_PROGRAM_V1))" 2>&1); \
	test "$$(printf '%s\n' "$$severed" | rg -c '^[(][)]$$')" -eq 5; \
	if printf '%s\n' "$$severed" | \
		rg -q 'MetamathProofTracePeTTa(Normal|Compressed)(Evidence)?V1'; then \
		exit 1; \
	fi; \
	printf '%s\n' '(MetamathProofTracePeTTaV1Summary 173 4 4 1 1 0)'

test-metamath-cogslt-petta-horn-oracle-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-metamath-cogslt-petta-horn-oracle-core-v1

test-metamath-cogslt-set-mm-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		METAMATH_SET_MM_V1="$(METAMATH_SET_MM_V1)" \
		test-metamath-cogslt-set-mm-core-v1

test-metamath-cogslt-set-mm-core-v1: test-metamath-cogslt-langdef-v1
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'the Metamath set.mm gate requires the C-only core build'; \
		exit 1; \
	fi
	@if ldd $(BIN) $(LANGDEF_COMPILER_V1_BIN) | rg -qi 'python|libswipl'; \
		then exit 1; fi
	@set -euo pipefail; \
	corpus='$(METAMATH_SET_MM_V1)'; \
	if [[ -z "$$corpus" ]]; then \
		echo 'set METAMATH_SET_MM_V1 to the canonical set.mm corpus'; \
		exit 1; \
	fi; \
	if [[ ! -f "$$corpus" ]]; then \
		echo "Metamath corpus is not a regular file: $$corpus"; \
		exit 1; \
	fi; \
	actual_digest=$$(sha256sum "$$corpus" | cut -d' ' -f1); \
	if [[ "$$actual_digest" != "$(METAMATH_SET_MM_SHA256_V1)" ]]; then \
		echo "Metamath corpus digest mismatch: $$actual_digest"; \
		exit 1; \
	fi; \
	import_output=$$($(CETTA_BIN_INVOKE) --lang prime \
		-e '!(import! &self langdef)' \
		-e '!(bind! &metamath-langdef (langdef:load "$(METAMATH_LANGDEF_MANIFEST_V1)"))' \
		-e "!(println! (langdef:import-file &metamath-langdef \"$$corpus\"))"); \
	if printf '%s\n' "$$import_output" | rg -q '\(Error '; then \
		printf '%s\n' "$$import_output" >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$import_output" | rg -q \
		'^\(LangDef:Imported MetamathV1 "[0-9a-f]{64}" $(METAMATH_SET_MM_IMPORT_COUNT_V1)\)$$'; \
	cli_output=$$($(CETTA_BIN_INVOKE) --lang metamath "$$corpus"); \
	printf '%s\n' "$$cli_output" | rg -q \
		'^\(LangDef:RunAccepted MetamathV1 "[0-9a-f]{64}" $(METAMATH_SET_MM_IMPORT_COUNT_V1)\)$$'; \
	printf '%s\n' \
		'(MetamathCoGSLTCliSetMmV1Summary 1 1 0 "$(METAMATH_SET_MM_SHA256_V1)" $(METAMATH_SET_MM_IMPORT_COUNT_V1))'; \
	printf '%s\n' \
		'(MetamathCoGSLTSetMmV1Summary 1 1 0 "$(METAMATH_SET_MM_SHA256_V1)" $(METAMATH_SET_MM_IMPORT_COUNT_V1))'

test-metamath-cogslt-corpus-differential-v1:
	@$(MAKE) --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
		BIN=$(METAMATH_COGSLT_CORE_BIN_V1) \
		GSLT2PARSE_COMPILER_ROOT="$(GSLT2PARSE_COMPILER_ROOT)" \
		METAMATH_TEST_ROOT_V1="$(METAMATH_TEST_ROOT_V1)" \
		METAMATH_MMLEAN4_ROOT_V1="$(METAMATH_MMLEAN4_ROOT_V1)" \
		METAMATH_MMLEAN4_BIN_V1="$(METAMATH_MMLEAN4_BIN_V1)" \
		test-metamath-cogslt-corpus-differential-core-v1

test-metamath-cogslt-corpus-differential-core-v1: \
		test-metamath-cogslt-langdef-v1 \
		$(LANGDEF_IMPORT_VERDICT_DRIVER_V1)
	@if [[ "$(ENABLE_PYTHON)" != 0 || "$(LIB_PROLOG_ENABLED)" != 0 ]]; then \
		echo 'the Metamath corpus differential requires the C-only core build'; \
		exit 1; \
	fi
	@if ldd $(BIN) $(LANGDEF_COMPILER_V1_BIN) | rg -qi 'python|libswipl'; \
		then exit 1; fi
	@if [[ -z "$(strip $(METAMATH_TEST_ROOT_V1))" ]]; then \
		echo 'set METAMATH_TEST_ROOT_V1 to the conformance corpus checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(METAMATH_MMLEAN4_ROOT_V1))" || \
		-z "$(strip $(METAMATH_MMLEAN4_BIN_V1))" ]]; then \
		echo 'set METAMATH_MMLEAN4_ROOT_V1 and METAMATH_MMLEAN4_BIN_V1'; \
		exit 1; \
	fi
	@if [[ "$$(git -C "$(METAMATH_TEST_ROOT_V1)" rev-parse HEAD)" != \
		"$(METAMATH_TEST_COMMIT_V1)" ]]; then \
		echo 'Metamath conformance corpus revision does not match the pinned suite'; \
		exit 1; \
	fi
	@if ! git -C "$(METAMATH_TEST_ROOT_V1)" diff --quiet || \
		! git -C "$(METAMATH_TEST_ROOT_V1)" diff --cached --quiet; then \
		echo 'Metamath conformance corpus tracked source is dirty'; \
		exit 1; \
	fi
	@if [[ "$$(git -C "$(METAMATH_MMLEAN4_ROOT_V1)" rev-parse HEAD)" != \
		"$(METAMATH_MMLEAN4_COMMIT_V1)" ]]; then \
		echo 'mm-lean4 source revision does not match the pinned oracle'; \
		exit 1; \
	fi
	@if ! git -C "$(METAMATH_MMLEAN4_ROOT_V1)" diff --quiet || \
		! git -C "$(METAMATH_MMLEAN4_ROOT_V1)" diff --cached --quiet; then \
		echo 'mm-lean4 tracked source is dirty'; \
		exit 1; \
	fi
	@sh -n $(LANGDEF_IMPORT_VERDICT_DRIVER_V1)
	@shellcheck $(LANGDEF_IMPORT_VERDICT_DRIVER_V1)
	@set -uo pipefail; \
	suite="$(METAMATH_TEST_ROOT_V1)/run-testsuite-all"; \
	tests_root="$(METAMATH_TEST_ROOT_V1)/tests"; \
	runtime_root="$(CURDIR)"; \
	runtime_bin="$$(realpath $(BIN))"; \
	manifest="$$(realpath $(METAMATH_LANGDEF_MANIFEST_V1))"; \
	driver="$$(realpath $(LANGDEF_IMPORT_VERDICT_DRIVER_V1))"; \
	mmlean="$$(realpath $(METAMATH_MMLEAN4_BIN_V1))"; \
	if [[ ! -f "$$suite" || ! -d "$$tests_root" || \
		! -x "$$runtime_bin" || ! -f "$$manifest" || \
		! -x "$$driver" || ! -x "$$mmlean" ]]; then \
		echo 'corpus differential resource is missing or invalid'; \
		exit 1; \
	fi; \
	total=0; ours_checked=0; oracle_checked=0; agreement=0; \
	failures=0; harness_failures=0; \
	while IFS=$$'\t' read -r suite_expected relative; do \
		total=$$((total + 1)); \
		file="$$tests_root/$$relative"; \
		if [[ ! -f "$$file" ]]; then \
			printf 'missing corpus database: %s\n' "$$relative" >&2; \
			failures=$$((failures + 1)); \
			continue; \
		fi; \
		if CETTA_LANGDEF_ROOT="$$runtime_root" \
			CETTA_LANGDEF_BIN="$$runtime_bin" \
			CETTA_LANGDEF_MANIFEST="$$manifest" \
			"$$driver" "$$file" >/dev/null 2>&1; then \
			ours=pass; \
		else \
			status=$$?; \
			case "$$status" in \
				1) ours=fail ;; \
				*) ours=harness ;; \
			esac; \
		fi; \
		directory=$$(dirname "$$file"); \
		basename=$$(basename "$$file"); \
		if (cd "$$directory" && \
			timeout 300 "$$mmlean" "$$basename" >/dev/null 2>&1); then \
			oracle=pass; \
		else \
			status=$$?; \
			case "$$status" in \
				1) oracle=fail ;; \
				*) oracle=harness ;; \
			esac; \
		fi; \
		if [[ "$$ours" == harness || "$$oracle" == harness ]]; then \
			printf 'harness failure: %s (generated=%s oracle=%s)\n' \
				"$$relative" "$$ours" "$$oracle" >&2; \
			harness_failures=$$((harness_failures + 1)); \
			continue; \
		fi; \
		if [[ "$$ours" == "$$suite_expected" ]]; then \
			ours_checked=$$((ours_checked + 1)); \
		else \
			printf 'spec mismatch: %s (expected=%s generated=%s)\n' \
				"$$relative" "$$suite_expected" "$$ours" >&2; \
			failures=$$((failures + 1)); \
		fi; \
		if [[ "$$oracle" == "$$suite_expected" ]]; then \
			oracle_checked=$$((oracle_checked + 1)); \
		else \
			printf 'oracle/corpus mismatch: %s (expected=%s oracle=%s)\n' \
				"$$relative" "$$suite_expected" "$$oracle" >&2; \
			failures=$$((failures + 1)); \
		fi; \
		if [[ "$$ours" == "$$oracle" ]]; then \
			agreement=$$((agreement + 1)); \
		else \
			printf 'unexplained differential: %s (generated=%s oracle=%s)\n' \
				"$$relative" "$$ours" "$$oracle" >&2; \
			failures=$$((failures + 1)); \
		fi; \
	done < <(awk '$$1 == "pass" || $$1 == "fail" { \
		gsub(/\\/, "", $$2); print $$1 "\t" $$2 \
	}' "$$suite"); \
	if [[ "$$total" -ne $(METAMATH_TEST_CASE_COUNT_V1) || \
		"$$ours_checked" -ne $(METAMATH_TEST_CASE_COUNT_V1) || \
		"$$oracle_checked" -ne $(METAMATH_TEST_CASE_COUNT_V1) || \
		"$$agreement" -ne $(METAMATH_TEST_CASE_COUNT_V1) || \
		"$$failures" -ne 0 || \
		"$$harness_failures" -ne 0 ]]; then \
		exit 1; \
	fi; \
	printf '%s\n' \
		'(MetamathCoGSLTCorpusDifferentialV1Summary $(METAMATH_TEST_CASE_COUNT_V1) $(METAMATH_TEST_CASE_COUNT_V1) $(METAMATH_TEST_CASE_COUNT_V1) $(METAMATH_TEST_CASE_COUNT_V1) 0 0 "$(METAMATH_TEST_COMMIT_V1)" "$(METAMATH_MMLEAN4_COMMIT_V1)")'

test-metamath-cogslt-generated-relational-corpus-oracle-v1: \
		test-metamath-cogslt-langdef-v1 \
		$(LANGDEF_PROOF_VERDICT_DRIVER_V1)
	@if [[ -z "$(strip $(METAMATH_TEST_ROOT_V1))" ]]; then \
		echo 'set METAMATH_TEST_ROOT_V1 to the conformance corpus checkout'; \
		exit 1; \
	fi
	@sh -n $(LANGDEF_PROOF_VERDICT_DRIVER_V1)
	@shellcheck $(LANGDEF_PROOF_VERDICT_DRIVER_V1)
	@set -uo pipefail; \
	suite="$(METAMATH_TEST_ROOT_V1)/run-testsuite-all"; \
	tests_root="$(METAMATH_TEST_ROOT_V1)/tests"; \
	registry_root="$$(realpath langdef)"; \
	runtime_bin="$$(realpath $(BIN))"; \
	manifest="$$(realpath $(METAMATH_LANGDEF_MANIFEST_V1))"; \
	driver="$$(realpath $(LANGDEF_PROOF_VERDICT_DRIVER_V1))"; \
	if [[ ! -f "$$suite" || ! -d "$$tests_root" || \
		! -d "$$registry_root" || ! -x "$$runtime_bin" || \
		! -f "$$manifest" || ! -x "$$driver" ]]; then \
		echo 'generated relational corpus resource is missing or invalid'; \
		exit 1; \
	fi; \
	total=0; checked=0; failures=0; harness_failures=0; \
	while IFS=$$'\t' read -r suite_expected relative; do \
		total=$$((total + 1)); \
		file="$$tests_root/$$relative"; \
		if [[ ! -f "$$file" ]]; then \
			printf 'missing corpus database: %s\n' "$$relative" >&2; \
			failures=$$((failures + 1)); \
			continue; \
		fi; \
		if timeout 300 env \
			CETTA_LANGDEF_ROOT="$$registry_root" \
			CETTA_LANGDEF_BIN="$$runtime_bin" \
			CETTA_LANGDEF_MANIFEST="$$manifest" \
			CETTA_LANGDEF_PROOF_BACKEND=generated-relational-audit-v1 \
			"$$driver" "$$file" >/dev/null 2>&1; then \
			ours=pass; \
		else \
			status=$$?; \
			case "$$status" in \
				1) ours=fail ;; \
				*) ours=harness ;; \
			esac; \
		fi; \
		if [[ "$$ours" == harness ]]; then \
			printf 'harness failure: %s\n' "$$relative" >&2; \
			harness_failures=$$((harness_failures + 1)); \
			continue; \
		fi; \
		if [[ "$$ours" == "$$suite_expected" ]]; then \
			checked=$$((checked + 1)); \
		else \
			printf 'spec mismatch: %s (expected=%s generated=%s)\n' \
				"$$relative" "$$suite_expected" "$$ours" >&2; \
			failures=$$((failures + 1)); \
		fi; \
	done < <(awk '$$1 == "pass" || $$1 == "fail" { \
		gsub(/\\/, "", $$2); print $$1 "\t" $$2 \
	}' "$$suite"); \
	if [[ "$$total" -ne $(METAMATH_TEST_CASE_COUNT_V1) || \
		"$$checked" -ne $(METAMATH_TEST_CASE_COUNT_V1) || \
		"$$failures" -ne 0 || "$$harness_failures" -ne 0 ]]; then \
		exit 1; \
	fi; \
	printf '%s\n' \
		'(MetamathCoGSLTGeneratedRelationalCorpusV1Summary $(METAMATH_TEST_CASE_COUNT_V1) $(METAMATH_TEST_CASE_COUNT_V1) 0 0)'

test-compact-resolution-include-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-compact-resolution-include-v1-body

test-compact-resolution-clausification-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-compact-resolution-clausification-v1-body

test-tptp-compact-policy-peak-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-tptp-compact-policy-peak-v1-body

qualify-tptp-external-prover-fixtures-v1: \
		$(TPTP_EXTERNAL_PROVER_FIXTURE_CHECK_V1) \
		$(TPTP_EXTERNAL_GROUND_REFUTATION_V1) \
		$(TPTP_E_GROUND_REFUTATION_FIXTURE_V1) \
		$(TPTP_VAMPIRE_GROUND_REFUTATION_FIXTURE_V1)
	@if [[ -z "$(strip $(TPTP_EPROVER_V1))" || \
		-z "$(strip $(TPTP_VAMPIRE_V1))" ]]; then \
		echo 'set TPTP_EPROVER_V1 and TPTP_VAMPIRE_V1 to executable prover paths' >&2; \
		exit 2; \
	fi
	@sh -n $(TPTP_EXTERNAL_PROVER_FIXTURE_CHECK_V1)
	@shellcheck $(TPTP_EXTERNAL_PROVER_FIXTURE_CHECK_V1)
	@$(TPTP_EXTERNAL_PROVER_FIXTURE_CHECK_V1) \
		"$(TPTP_EPROVER_V1)" "$(TPTP_VAMPIRE_V1)"

test-tptp-canonical-round-trip-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-canonical-round-trip-v1-body

test-tptp-compact-policies-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-compact-policies-v1-body

test-tptp-compact-print-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-compact-print-v1-body

test-tptp-compact-stale-artifact-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-compact-stale-artifact-v1-body

test-tptp-compact-import-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-compact-import-v1-body

test-tptp-compact-binding-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-compact-binding-v1-body

check-tptp-compact-morphism-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		check-tptp-compact-morphism-v1-body

benchmark-tptp-frozen-size-ladder-v1-body: \
		$(TPTP_FROZEN_LADDER_V1_BIN) \
		prepare-tptp-compact-morphism-v1-body \
		$(TPTP_FROZEN_SIZE_LADDER_RUNNER_V1)
	@if [[ -z "$(strip $(TPTP_OFFICIAL_CORPUS_ROOT_V1))" || \
		! -d "$(TPTP_OFFICIAL_CORPUS_ROOT_V1)/Problems" ]]; then \
		echo 'set TPTP_OFFICIAL_CORPUS_ROOT_V1 to the pinned distribution' >&2; \
		exit 2; \
	fi
	@bash "$(TPTP_FROZEN_SIZE_LADDER_RUNNER_V1)" \
		"$(TPTP_FROZEN_LADDER_V1_BIN)" \
		"$(TPTP_OFFICIAL_CORPUS_COMPATIBILITY_SNAPSHOT_V1)" \
		"$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		"$(TPTP_FROZEN_SIZE_LADDER_OUTPUT_V1)"
	@echo "TPTP frozen size ladder: $(TPTP_FROZEN_SIZE_LADDER_OUTPUT_V1)"

benchmark-tptp-frozen-size-ladder-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		TPTP_FROZEN_SIZE_LADDER_OUTPUT_V1="$(TPTP_FROZEN_SIZE_LADDER_OUTPUT_V1)" \
		benchmark-tptp-frozen-size-ladder-v1-body

qualify-tptp-frozen-full-corpus-v1-body: \
		$(TPTP_FROZEN_CORPUS_QUALIFIER_V1_BIN) \
		$(TPTP_FROZEN_CORPUS_SNAPSHOT_GATE_V1) \
		prepare-tptp-compact-morphism-v1-body \
		$(TPTP_OFFICIAL_TSTP_FIXTURES_V1)
	@if [[ -z "$(strip $(TPTP_OFFICIAL_CORPUS_ROOT_V1))" || \
		! -d "$(TPTP_OFFICIAL_CORPUS_ROOT_V1)/Problems" || \
		! -d "$(TPTP_OFFICIAL_CORPUS_ROOT_V1)/Axioms" ]]; then \
		echo 'set TPTP_OFFICIAL_CORPUS_ROOT_V1 to the pinned distribution' >&2; \
		exit 2; \
	fi
	@set -euo pipefail; \
	mkdir -p "$(dir $(TPTP_FROZEN_CORPUS_QUALIFICATION_OUTPUT_V1))" \
		"$(dir $(TPTP_FROZEN_CORPUS_QUALIFICATION_SUMMARY_V1))"; \
	tmp_tsv=$$(mktemp "$(dir $(TPTP_FROZEN_CORPUS_QUALIFICATION_OUTPUT_V1))/tptp-corpus-v1.XXXXXX"); \
	tmp_summary=$$(mktemp "$(dir $(TPTP_FROZEN_CORPUS_QUALIFICATION_SUMMARY_V1))/tptp-corpus-summary-v1.XXXXXX"); \
	trap 'mv -f "$$tmp_tsv" "$(TPTP_FROZEN_CORPUS_QUALIFICATION_OUTPUT_V1).failed"; mv -f "$$tmp_summary" "$(TPTP_FROZEN_CORPUS_QUALIFICATION_SUMMARY_V1).failed"' EXIT INT TERM; \
	$(TPTP_FROZEN_CORPUS_QUALIFIER_V1_BIN) \
		"$(TPTP_FROZEN_CORPUS_SNAPSHOT_V1)" \
		"$(TPTP_OFFICIAL_CORPUS_ROOT_V1)/Problems" \
		"$(TPTP_OFFICIAL_CORPUS_ROOT_V1)/Axioms" \
		$(TPTP_OFFICIAL_TSTP_FIXTURES_V1) \
		> "$$tmp_tsv" 2> >(tee "$$tmp_summary" >&2); \
	rows=$$(awk 'END { print NR - 1 }' "$$tmp_tsv"); \
	non_accept=$$(awk -F '\t' 'NR > 1 && $$2 != "accept" { n++ } END { print n + 0 }' "$$tmp_tsv"); \
	test "$$rows" -eq 28694; \
	test "$$non_accept" -eq "$(TPTP_FROZEN_CORPUS_EXPECTED_REJECTED_V1)"; \
	if [[ "$(TPTP_FROZEN_CORPUS_PROFILE_V1)" == strict ]]; then \
		itp_rejects=$$(awk -F '\t' 'NR > 1 && $$2 == "reject" && $$1 ~ /\/Problems\/ITP\// { n++ } END { print n + 0 }' "$$tmp_tsv"); \
		syn_rejects=$$(awk -F '\t' 'NR > 1 && $$2 == "reject" && $$1 ~ /\/Problems\/SYN\// { n++ } END { print n + 0 }' "$$tmp_tsv"); \
		other_rejects=$$(awk -F '\t' 'NR > 1 && $$2 == "reject" && $$1 !~ /\/Problems\/(ITP|SYN)\// { n++ } END { print n + 0 }' "$$tmp_tsv"); \
		bad_witnesses=$$(awk -F '\t' 'NR > 1 && $$2 == "reject" && $$5 !~ /^TPTP:NoParse byte=[0-9]+ rule=TPTP_file$$/ { n++ } END { print n + 0 }' "$$tmp_tsv"); \
		test "$$itp_rejects" -eq 148; \
		test "$$syn_rejects" -eq 5; \
		test "$$other_rejects" -eq 0; \
		test "$$bad_witnesses" -eq 0; \
		for name in 'SYN000^7.p' 'SYN000_2.p' 'SYN000-3.p' 'SYN000-2.p' 'SYN000+2.p'; do \
			suffix="/Problems/SYN/$$name"; \
			count=$$(awk -F '\t' -v suffix="$$suffix" 'NR > 1 && $$2 == "reject" && substr($$1, length($$1) - length(suffix) + 1) == suffix { n++ } END { print n + 0 }' "$$tmp_tsv"); \
			test "$$count" -eq 1; \
		done; \
	fi; \
	expected='(TptpCorpusQualificationV1 accepted=$(TPTP_FROZEN_CORPUS_EXPECTED_ACCEPTED_V1) rejected=$(TPTP_FROZEN_CORPUS_EXPECTED_REJECTED_V1) unprojected=0 skipped=0 cnf=8478 fof=10604 tff=3936 thf=5671 tcf=1 tpi=0 ax=2426 tstp=4 other=4)'; \
	grep -Fqx "$$expected" "$$tmp_summary"; \
	accepted='$(TPTP_FROZEN_CORPUS_EXPECTED_ACCEPTED_V1)'; \
	grep -Eq "^\(TptpCorpusStagesV1 compacted=$$accepted printed=$$accepted round-trip=$$accepted compact-failed=0 print-failed=0 reread-failed=0 round-trip-differs=0 " "$$tmp_summary"; \
	unstaged=$$(awk -F '\t' 'NR > 1 && $$2 == "accept" && $$7 != "ok" { n++ } END { print n + 0 }' "$$tmp_tsv"); \
	test "$$unstaged" -eq 0; \
	mv "$$tmp_tsv" "$(TPTP_FROZEN_CORPUS_QUALIFICATION_OUTPUT_V1)"; \
	mv "$$tmp_summary" "$(TPTP_FROZEN_CORPUS_QUALIFICATION_SUMMARY_V1)"; \
	trap - EXIT INT TERM; \
	echo "TPTP frozen corpus qualification ($(TPTP_FROZEN_CORPUS_PROFILE_V1)): $(TPTP_FROZEN_CORPUS_EXPECTED_ACCEPTED_V1) accepted, $(TPTP_FROZEN_CORPUS_EXPECTED_REJECTED_V1) rejected"

qualify-tptp-frozen-full-corpus-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		qualify-tptp-frozen-full-corpus-v1-body

qualify-tptp-frozen-corpus-compatible-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		JSON_BACKEND=gslt CETTA_PROVENANCE_ASSERT=0 \
		RHOCOST_COMMIT_AUDIT=0 ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		TPTP_FROZEN_CORPUS_PROFILE_V1=corpus-compatible \
		TPTP_FROZEN_CORPUS_SNAPSHOT_V1="$(TPTP_OFFICIAL_CORPUS_COMPATIBILITY_SNAPSHOT_V1)" \
		TPTP_FROZEN_CORPUS_EXPECTED_ACCEPTED_V1=28694 \
		TPTP_FROZEN_CORPUS_EXPECTED_REJECTED_V1=0 \
		TPTP_FROZEN_CORPUS_QUALIFICATION_OUTPUT_V1="$(TPTP_FROZEN_CORPUS_COMPATIBILITY_QUALIFICATION_OUTPUT_V1)" \
		TPTP_FROZEN_CORPUS_QUALIFICATION_SUMMARY_V1="$(TPTP_FROZEN_CORPUS_COMPATIBILITY_QUALIFICATION_SUMMARY_V1)" \
		qualify-tptp-frozen-full-corpus-v1-body

test-tptp-reader-artifact-reproducibility-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		test-tptp-reader-artifact-reproducibility-v1-body

test-gslt2parse-parser-pack-lexical-v1-matrix: \
		test-gslt2parse-parser-pack-lexical-v1-native \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(REGULAR_SPAN_DFA_V1_STREAM_BIN) \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'GSLT2PARSE_PETTA_ROOT is required for the lexical projection matrix'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--projection-binary $(REGULAR_SPAN_DFA_V1_STREAM_BIN) \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-gll-v1-matrix: \
		test-gslt2parse-parser-pack-gll-v1-native \
		$(PARSER_PACK_GLL_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_gll_v1.py \
		--binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into the generic ParserPack GLL path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-glr-v1-matrix: \
		test-gslt2parse-parser-pack-gll-v1-native \
		test-gslt2parse-parser-pack-glr-v1-native \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_gll_v1.py \
		--binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"
	@if rg -ni 'metamath|megalodon|tptp' \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
		experiments/gslt2parse_foundation/native/parser_pack_gll_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_glr_v1_stream.c \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.c \
		experiments/gslt2parse_foundation/native/parser_pack_abi_stream_v1.h \
		src/lib_parse_native_grammar.c \
		src/lib_parse_native_grammar.h; then \
		echo 'guest-language name leaked into a generic ParserPack parser path'; \
		exit 1; \
	fi

test-gslt2parse-parser-pack-wide-scale-v1: \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/probe_parser_pack_wide_scale_v1.py \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-regular-span-dfa-v1-matrix: \
		test-gslt2parse-regular-span-dfa-v1-native \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_regular_span_dfa_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--dfa-binary $(REGULAR_SPAN_DFA_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-c-production-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@$(MAKE) --no-print-directory ENABLE_PIC=1 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		GSLT2PARSE_HE_ROOT="$(GSLT2PARSE_HE_ROOT)" \
		test-gslt2parse-c-production-v1-body

probe-prime-hopper-table1-source:
	@test -n "$(HOPPER_TABLE1_ROOT)" || { \
		echo 'FAIL: set HOPPER_TABLE1_ROOT to the pinned Hopper checkout'; \
		exit 1; \
	}
	@python3 scripts/check_prime_hopper_table1_manifest.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)"
	@python3 scripts/generate_prime_hopper_first_order.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)" --check
	@python3 scripts/generate_prime_hopper_higher_order.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)" --check
	@python3 scripts/generate_prime_hopper_additional_iterative.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)" --check
	@python3 scripts/generate_prime_hopper_structural_list.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)" --check
	@python3 scripts/generate_prime_hopper_relational_recursion.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)" --check
	@python3 scripts/generate_prime_hopper_tree_relations.py \
		--snapshot-root "$(HOPPER_TABLE1_ROOT)" --check

qualify-match-decision-policy-lean-native-v1-body: \
		$(MATCH_DECISION_POLICY_GENERATED_V1) $(MATCH_DECISION_POLICY_RESULT_V1) \
		tests/langdef/bnf/match_decision_policy_source_qualification_v1.lean \
		tests/support/qualify_match_decision_policy_lean_native_v1.sh \
		test-match-decision-policy-no-python-build-dependency-v1
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo "METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project" >&2; \
		exit 2; \
	}; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(abspath $(BOOTSTRAP_TMPDIR))/match-policy-lean.XXXXXX"); \
	sh tests/support/qualify_match_decision_policy_lean_native_v1.sh \
		"$(CURDIR)/$(LANGDEF_COMPILER_V1_BIN)" \
		"$(CURDIR)/$(GSLT2PARSE_CHART_V1_NATIVE_BIN)" \
		"$$lean_root" "$$evidence"

qualify-prepared-scalar-lean:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_CHECKOUT)"; \
	test -f "$$lean_root/lakefile.lean" || \
		{ echo 'Set METTAPEDIA_LEAN_ROOT to the MeTTapedia Lean project' >&2; exit 2; }; \
	cd "$$lean_root"; \
	lake build Mettapedia.Machines.RegisterCode \
		Mettapedia.GSLT.Dynamics.PartialRegionPlan; \
	lake env lean -DwarningAsError=true Mettapedia/Machines/ScalarTreeRegion.lean

probe-petta-corpus-manifest: test-petta-corpus-manifest-unit
	@if [[ -z "$(strip $(PETTA_ORACLE_ROOT))" ]]; then \
		echo 'set PETTA_ORACLE_ROOT to the pinned PeTTa oracle checkout'; \
		exit 1; \
	fi
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py verify \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)"

test-petta-corpus-manifest: test-petta-corpus-manifest-unit
	@if [[ -z "$(strip $(PETTA_ORACLE_ROOT))" ]]; then \
		echo 'set PETTA_ORACLE_ROOT to the pinned PeTTa oracle checkout'; \
		exit 1; \
	fi
	@PYTHONDONTWRITEBYTECODE=1 \
		python3 scripts/petta_corpus_manifest.py verify \
		--petta-dir "$(PETTA_ORACLE_ROOT)" \
		--manifest "$(PETTA_CORPUS_MANIFEST)" \
		--require-complete

test-prime-nik-megalodon-term-v1: \
		$(BIN) \
		$(NIK_RUNTIME_TEST_BIN) \
		$(PRIME_NIK_MEGALODON_TERM_TEST_V1) \
		$(PRIME_NIK_MEGALODON_TERM_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_TERM_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_TERM_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable term differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_TERM_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-term.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_TERM_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_TERM_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--differential "$(abspath $(NIK_RUNTIME_TEST_BIN))" \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--positive $(PRIME_NIK_MEGALODON_TERM_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

test-prime-nik-megalodon-polymorphic-v1: \
		$(BIN) \
		$(NIK_RUNTIME_TEST_BIN) \
		$(PRIME_NIK_MEGALODON_POLY_TEST_V1) \
		$(PRIME_NIK_MEGALODON_POLY_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_POLY_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_POLY_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable polymorphic differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_POLY_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-poly.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_POLY_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_POLY_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--differential "$(abspath $(NIK_RUNTIME_TEST_BIN))" \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--positive $(PRIME_NIK_MEGALODON_POLY_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

test-prime-nik-megalodon-known-implication-v1: \
		$(BIN) \
		$(NIK_RUNTIME_TEST_BIN) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_TEST_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable known-implication differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-known-imp.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_KNOWN_IMP_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--differential "$(abspath $(NIK_RUNTIME_TEST_BIN))" \
		--positive $(PRIME_NIK_MEGALODON_KNOWN_IMP_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

test-prime-nik-megalodon-definition-v1: \
		$(BIN) \
		$(NIK_RUNTIME_TEST_BIN) \
		$(PRIME_NIK_MEGALODON_DEFINITION_TEST_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_POSITIVE_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_CATALOG_V1)
	@set -eu; \
	if [ ! -x "$(MEGALODON_AUTO_BIN)" ]; then \
		echo "SKIP: Megalodon executable definition differential (binary unavailable)"; \
		exit 0; \
	fi; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	witness_abs=$$(realpath "$(PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1)"); \
	if [ -n "$$lean_root" ]; then \
		mkdir -p $(BOOTSTRAP_TMPDIR); \
		tmp_witness=$$(mktemp "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-definition.XXXXXX"); \
		tmp_witness_abs=$$(realpath "$$tmp_witness"); \
		trap 'rm -f "$$tmp_witness_abs"' EXIT INT TERM; \
		(cd "$$lean_root" && lake env lean --run \
			"$(abspath $(PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1))" \
			"$$tmp_witness_abs"); \
		cmp -s "$$tmp_witness_abs" "$$witness_abs"; \
		witness_abs="$$tmp_witness_abs"; \
	fi; \
	python3 $(PRIME_NIK_MEGALODON_DEFINITION_TEST_V1) \
		--megalodon "$(MEGALODON_AUTO_BIN)" \
		--cetta "$(abspath $(BIN))" \
		--differential "$(abspath $(NIK_RUNTIME_TEST_BIN))" \
		--catalog $(PRIME_NIK_AUTHORITY_CATALOG_V1) \
		--positive $(PRIME_NIK_MEGALODON_DEFINITION_POSITIVE_V1) \
		--lean-witness "$$witness_abs"

test-prime-nik-qualification-v1: \
		test-prime-nik-megalodon-v1 test-prime-nik-megalodon-term-v1 \
		test-prime-nik-megalodon-polymorphic-v1 \
		test-prime-nik-megalodon-known-implication-v1 \
		test-prime-nik-megalodon-definition-v1 \
		test-prime-nik-megalodon-tactics-package-v1
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	if [ -n "$$lean_root" ]; then \
		$(MAKE) -s METTAPEDIA_LEAN_ROOT="$$lean_root" \
			test-prime-nik-lean-export-v1; \
	else \
		echo "SKIP: Prime NIK Lean export (Mettapedia Lean project unavailable)"; \
	fi

test-gslt2parse-c-horn-v1-differential: test-gslt2parse-c-horn-v1-native
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_action_bytecode_compiler_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--presentation-root \
		"$(CURDIR)/experiments/gslt2parse_foundation/presentations"
	@swipl -q -f \
		"$(GSLT2PARSE_PETTA_ROOT)/experiments/gslt2parse_foundation/test_finite_horn_ground_terms.pl" \
		-- "$(CURDIR)/experiments/gslt2parse_foundation/presentations"

test-gslt2parse-parser-action-bytecode-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_ACTION_BYTECODE_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_action_bytecode_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--stream-binary $(PARSER_ACTION_BYTECODE_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guard-compiler-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_compiler_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guard-regular-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_regular_v1.py \
		--binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-lexical-plan-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_lexical_plan_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_LEXICAL_PLAN_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-guarded-lexical-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-mm0-cogslt-term-environment-swi-compat-v1: \
		test-mm0-cogslt-term-environment-v1 \
		$(MM0_TERM_ENVIRONMENT_SWI_QUERY_V1)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" || \
		! -x "$(GSLT2PARSE_PETTA_ROOT)/run.sh" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned SWI PeTTa checkout'; \
		exit 1; \
	fi
	@set -euo pipefail; \
	work=$$(mktemp -d runtime/mm0-term-swi-compat.XXXXXX); \
	trap 'rm -rf "$$work"' EXIT INT TERM; \
	(cd "$(GSLT2PARSE_PETTA_ROOT)" && timeout 60 \
		sh run.sh --silent \
		"$(abspath $(MM0_TERM_ENVIRONMENT_SWI_QUERY_V1))") \
		>"$$work/stdout" 2>"$$work/stderr"; \
	for expected in \
		MM0TermEnvironmentSWIAcceptedV1 \
		MM0TermEnvironmentSWIPureTargetRefusedV1 \
		MM0TermEnvironmentSWIDependentSignatureAcceptedV1; do \
		rg -F -x -q "$$expected" "$$work/stdout"; \
	done; \
	! rg -q 'Mismatch|partial|Error' "$$work/stdout"; \
	printf '%s\n' \
		'(MM0TermEnvironmentSWICompatibilityV1Summary 3 0)'

test-gslt2parse-parser-pack-guard-plan-prime-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_plan_prime_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--profile prime

test-gslt2parse-parser-pack-guard-plan-he-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guard_plan_prime_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARD_PLAN_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--profile he

test-gslt2parse-he-parser-authority-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_parser_authority_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)"

test-gslt2parse-he-string-slr-specialization-v1: \
		$(PARSER_PACK_SLR_SUMMARY_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_string_slr_specialization_v1.py \
		--binary $(PARSER_PACK_SLR_SUMMARY_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-he-reader-escape-differential-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_escape_differential_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--cetta-root "$(CURDIR)" \
		--mettapedia-root "$${METTAPEDIA_ROOT:-../../Mettapedia}"

test-gslt2parse-he-reader-source-faithfulness-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_source_faithfulness_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-he-reader-source-correspondence-v1: \
		test-gslt2parse-he-unicode-residual-dfa-v1 \
		test-gslt2parse-he-string-slr-specialization-v1 \
		test-gslt2parse-he-reader-escape-differential-v1 \
		test-gslt2parse-he-reader-source-faithfulness-v1
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_source_correspondence_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--cetta-root "$(CURDIR)" \
		--mettapedia-root "$${METTAPEDIA_ROOT:-../../Mettapedia}"

test-gslt2parse-rho-abstract-syntax-v1:
	@mettapedia_root="$${METTAPEDIA_ROOT:-../../Mettapedia/lean/mettapedia}"; \
	python3 tools/test_rho_abstract_syntax_v1.py \
		--mettapedia-root "$$mettapedia_root"

test-gslt2parse-rhocalc-parser-pack-v1: \
		$(BIN) \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_rhocalc_parser_pack_v1.py \
		--cetta-binary "$(CETTA_SCRIPT_BIN)" \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-rho-syntax-convergence-v1: \
		$(BIN) \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN) \
		$(GSLT2PARSE_TERM_PROJECTION_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_rhocalc_syntax_convergence_v1.py \
		--cetta-binary "$(CETTA_SCRIPT_BIN)" \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--projection-binary $(GSLT2PARSE_TERM_PROJECTION_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-stable-parser-parse-only-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_stable_parser_parse_only_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-he-gslt-parse-only-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_gslt_parse_only_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN) \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

bench-gslt2parse-he-gslt-parse-only-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_gslt_parse_only_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(HE_DOCUMENT_PIPELINE_V1_BENCH_BIN) \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--benchmark

bench-gslt2parse-stable-parser-parse-only-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_stable_parser_parse_only_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--benchmark

test-gslt2parse-he-reader-guard-exec-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_guard_exec_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)"

test-gslt2parse-he-reader-guarded-lexical-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--scalar-exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		$(if $(strip $(GSLT2PARSE_HE_GENERATED_C_OUTPUT)),--generated-c-output "$(GSLT2PARSE_HE_GENERATED_C_OUTPUT)",)

refresh-he-compiled-reader-cursor-v1:
	@$(MAKE) --no-print-directory \
		GSLT2PARSE_HE_ROOT="$(GSLT2PARSE_HE_ROOT)" \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		GSLT2PARSE_HE_GENERATED_C_OUTPUT="$(GSLT2PARSE_HE_CURSOR_GENERATED_C)" \
		test-gslt2parse-he-reader-guarded-lexical-v1

test-gslt2parse-he-document-pipeline-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(HE_DOCUMENT_PIPELINE_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_document_pipeline_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--pipeline-binary $(HE_DOCUMENT_PIPELINE_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)"

bench-gslt2parse-prepared-final-forest-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		$(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--probe-final-forest-growth \
		--probe-iterations $(PREPARED_FINAL_FOREST_PROBE_ITERATIONS)
	@python3 tools/test_he_reader_guarded_lexical_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--scalar-exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--plan-binary $(PARSER_PACK_GUARDED_LEXICAL_PLAN_V1_STREAM_BIN) \
		--exec-binary $(PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)" \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--probe-final-forest-growth \
		--probe-iterations $(PREPARED_FINAL_FOREST_PROBE_ITERATIONS)

test-gslt2parse-petta-form-guard-exec-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_PACK_GUARD_REF_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_petta_form_guard_exec_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--exec-binary $(PARSER_PACK_GUARD_REF_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-petta-document-splitter-v1: \
		$(PARSER_PACK_GLL_V1_STREAM_BIN) \
		$(PARSER_PACK_GLR_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_petta_document_splitter_v1.py \
		--gll-binary $(PARSER_PACK_GLL_V1_STREAM_BIN) \
		--glr-binary $(PARSER_PACK_GLR_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-petta-ffi-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=1 ENABLE_SANITIZERS=0 ENABLE_PIC=0 \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_RECEIPT_PRIMARY_INDEX=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_NEED_CLOSURE_CAPTURE=0 ENABLE_PRIME_EVAL_STACK=0 \
		ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-ffi-v1-body

test-gslt2parse-petta-document-pipeline-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=1 ENABLE_SANITIZERS=0 ENABLE_PIC=0 \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_RECEIPT_PRIMARY_INDEX=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_NEED_CLOSURE_CAPTURE=0 ENABLE_PRIME_EVAL_STACK=0 \
		ENABLE_LIB_PROLOG=0 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-document-pipeline-v1-body

test-gslt2parse-petta-ffi-v1-body:
	@$(MAKE) --no-print-directory ENABLE_PIC=1 \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-parser-pack-native-petta-v1
	@$(MAKE) --no-print-directory \
		GSLT2PARSE_PETTA_ROOT="$(GSLT2PARSE_PETTA_ROOT)" \
		test-gslt2parse-petta-document-pipeline-v1-body

test-gslt2parse-petta-parser-authority-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the pinned PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_petta_parser_authority_v1.py \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-atom-projection-closure-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_BIN) \
		tools/test_parser_atom_projection_closure_v1.py \
		tools/sexpr_atom_projection_plan_v1.py \
		experiments/gslt2parse_foundation/presentations/compiler/semantic_text_span_compiler_v1.metta
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the paired PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_atom_projection_closure_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--closure-binary $(PARSER_ATOM_PROJECTION_CLOSURE_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-semantic-mask-span-compiler-v1: \
		$(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		$(SEMANTIC_MASK_NFA_V1_STREAM_BIN) \
		tools/test_semantic_mask_span_compiler_v1.py \
		experiments/gslt2parse_foundation/presentations/compiler/semantic_mask_span_compiler_v1.metta \
		experiments/gslt2parse_foundation/presentations/canaries/semantic_mask_v1.metta
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the paired PeTTa checkout'; \
		exit 1; \
	fi
	@python3 tools/test_semantic_mask_span_compiler_v1.py \
		--chart-binary $(GSLT2PARSE_CHART_V1_NATIVE_BIN) \
		--stream-binary $(SEMANTIC_MASK_NFA_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"

test-gslt2parse-parser-pack-abi-v1-matrix: \
		$(GSLT2PARSE_PARSER_PACK_ABI_V1_STREAM_BIN)
	@if [[ -z "$(strip $(GSLT2PARSE_PETTA_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_PETTA_ROOT to the PeTTa foundation checkout'; \
		exit 1; \
	fi
	@python3 tools/test_parser_pack_abi_v1.py \
		--binary $(GSLT2PARSE_PARSER_PACK_ABI_V1_STREAM_BIN) \
		--petta-root "$(GSLT2PARSE_PETTA_ROOT)"
	@if rg -ni 'metamath|megalodon|tptp' \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/parser_pack_abi_stream_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.c \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/finite_horn_ground_term_v1.h \
		$(GSLT2PARSE_SCHEMA_V1_NATIVE_DIR)/test_parser_pack_abi_v1_stream.c; then \
		echo 'guest-language name leaked into the native ParserPack ABI path'; \
		exit 1; \
	fi

require-mettapedia-root-for-wire-provenance-v1:
	@test -n "$(strip $(METTAPEDIA_ROOT))" || { \
		echo "METTAPEDIA_ROOT must name a Mettapedia checkout for wire provenance qualification" >&2; \
		echo "usage: make METTAPEDIA_ROOT=/path/to/Mettapedia qualify-language-def-wire-provenance-v1" >&2; \
		exit 2; \
	}
	@test -f "$(METTAPEDIA_ROOT)/lean/mettapedia/lakefile.lean" || { \
		echo "METTAPEDIA_ROOT does not contain lean/mettapedia/lakefile.lean: $(METTAPEDIA_ROOT)" >&2; \
		exit 2; \
	}

check-walters-zantema-da-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_walters_zantema_da_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-radix-digit-language-def-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_radix_digit_language_def_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-external-call-machine-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_external_call_machine_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-structured-c-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_structured_c_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-arithmetic-target-coproduct-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_arithmetic_target_coproduct_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-exact-arithmetic-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_exact_arithmetic_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-rfc8259-json-syntax-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_rfc8259_json_syntax_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-rfc8259-json-parser-profile-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_rfc8259_json_parser_profile_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-json-value-language-def-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_json_value_language_def_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-first-order-resolution-input-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_first_order_resolution_input_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-first-order-resolution-example-trace-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_first_order_resolution_example_trace_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-tptp-fof-cnf-syntax-tree-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_tptp_fof_cnf_syntax_tree_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-tptp-fof-skolemization-ntt-wires-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_tptp_fof_skolemization_ntt_wires_v1.sh "$(METTAPEDIA_ROOT)"

check-tptp-fof-transformation-language-defs-v1: require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_FOF_TRANSFORMATION_LANGUAGE_DEFS_V1) \
		$(TPTP_FOF_TRANSFORMATION_LANGUAGE_DEFS_LOCK_V1)
	@sh tools/check_tptp_fof_transformation_language_defs_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-include-directive-language-def-v1: \
		require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_OFFICIAL_INCLUDE_DIRECTIVE_LANGUAGE_DEF_V1) \
		$(TPTP_OFFICIAL_INCLUDE_DIRECTIVE_LANGUAGE_DEF_LOCK_V1)
	@sh tools/check_tptp_official_include_directive_language_def_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-include-resolution-carrier-v1: \
		require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_OFFICIAL_INCLUDE_RESOLUTION_CARRIER_V1) \
		$(TPTP_OFFICIAL_INCLUDE_RESOLUTION_CARRIER_LOCK_V1)
	@sh tools/check_tptp_official_include_resolution_carrier_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-include-resolution-result-carrier-v1: \
		require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_OFFICIAL_INCLUDE_RESOLUTION_RESULT_CARRIER_V1) \
		$(TPTP_OFFICIAL_INCLUDE_RESOLUTION_RESULT_CARRIER_LOCK_V1)
	@sh tools/check_tptp_official_include_resolution_result_carrier_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-include-selection-v1: \
		require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_OFFICIAL_INCLUDE_SELECTION_V1) \
		$(TPTP_OFFICIAL_INCLUDE_SELECTION_LOCK_V1)
	@sh tools/check_tptp_official_include_selection_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-include-environment-lookup-v1: \
		require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_OFFICIAL_INCLUDE_ENVIRONMENT_LOOKUP_V1) \
		$(TPTP_OFFICIAL_INCLUDE_ENVIRONMENT_LOOKUP_LOCK_V1)
	@sh tools/check_tptp_official_include_environment_lookup_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-include-input-classification-v1: \
		require-mettapedia-root-for-wire-provenance-v1 \
		$(TPTP_OFFICIAL_INCLUDE_INPUT_CLASSIFICATION_V1) \
		$(TPTP_OFFICIAL_INCLUDE_INPUT_CLASSIFICATION_LOCK_V1)
	@sh tools/check_tptp_official_include_input_classification_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-derivation-word-machine-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_derivation_word_machine_wire_v1.sh "$(METTAPEDIA_ROOT)"

check-tptp-ground-resolution-word-canary-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_tptp_ground_resolution_word_canary_v1.sh "$(METTAPEDIA_ROOT)"

check-tptp-official-ground-resolution-semantic-canary-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_tptp_official_ground_resolution_semantic_canary_v1.sh \
		"$(METTAPEDIA_ROOT)"

check-tptp-official-semantic-carrier-wire-v1: require-mettapedia-root-for-wire-provenance-v1
	@sh tools/check_tptp_official_semantic_carrier_wire_v1.sh "$(METTAPEDIA_ROOT)"

qualify-ebnf-authored-source-lean-v1:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo 'METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project' >&2; exit 2; \
	}; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/ebnf-source-parent.XXXXXX"); \
	bash tests/support/check_ebnf_authored_source_lean_v1.sh "$$lean_root" "$$evidence/check"

qualify-plain-bnf-selected-native-type-lean-v1:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo 'METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project' >&2; exit 2; \
	}; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/plain-bnf-selected-type.XXXXXX"); \
	bash tests/support/check_plain_bnf_selected_native_type_lean_v1.sh "$$lean_root" "$$evidence/check"

qualify-plain-bnf-authored-source-lean-v1:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo "METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project" >&2; \
		exit 2; \
	}; \
	client="$(abspath tests/langdef/bnf/plain_bnf_authored_source_qualification_v1.lean)"; \
	if grep -En '\b(sorry|axiom|native_decide|theorem_wanted)\b|_wanted|set_option[[:space:]]+(maxHeartbeats|maxRecDepth|maxSteps)' "$$client"; then \
		echo 'authored source qualification contains a proof placeholder or resource override' >&2; \
		exit 1; \
	fi; \
	(cd "$$lean_root" && lake build \
		Mettapedia.GSLT.Parsing.CanonicalSourceQualification \
		Mettapedia.GSLT.Parsing.CanonicalSourceOperationalGSLT \
		Mettapedia.GSLT.Parsing.HornCertificateBoundary \
		Mettapedia.GSLT.Parsing.HornIntegerProvider \
		Mettapedia.GSLT.Parsing.HornIntegerProviderNativeType \
		Mettapedia.GSLT.Parsing.HornProviderGSLT \
		Mettapedia.GSLT.Parsing.PlainBnfLexicalScalarSemantics \
		Mettapedia.GSLT.Parsing.PlainBnfLexicalInhabitation \
		Mettapedia.GSLT.Parsing.PlainBnfSourceScalarCodec \
		Mettapedia.GSLT.Parsing.PlainBnfExclusionLocalModel \
		Mettapedia.GSLT.Parsing.PlainBnfDeclarationOccurrences \
		Mettapedia.GSLT.Parsing.PlainBnfSourceTextAppend && \
		lake env lean -DwarningAsError=true "$$client")

qualify-plain-bnf-denotation-lean-native-v1-body: \
		$(BIN) $(PLAIN_BNF_SEMANTIC_ADMISSION_PETTA_PROGRAM_V1) \
		lib/lib_bnf.metta langdef/bnf/plain_bnf_denotation_v1.metta
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo "METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project" >&2; \
		exit 2; \
	}; \
	packet_root="$(abspath $(BOOTSTRAP_TMPDIR))"; \
	mkdir -p "$$packet_root"; \
	evidence=$$(mktemp -d "$$packet_root/plain-bnf-lean-denotation.XXXXXX"); \
	(cd "$$lean_root" && lake build \
		Mettapedia.GSLT.Tools.ExportPlainBnfDenotationCases) \
		> "$$evidence/lean-build.log" 2>&1 || { \
		tail -n 40 "$$evidence/lean-build.log" >&2; exit 1; \
	}; \
	(cd "$$lean_root" && lake env lean --run \
		Mettapedia/GSLT/Tools/ExportPlainBnfDenotationCases.lean \
		"$$evidence/cases.metta" "$$evidence/expected.txt" \
		"$$evidence/mutated-cases.metta"); \
	$(CETTA_PETTA_DIRECT_RUN_V1) \
		$(PLAIN_BNF_SEMANTIC_ADMISSION_PETTA_PROGRAM_V1) \
		lib/lib_bnf.metta "$$evidence/cases.metta" \
		> "$$evidence/actual.txt"; \
	(cd "$$lean_root" && nice -n 19 lake env lean --run \
		Mettapedia/GSLT/Tools/ExportPlainBnfDenotationCases.lean \
		--check "$$evidence/actual.txt"); \
	$(CETTA_PETTA_DIRECT_RUN_V1) \
		$(PLAIN_BNF_SEMANTIC_ADMISSION_PETTA_PROGRAM_V1) \
		lib/lib_bnf.metta "$$evidence/mutated-cases.metta" \
		> "$$evidence/mutated-actual.txt"; \
	(cd "$$lean_root" && nice -n 19 lake env lean --run \
		Mettapedia/GSLT/Tools/ExportPlainBnfDenotationCases.lean \
		--check-mutant "$$evidence/mutated-actual.txt")

test-tptp-extended-bnf-meta-parser-v1-body: \
		$(TPTP_EXTENDED_BNF_META_PARSER_V1_TEST_BIN)
	@if [[ -z "$(strip $(TPTP_OFFICIAL_SYNTAX_BNF_V1))" || \
		! -f "$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" ]]; then \
		echo 'set TPTP_OFFICIAL_SYNTAX_BNF_V1 to the official SyntaxBNF file' >&2; \
		exit 2; \
	fi
	@$(TPTP_EXTENDED_BNF_META_PARSER_V1_TEST_BIN) \
		"$(TPTP_OFFICIAL_SYNTAX_BNF_V1)"

test-tptp-extended-bnf-full-reader-v1-body: \
		$(BIN) $(TPTP_EXTENDED_BNF_FULL_READER_V1_TEST) \
		langdef/tptp/official_extended_bnf_source_v1.metta \
		langdef/tptp/official_extended_bnf_parser_profile_v1.metta \
		langdef/tptp/official_extended_bnf_ast_v1.metta \
		langdef/tptp/official_extended_bnf_ast_projection_v1.metta
	@if [[ -z "$(strip $(TPTP_OFFICIAL_SYNTAX_BNF_V1))" || \
		! -f "$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" ]]; then \
		echo 'set TPTP_OFFICIAL_SYNTAX_BNF_V1 to the official SyntaxBNF file' >&2; \
		exit 2; \
	fi
	@set -eu; \
	result=$$($(CETTA_BIN_INVOKE) --quiet --profile extended --lang he --profile extended \
		$(TPTP_EXTENDED_BNF_FULL_READER_V1_TEST) \
		"$(TPTP_OFFICIAL_SYNTAX_BNF_V1)"); \
	test "$$result" = '(TptpOfficialExtendedBnfFullReaderV1Summary 2 2 0)'

qualify-tptp-reader-source-lean-v1:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo 'METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project' >&2; exit 2; \
	}; \
	client="$(abspath $(TPTP_READER_SOURCE_QUALIFICATION_V1))"; \
	models='Mettapedia/GSLT/Parsing/TptpCorpusCompatibilitySource.lean Mettapedia/GSLT/Parsing/TptpOfficialCompositionSource.lean'; \
	if grep -En '\b(sorry|admit|axiom|native_decide|theorem_wanted)\b|_wanted' \
		"$$client" $$(for model in $$models; do printf '%s/%s ' "$$lean_root" "$$model"; done); then \
		echo 'TPTP reader source qualification contains a proof placeholder' >&2; exit 1; \
	fi; \
	if grep -En 'cetta-prime-nik|/home/|/shared/' \
		"$$client" $$(for model in $$models; do printf '%s/%s ' "$$lean_root" "$$model"; done); then \
		echo 'TPTP reader source qualification contains a checkout-specific path' >&2; exit 1; \
	fi; \
	(cd "$$lean_root" && LAKE_JOBS=3 nice -n 19 lake build \
		Mettapedia.GSLT.Parsing.TptpCorpusCompatibilitySource \
		Mettapedia.GSLT.Parsing.TptpOfficialCompositionSource && \
		lake env lean -DwarningAsError=true "$$client")
	@echo '(TptpReaderSourceLeanV1Summary 2 2 0)'

test-tptp-official-reusable-reader-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_SYNTAX_BNF_V1="$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-tptp-official-reusable-reader-v1-body

test-tptp-official-reader-utilities-real-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-tptp-official-reader-utilities-real-v1-body

qualify-collection-ingress-lean:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_CHECKOUT)"; \
	test -f "$$lean_root/lakefile.lean" || \
		{ echo 'Set METTAPEDIA_LEAN_ROOT to the MeTTapedia Lean project' >&2; exit 2; }; \
	contract="$(abspath langdef/collections/generated/Ingress.lean)"; \
	cd "$$lean_root"; \
	lake env lean -DwarningAsError=true "$$contract"

qualify-collection-keys-lean:
	@set -eu; \
	contract="$(abspath langdef/collections/generated/KeyEncoding.lean)"; \
	cd "$(METTAPEDIA_LEAN_CHECKOUT)"; \
	lake env lean -DwarningAsError=true "$$contract"

qualify-collection-theories-lean:
	@set -eu; \
	contract="$(abspath langdef/collections/Theories.lean)"; \
	cd "$(METTAPEDIA_LEAN_CHECKOUT)"; \
	lake env lean -DwarningAsError=true "$$contract"

qualify-postings-descriptors-lean:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_CHECKOUT)"; \
	test -f "$$lean_root/lakefile.lean" || \
		{ echo 'Set METTAPEDIA_LEAN_ROOT to the MeTTapedia Lean project' >&2; exit 2; }; \
	descriptors="$(abspath langdef/postings/generated/Descriptors.lean)"; \
	cd "$$lean_root"; \
	lake env lean -DwarningAsError=true "$$descriptors"

qualify-tensor-support-lean:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_CHECKOUT)"; \
	test -f "$$lean_root/lakefile.lean" || \
		{ echo 'Set METTAPEDIA_LEAN_ROOT to the MeTTapedia Lean project' >&2; exit 2; }; \
	client="$(abspath tests/langdef/tensor/support_contract_v1.lean)"; \
	descriptors="$(abspath langdef/tensor/generated/Descriptors.lean)"; \
	cd "$$lean_root"; \
	lake build Mettapedia.Algebra.SupportTensorRounding \
		Mettapedia.Algebra.SupportTensorScreening \
		Mettapedia.Algebra.SupportTensorLayout \
		Mettapedia.Algebra.SupportTensorSchedule \
		Mettapedia.Algebra.SupportTensorObservers \
		Mettapedia.Algorithms.Retrieval.GroupedFold \
		Mettapedia.Algorithms.Retrieval.PrefixSearch \
		Mettapedia.GSLT.LanguageDef.ModuleSource.Commands \
		Mettapedia.GSLT.LanguageDef.ModuleSource.Controls; \
	lake env lean -DwarningAsError=true "$$client"; \
	lake env lean -DwarningAsError=true "$$descriptors"

refresh-petta-reference-vocabulary-v1:
	@test -n "$(PETTA_REFERENCE_ROOT)" || \
		{ echo 'set PETTA_REFERENCE_ROOT to a PeTTa checkout' >&2; exit 2; }
	bash tests/support/petta_reference_vocabulary_v1.sh \
		"$(PETTA_REFERENCE_ROOT)" $(PETTA_REFERENCE_VOCABULARY_V1)

test-tptp-official-reader-canonical-print-real-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-tptp-official-reader-canonical-print-real-v1-body

test-tptp-official-compact-include-resolution-real-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-tptp-official-compact-include-resolution-real-v1-body

test-tptp-official-compact-resolution-consumer-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 \
		TPTP_OFFICIAL_CORPUS_ROOT_V1="$(TPTP_OFFICIAL_CORPUS_ROOT_V1)" \
		test-tptp-official-compact-resolution-consumer-v1-body

test-tptp-official-syntax-gate-failure-v1:
	@set -eu; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/tptp-reader-gate-failure-v1.XXXXXX"); \
	bash tests/support/check_tptp_reader_gate_failure_v1.sh "$$evidence" \
		"$(TPTP_OFFICIAL_SYNTAX_BNF_V1)" "$(TPTP_OFFICIAL_CORPUS_ROOT_V1)"

qualify-grammar-constructor-actions-lean-native-v1: test-grammar-constructor-actions-v1
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ]; then lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; fi; \
	test -f "$$lean_root/lakefile.lean" || { \
		echo 'METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project' >&2; exit 2; \
	}; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(abspath $(BOOTSTRAP_TMPDIR))/grammar-constructor-actions.XXXXXX"); \
	(cd "$$lean_root" && LAKE_JOBS=3 nice -n 19 lake build \
		Mettapedia.GSLT.Tools.ExportGrammarConstructorActions) \
		> "$$evidence/lean-build.log" 2>&1 || { \
		tail -n 35 "$$evidence/lean-build.log" >&2; exit 1; \
	}; \
	(cd "$$lean_root" && nice -n 19 lake env lean --run \
		Mettapedia/GSLT/Tools/ExportGrammarConstructorActions.lean) \
		> "$$evidence/cases.metta"; \
	cmp "$$evidence/cases.metta" "$(GRAMMAR_CONSTRUCTOR_ACTIONS_V1_FIXTURES)"; \
	printf '%s\n' 'Typed action export matches the checked-in native fixtures.'

qualify-metta-atom-actions-lean-native-v1: test-metta-atom-actions-v1
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ]; then lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; fi; \
	test -f "$$lean_root/lakefile.lean" || { \
		echo 'METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project' >&2; exit 2; \
	}; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(abspath $(BOOTSTRAP_TMPDIR))/metta-atom-actions.XXXXXX"); \
	(cd "$$lean_root" && LAKE_JOBS=3 nice -n 19 lake build \
		Mettapedia.GSLT.Tools.ExportMeTTaAtomActions) \
		> "$$evidence/lean-build.log" 2>&1 || { \
		tail -n 35 "$$evidence/lean-build.log" >&2; exit 1; \
	}; \
	(cd "$$lean_root" && nice -n 19 lake env lean --run \
		Mettapedia/GSLT/Tools/ExportMeTTaAtomActions.lean) \
		> "$$evidence/cases.metta"; \
	cmp "$$evidence/cases.metta" "$(METTA_ATOM_ACTIONS_V1_FIXTURES)"; \
	printf '%s\n' 'Typed atom-action export matches the native fixtures.'

qualify-json-gslt-native-forest-exact-v1-body: \
		$(LANGUAGE_DEF_PARSER_PACK_V1_TEST_BIN)
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && \
	   [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo "METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project" >&2; \
		exit 2; \
	}; \
	packet_root="$(abspath $(BOOTSTRAP_TMPDIR))"; \
	mkdir -p "$$packet_root"; \
	identity=$$(mktemp "$$packet_root/json-parser-identity.XXXXXX"); \
	gll_forest=$$(mktemp "$$packet_root/json-gll-forest.XXXXXX"); \
	glr_forest=$$(mktemp "$$packet_root/json-glr-forest.XXXXXX"); \
	trap 'rm -f "$$identity" "$$gll_forest" "$$glr_forest"' \
		EXIT INT TERM; \
	CETTA_JSON_PARSER_PACK_IDENTITY_WIRE_OUT="$$identity" \
	CETTA_JSON_GLL_FOREST_WIRE_OUT="$$gll_forest" \
	CETTA_JSON_GLR_FOREST_WIRE_OUT="$$glr_forest" \
		$(LANGUAGE_DEF_PARSER_PACK_V1_TEST_BIN); \
	(cd "$$lean_root" && \
		lake exe checkRFC8259NativeForestExact \
			gll "$$identity" "$$gll_forest"); \
	(cd "$$lean_root" && \
		lake exe checkRFC8259NativeForestExact \
			glr "$$identity" "$$glr_forest"); \
	echo "PASS: live JSON GLL and GLR forests exactly cover the independent root-relative ParserPack chart"

qualify-json-projection-refinement-lean-v1:
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if [ -z "$$lean_root" ] && [ -f "$(METTAPEDIA_LEAN_AUTO_ROOT)/lakefile.lean" ]; then \
		lean_root="$(METTAPEDIA_LEAN_AUTO_ROOT)"; \
	fi; \
	test -n "$$lean_root" && test -f "$$lean_root/lakefile.lean" || { \
		echo 'METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project' >&2; exit 2; \
	}; \
	client="$(abspath $(JSON_PROJECTION_REFINEMENT_LEAN_V1))"; \
	if grep -En '\b(sorry|admit|axiom|native_decide|theorem_wanted)\b|_wanted' "$$client"; then \
		echo 'JSON projection refinement contains a proof placeholder' >&2; exit 1; \
	fi; \
	if grep -En 'cetta-prime-nik|/home/|/shared/' "$$client"; then \
		echo 'JSON projection refinement contains a checkout-specific path' >&2; exit 1; \
	fi; \
	(cd "$$lean_root" && lake build \
		Mettapedia.GSLT.LanguageDef.RFC8259ValueSemantics \
		Mettapedia.GSLT.Dynamics.PartialRegionPlan && \
		lake env lean -DwarningAsError=true "$$client")
	@echo '(JsonProjectionRefinementLeanV1Summary 8 8 0)'

test-ic-interpreter-correspondence:
	@test -n "$(strip $(IC_ORIGINAL_TOOLCHAIN))" || { \
		echo 'Set IC_ORIGINAL_TOOLCHAIN to the installed toolchain matching ic-theory/lean-toolchain' >&2; exit 2; \
	}
	@test -f "$(METTAPEDIA_LEAN_CHECKOUT)/lakefile.lean" || { \
		echo 'Set METTAPEDIA_LEAN_ROOT to the Mettapedia Lean project' >&2; exit 2; \
	}
	python3 tests/support/test_ic_interpreter_bridge.py
	python3 tests/support/check_ic_interpreter_bridge.py \
		--ic-theory "$(IC_THEORY_ROOT)" \
		--mettapedia "$(abspath $(METTAPEDIA_LEAN_CHECKOUT)/../..)" \
		--original-toolchain "$(IC_ORIGINAL_TOOLCHAIN)" \
		--output runtime/ic-original-interpreter-qualification

test-execution-theory:
	@test -n "$(METTAPEDIA_ROOT)" || { echo 'Set METTAPEDIA_ROOT to the Lean project root'; exit 1; }
	@python3 tests/support/check_execution_lean_models.py --mettapedia "$(METTAPEDIA_ROOT)" \
		--receipt "$(EXECUTION_MODEL_RECEIPT)"

build-lang-datalog:
	@set -eu; lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if test -z "$$lean_root"; then lean_root="$(abspath ../../Mettapedia/lean/mettapedia)"; fi; \
	test -f "$$lean_root/lakefile.lean" || \
		{ echo 'Set METTAPEDIA_LEAN_ROOT to the MeTTapedia Lean project' >&2; exit 2; }; \
	python3 tools/build_datalog_source_compiler.py --lean-root "$$lean_root" \
		--output runtime/lang/datalog-compiler.so --package-output langdef/lang/Datalog.module

# The answer-set solver against clingo: on random ground normal programs the
# sets of stable models agree, and every refutation the solver emits is
# accepted by its certificate checker, which refuses truncated certificates
# and those offered for programs that have a stable model.
.PHONY: test-asp-clingo-differential
test-asp-clingo-differential: $(ASP_CDNL_TEST_BIN)
	@if [ -z "$(strip $(ASP_CLINGO_PYTHON))" ]; then \
		echo 'set ASP_CLINGO_PYTHON to a Python with the clingo module'; \
		exit 1; \
	fi
	@for seed in 1 2 3; do \
		"$(ASP_CLINGO_PYTHON)" tools/asp_clingo_differential_v1.py ./$(ASP_CDNL_TEST_BIN) $$seed 200 || exit 1; \
	done
	@echo "PASS: the answer-set solver's stable models equal clingo's on 600 random programs, and its certificates check"

# The clingo heads of the asp library against its solver.
.PHONY: test-asp-clingo-reference
test-asp-clingo-reference: $(BIN) tests/asp/clingo_reference.metta tests/asp/clingo_reference.expected
	@if [ -z "$(strip $(CETTA_CLINGO_LIBRARY))" ]; then \
		echo 'set CETTA_CLINGO_LIBRARY to a libclingo shared library'; \
		exit 1; \
	fi
	@CETTA_CLINGO_LIBRARY="$(CETTA_CLINGO_LIBRARY)" $(call cetta_exec,./$(BIN)) --lang petta tests/asp/clingo_reference.metta > runtime/test-asp-clingo-reference.out 2>&1
	@diff -u tests/asp/clingo_reference.expected runtime/test-asp-clingo-reference.out
	@echo "PASS: on programs grounded by clingo and programs given as data, the answer-set solver's stable models equal clingo's"
