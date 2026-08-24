(gslt-langdef-v1
  (name MM0TextualV1)
  (source "../../experiments/gslt2parse_foundation/presentations/core/syntax_core_v1.metta")
  (source "../../experiments/gslt2parse_foundation/presentations/shared/lookahead_core_v1.metta")
  (source "../../experiments/gslt2parse_foundation/presentations/shared/char_core_v1.metta")
  (source "../../experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta")
  (source "syntax_v1.metta")
  (start (pp-def mm0-file))
  (parser-pack "generated/parser_pack_v1.abi")
  (parser-pack-closure partial)
  (extension-artifact parser-regular-span-compiler-v1
    "../../experiments/gslt2parse_foundation/presentations/compiler/regular_span_compiler_v1.metta")
  (extension-artifact parser-guarded-span-compiler-v1
    "../../experiments/gslt2parse_foundation/presentations/compiler/guarded_regular_span_compiler_v1.metta")
  (extension-artifact parser-lexical-nfa-v1
    "generated/lexical_nfa_v1.answers")
  (extension-artifact parser-guard-nfa-v1
    "generated/guard_nfa_v1.answers")
  (extension-artifact parser-positive-guard-evidence-v1
    "generated/parser_guard_evidence_v1.abi")
  (extension-artifact parser-guarded-nfa-v1
    "generated/guarded_nfa_v1.answers")
  (lock "generated/langdef_lock_v1.metta"))
