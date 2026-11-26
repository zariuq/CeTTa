# Lean alignment
#
# Tools that compare CeTTa with the Mettapedia checkout: with its Lean
# developments, run by its Lean toolchain, and with MeTTa sources kept there.
# They are alignment tools, not tests.  make test, make test-all, the stage
# targets, readiness (candidate, suite, routine, exhaustive), readiness
# calibration and CI never run them, and no test-* target depends on one.
# Each stops and names the setting it needs when that is missing.
#
#   METTAPEDIA_LEAN_ROOT  The Mettapedia Lean project (lean/mettapedia in the
#                         MeTTapedia repository); by default the checkout
#                         beside this repository.
#   METTAPEDIA_ROOT       For the tools that read it: the Lean project
#                         (lean-alignment-pattern-models,
#                         lean-alignment-execution-theory) or the MeTTapedia
#                         checkout (lean-alignment-gslt2parse-he-reader-*).
#
# make lean-alignment runs the tools that need only the Lean project.  The
# qualify-*-lean* targets and the check-*-wire targets of mk/external.mk also
# run Lean: they check CeTTa's descriptors and wires against their Lean
# sources.  lean-lang-provider-coexistence is not an alignment check: it loads
# a Datalog provider built from Lean beside the packaged CHR provider.

.PHONY: lean-alignment lean-alignment-rhocalc lean-alignment-corpus \
	lean-alignment-rhocalc-cost lean-alignment-store-demand-contracts \
	lean-alignment-abt lean-alignment-petta-typecheck-v3-core-v1 \
	lean-alignment-he-compat-semantic-suite lean-alignment-prime-nik-export-v1 \
	lean-alignment-prime-nik-megalodon-witnesses-v1 \
	lean-alignment-plain-bnf-semantic-generated-artifact-current-v1 \
	lean-alignment-plain-bnf-semantic-generated-artifact-current-v1-body \
	lean-alignment-plain-bnf-typed-admission-v1 \
	lean-alignment-plain-bnf-typed-admission-v1-body \
	lean-alignment-pattern-models lean-alignment-execution-theory \
	lean-alignment-ic-interpreter-correspondence \
	lean-alignment-incremental-compression-joined \
	lean-alignment-gslt2parse-he-reader-escape-v1 \
	lean-alignment-gslt2parse-he-reader-source-correspondence-v1 \
	lean-alignment-gslt2parse-rho-abstract-syntax-v1 \
	lean-lang-provider-coexistence

lean-alignment: lean-alignment-rhocalc lean-alignment-corpus lean-alignment-abt \
	lean-alignment-petta-typecheck-v3-core-v1 lean-alignment-store-demand-contracts \
	lean-alignment-plain-bnf-typed-admission-v1 \
	lean-alignment-plain-bnf-semantic-generated-artifact-current-v1

# Cost-rho: the bounded causal-prefix cases (scripts/check_rhocalc_cost_prefixes.py,
# which test-rhocalc runs against their recorded outputs) through the Lean cost
# executor, and the receipts CeTTa printed replayed as Lean derivations; also the
# cost-syntax bridges and the Lean microcheck.
lean-alignment-rhocalc: $(BIN)
	@pass=0; fail=0; \
	mettapedia_root="$(METTAPEDIA_LEAN_CHECKOUT)"; \
	if [ ! -f "$$mettapedia_root/lakefile.lean" ]; then \
		echo "FAIL: METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project"; \
		exit 2; \
	fi; \
	rhocalc_lean_skip=85; \
	while IFS=$$(printf '\t') read -r name fixture expected_file lean_file anchor; do \
		[ -n "$$name" ] || continue; \
		case "$$name" in \#*) continue ;; esac; \
		bridge_output=$$(METTAPEDIA_ROOT="$$mettapedia_root" $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_lean_bridge.py "$(CETTA_SCRIPT_BIN)" "$$fixture" "$$expected_file" "$$lean_file" "$$anchor" 2>&1); \
		bridge_status=$$?; \
		if [ "$$bridge_status" -eq 0 ]; then \
			echo "PASS: rhocalc cost lean bridge $$name"; \
			pass=$$((pass + 1)); \
		elif [ "$$bridge_status" -eq "$$rhocalc_lean_skip" ]; then \
			printf '%s\n' "$$bridge_output"; \
			echo "SKIP: rhocalc cost lean bridge $$name"; \
		else \
			printf '%s\n' "$$bridge_output"; \
			echo "FAIL: rhocalc cost lean bridge $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done < tests/rhocalc_cost_lean_bridge.tsv; \
	differential_output=$$(METTAPEDIA_ROOT="$$mettapedia_root" $(CETTA_SCRIPT_RUN_ENV) python3 scripts/rhocalc_cost_differential.py "$(CETTA_SCRIPT_BIN)" 2>&1); \
	differential_status=$$?; \
	if [ "$$differential_status" -eq 0 ]; then \
		printf '%s\n' "$$differential_output"; \
		pass=$$((pass + 1)); \
	else \
		printf '%s\n' "$$differential_output"; \
		echo "FAIL: bounded cost-rho CeTTa/Lean differential"; \
		fail=$$((fail + 1)); \
	fi; \
	microcheck_output=$$(python3 scripts/rhocalc_lean_microcheck.py "$$mettapedia_root" tests/rhocalc_lean_microcheck.lean 2>&1); \
	microcheck_status=$$?; \
	if [ "$$microcheck_status" -eq 0 ]; then \
		echo "PASS: rhocalc lean microcheck"; \
		pass=$$((pass + 1)); \
	elif [ "$$microcheck_status" -eq "$$rhocalc_lean_skip" ]; then \
		printf '%s\n' "$$microcheck_output"; \
		echo "SKIP: rhocalc lean microcheck"; \
	else \
		printf '%s\n' "$$microcheck_output"; \
		echo "FAIL: rhocalc lean microcheck"; \
		fail=$$((fail + 1)); \
	fi; \
	echo "---"; \
	echo "rhocalc lean: $$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

lean-alignment-rhocalc-cost: $(BIN)
	@test -f "$(METTAPEDIA_LEAN_CHECKOUT)/lakefile.lean" || { \
		echo 'Set METTAPEDIA_LEAN_ROOT to the Mettapedia Lean project' >&2; exit 2; \
	}
	@METTAPEDIA_ROOT="$(METTAPEDIA_LEAN_CHECKOUT)" $(CETTA_SCRIPT_RUN_ENV) \
		python3 scripts/rhocalc_cost_differential.py "$(CETTA_SCRIPT_BIN)"

# Corpus files that import MeTTa sources from a Mettapedia checkout beside this
# repository.  They are outside the default corpus (tests/test_*.metta).
LEAN_ALIGNMENT_CORPUS = tests/mettapedia_lts_mi_syntax.metta

lean-alignment-corpus: $(BIN)
	@pass=0; fail=0; \
	for f in $(LEAN_ALIGNMENT_CORPUS); do \
		exp="$${f%.metta}.expected"; \
		runtime_status=0; result=$$($(CETTA_BIN_INVOKE) --profile extended --lang he "$$f" 2>&1) || runtime_status=$$?; \
		if [ $$runtime_status -eq 0 ] && [ "$$result" = "$$(cat $$exp)" ]; then \
			echo "PASS: $$f"; \
			pass=$$((pass + 1)); \
		elif [ $$runtime_status -eq 0 ] && python3 tests/support/compare_answers.py \
				--lang he --program "$$f" --expected "$$exp" \
				--actual <(printf '%s\n' "$$result") 2>/dev/null; then \
			echo "PASS: $$f (answers as bags)"; \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f (runtime status $$runtime_status)"; \
			diff <(cat "$$exp") <(echo "$$result") | head -10; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "Mettapedia corpus: $$pass passed, $$fail failed"; \
	[ $$fail -eq 0 ]

# The store-and-demand contracts of Mettapedia's MettaKernel examples, replayed
# against CeTTa through their own fixtures.
lean-alignment-store-demand-contracts: $(BIN)
	@set -eu; \
	fixtures="$(METTAPEDIA_LEAN_CHECKOUT)/../../MettaKernel/Examples/StoreAndDemand"; \
	test -f "$$fixtures/replay.py"; \
	python3 "$$fixtures/replay.py" --binary "$(abspath $(BIN))"; \
	python3 "$$fixtures/replay.py" --binary "$(abspath $(BIN))" --petta-reference

lean-alignment-abt: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/check_abt_differential.py "$(CETTA_SCRIPT_BIN)"

lean-alignment-petta-typecheck-v3-core-v1:
	@python3 tools/test_petta_typecheck_v3_core_parity_v1.py \
		--lean-core $(PETTA_TYPECHECK_V3_CORE_LEAN_V1) \
		--lean-seam $(PETTA_TYPECHECK_V3_SEAM_LEAN_V1) \
		--langdef $(PETTA_TYPECHECK_V3_CORE_LANGDEF_V1)

# The HE-compat semantic suite runs Mettapedia's HE conformance scripts,
# including its Lean anchors.
lean-alignment-he-compat-semantic-suite: $(BIN)
	@$(CETTA_SCRIPT_RUN_ENV) python3 scripts/check_he_compat_semantic_suite.py \
		--catalog $(HE_COMPAT_CATALOG)

# Current-model synchronization is distinct from replaying the saved packages.
# The export remains comparison evidence; this target does not install it.
lean-alignment-prime-nik-export-v1: $(PRIME_NIK_AUTHORITY_EXPORTER_V1) \
		$(PRIME_NIK_AUTHORITY_EXPORT_CHECK_V1)
	@test -n "$(METTAPEDIA_LEAN_ROOT)" || { \
		echo "METTAPEDIA_LEAN_ROOT must name the Mettapedia Lean project"; \
		exit 2; \
	}
	@set -eu; \
	mkdir -p $(BOOTSTRAP_TMPDIR); \
	export_evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/prime-nik-export.XXXXXX"); \
	trap 'status=$$?; if [ $$status -eq 0 ]; then rm -rf "$$export_evidence"; else echo "kept for inspection: $$export_evidence" >&2; fi' EXIT; \
	export_evidence_abs=$$(realpath "$$export_evidence"); \
	checked_catalog_abs=$$(realpath "$(PRIME_NIK_AUTHORITY_CATALOG_V1)"); \
	(cd "$(METTAPEDIA_LEAN_ROOT)" && \
		lake build \
			Mettapedia.Languages.MeTTa.PrimeCandidates.MinimalCheckingPackage \
			Mettapedia.Languages.Megalodon.ImplicationalKernel \
			Mettapedia.Languages.Megalodon.TheoryAdmissionKernel \
			Mettapedia.Languages.Megalodon.DefinitionConversionWireRefinement \
			Mettapedia.GSLT.LanguageDef.InferenceMeTTaRender \
			Mettapedia.GSLT.LanguageDef.InferencePresentationWire \
			Mettapedia.GSLT.LanguageDef.InferenceABTWireRefinement \
			MeTTailCore.Crypto.SHA256 && \
		lake env lean --run \
			"$(abspath $(PRIME_NIK_AUTHORITY_EXPORTER_V1))" \
			"$$export_evidence_abs/catalog.metta"); \
	python3 $(PRIME_NIK_AUTHORITY_EXPORT_CHECK_V1) \
		--checked "$$checked_catalog_abs" \
		--exported "$$export_evidence_abs/catalog.metta" \
		--report "$$export_evidence_abs/comparison.json"; \
	echo "(PrimeNikLeanExportV1Summary exact=1 authorities=4)"

# The Megalodon differentials (test-prime-nik-megalodon-*, mk/external.mk) read
# the recorded witnesses; this regenerates each with its Lean exporter and
# compares.
lean-alignment-prime-nik-megalodon-witnesses-v1: \
		$(PRIME_NIK_MEGALODON_TERM_EXPORTER_V1) $(PRIME_NIK_MEGALODON_TERM_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_POLY_EXPORTER_V1) $(PRIME_NIK_MEGALODON_POLY_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1) $(PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1) \
		$(PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1) $(PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1)
	@set -eu; \
	lean_root="$(METTAPEDIA_LEAN_CHECKOUT)"; \
	test -f "$$lean_root/lakefile.lean" || { \
		echo 'Set METTAPEDIA_LEAN_ROOT to the Mettapedia Lean project' >&2; exit 2; \
	}; \
	mkdir -p $(BOOTSTRAP_TMPDIR); \
	evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/prime-nik-megalodon-witnesses.XXXXXX"); \
	evidence_abs=$$(realpath "$$evidence"); \
	trap 'rm -rf "$$evidence_abs"' EXIT INT TERM; \
	count=0; \
	for pair in \
		"$(abspath $(PRIME_NIK_MEGALODON_TERM_EXPORTER_V1))=$(abspath $(PRIME_NIK_MEGALODON_TERM_WITNESS_V1))" \
		"$(abspath $(PRIME_NIK_MEGALODON_POLY_EXPORTER_V1))=$(abspath $(PRIME_NIK_MEGALODON_POLY_WITNESS_V1))" \
		"$(abspath $(PRIME_NIK_MEGALODON_KNOWN_IMP_EXPORTER_V1))=$(abspath $(PRIME_NIK_MEGALODON_KNOWN_IMP_WITNESS_V1))" \
		"$(abspath $(PRIME_NIK_MEGALODON_DEFINITION_EXPORTER_V1))=$(abspath $(PRIME_NIK_MEGALODON_DEFINITION_WITNESS_V1))"; do \
		exporter=$${pair%%=*}; witness=$${pair#*=}; \
		count=$$((count + 1)); \
		(cd "$$lean_root" && lake env lean --run "$$exporter" "$$evidence_abs/$$count.witness"); \
		cmp "$$evidence_abs/$$count.witness" "$$witness"; \
	done; \
	echo "(PrimeNikMegalodonWitnessAlignmentV1Summary $$count $$count 0)"

# The generated admission program is regenerated through the Lean NativeType
# export and compared with the tracked artifact.
lean-alignment-plain-bnf-semantic-generated-artifact-current-v1-body: \
		$(LANGDEF_COMPILER_V1_BIN)
	@set -eu; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/plain-bnf-semantic.XXXXXX"); \
	trap 'status=$$?; if [ $$status -eq 0 ]; then rm -rf "$$evidence"; else echo "kept for inspection: $$evidence" >&2; fi' EXIT; \
	bash $(PLAIN_BNF_TYPED_GENERATOR_V1) $(LANGDEF_COMPILER_V1_BIN) \
		"$(PLAIN_BNF_LEAN_ROOT_V1)" "$$evidence/native-types.metta" \
		"$$evidence/program.metta" > "$$evidence/generation.log" 2>&1 || { \
		cat "$$evidence/generation.log" >&2; exit 1; \
	}; \
	cmp "$$evidence/program.metta" $(PLAIN_BNF_SEMANTIC_ADMISSION_PETTA_PROGRAM_V1)
	@echo '(PlainBnfSemanticGeneratedArtifactCurrentV1Summary 1 1 0)'

lean-alignment-plain-bnf-semantic-generated-artifact-current-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_RECEIPT_PRIMARY_INDEX=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_NEED_CLOSURE_CAPTURE=0 \
		ENABLE_PRIME_EVAL_STACK=0 \
		lean-alignment-plain-bnf-semantic-generated-artifact-current-v1-body

lean-alignment-plain-bnf-typed-admission-v1-body: $(BIN) $(LANGDEF_COMPILER_V1_BIN) \
		$(PLAIN_BNF_SEMANTIC_ADMISSION_PETTA_PROGRAM_V1) lib/lib_bnf.metta \
		tests/support/check_plain_bnf_typed_discovery_v1.sh \
		tests/support/check_plain_bnf_graph_index_v1.sh \
		tests/langdef/bnf/plain_bnf_typed_discovery_orchestration_v1.metta
	@set -eu; \
	mkdir -p "$(BOOTSTRAP_TMPDIR)"; \
	evidence=$$(mktemp -d "$(BOOTSTRAP_TMPDIR)/plain-bnf-typed-admission.XXXXXX"); \
	trap 'status=$$?; if [ $$status -eq 0 ]; then rm -rf "$$evidence"; else echo "kept for inspection: $$evidence" >&2; fi' EXIT; \
	bash tests/support/check_plain_bnf_typed_discovery_v1.sh \
		$(LANGDEF_COMPILER_V1_BIN) $(BIN) "$(PLAIN_BNF_LEAN_ROOT_V1)" "$$evidence"

lean-alignment-plain-bnf-typed-admission-v1:
	@$(MAKE) --no-print-directory \
		BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 \
		ENABLE_SANITIZERS=0 ENABLE_PIC=0 CETTA_TEST_ISOLATED=1 JSON_BACKEND=gslt \
		CETTA_PROVENANCE_ASSERT=0 RHOCOST_COMMIT_AUDIT=0 \
		ENABLE_PRIME_RECEIPT_PRIMARY_INDEX=0 \
		ENABLE_PRIME_NEED_HEAP_INDEX=0 \
		ENABLE_PRIME_NEED_CLOSURE_CAPTURE=0 \
		ENABLE_PRIME_EVAL_STACK=0 lean-alignment-plain-bnf-typed-admission-v1-body

lean-alignment-pattern-models:
	@test -n "$(METTAPEDIA_ROOT)" || { echo 'Set METTAPEDIA_ROOT to the Lean project root'; exit 1; }
	@python3 tests/support/check_pattern_lean_models.py --mettapedia "$(METTAPEDIA_ROOT)" \
		--receipt "$(PATTERN_MODEL_RECEIPT)"

lean-alignment-execution-theory:
	@test -n "$(METTAPEDIA_ROOT)" || { echo 'Set METTAPEDIA_ROOT to the Lean project root'; exit 1; }
	@python3 tests/support/check_execution_lean_models.py --mettapedia "$(METTAPEDIA_ROOT)" \
		--receipt "$(EXECUTION_MODEL_RECEIPT)"

lean-alignment-ic-interpreter-correspondence:
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

lean-alignment-incremental-compression-joined: test-incremental-compression \
	lean-alignment-ic-interpreter-correspondence

# The gslt2parse HE parser gates (mk/external.mk) observe CeTTa against recorded
# observations; these observe the Rust and LeaTTa parsers again at their pinned
# sources, check the LeaTTa sources, and regenerate the rho abstract syntax from
# rhoCalc.
lean-alignment-gslt2parse-he-reader-escape-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" || -z "$${METTAPEDIA_ROOT:-}" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout and METTAPEDIA_ROOT to the MeTTapedia checkout' >&2; \
		exit 2; \
	fi
	@python3 tools/test_he_reader_escape_differential_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--cetta-root "$(CURDIR)" \
		--mettapedia-root "$${METTAPEDIA_ROOT}"

lean-alignment-gslt2parse-he-reader-source-correspondence-v1:
	@if [[ -z "$(strip $(GSLT2PARSE_HE_ROOT))" ]]; then \
		echo 'set GSLT2PARSE_HE_ROOT to the pinned HE checkout'; \
		exit 1; \
	fi
	@python3 tools/test_he_reader_source_correspondence_v1.py \
		--he-root "$(GSLT2PARSE_HE_ROOT)" \
		--cetta-root "$(CURDIR)" \
		--mettapedia-root "$${METTAPEDIA_ROOT:-../../Mettapedia}"

lean-alignment-gslt2parse-rho-abstract-syntax-v1:
	@test -f "$(METTAPEDIA_LEAN_CHECKOUT)/lakefile.lean" || { \
		echo 'Set METTAPEDIA_LEAN_ROOT to the Mettapedia Lean project' >&2; exit 2; \
	}
	@python3 tools/test_rho_abstract_syntax_v1.py --mettapedia-root "$(METTAPEDIA_LEAN_CHECKOUT)"

lean-lang-provider-coexistence: $(BIN)
	@set -eu; lean_root="$(METTAPEDIA_LEAN_ROOT)"; \
	if test -z "$$lean_root"; then lean_root="$(abspath ../../Mettapedia/lean/mettapedia)"; fi; \
	work="$$(mktemp -d runtime/lang-provider-coexistence.XXXXXX)"; \
	python3 tests/support/check_lang_provider_coexistence.py ./$(BIN) "$$work/checks" --lean-root "$$lean_root"
