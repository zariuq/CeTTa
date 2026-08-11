; A real proof-trace execution slice: generated sequence/assertion semantics,
; generic trace execution and compilation, an authored extensional input ABI,
; and the authored compressed-code interpretation.  Input rows are deliberately
; absent so they remain open capabilities of the generated native-type program.

(gslt-composition-v1
  (name MetamathProofTraceExecutionCanaryV1)
  (source "../../../../langdef/metamath/generated/proof_semantic_exec_v1.metta")
  (source "../core/proof_gslt_trace_semantics_v1.metta")
  (source "../core/proof_gslt_trace_compiler_v1.metta")
  (source "../core/proof_gslt_trace_input_v1.metta")
  (source "../core/proof_gslt_trace_input_interface_v1.metta")
  (source "../../../../langdef/metamath/proof_trace_codec_v1.metta")
)
