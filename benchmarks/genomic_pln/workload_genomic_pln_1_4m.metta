; Shared realistic WM-PLN workload core over the true full 1.4M raw-loader tier.
; Keep this separate from the post-split checkpoint tier so benchmark rows can
; say exactly which workload they ran.

!(import! &self ./lib_pln_inference_rules.metta)

!(import! &bio-wmpln-rb ./lib_pln_bridge_rules.metta)

!(import! &bio-wmpln-kb ./kb_genomic_facts_1_4m.metta)
