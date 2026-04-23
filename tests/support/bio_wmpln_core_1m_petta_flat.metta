; PeTTa compatibility loader for the 1.4M-tier WM-PLN checkpoint.
; Positive example:
; keep the shared bc/rulebase design, but flatten the KB imports one level so
; PeTTa can validate the same workload without depending on nested import-to-
; named-space behavior.
;
; Negative example:
; rewrite the shared benchmark architecture around a runtime-specific import
; quirk.

!(import! &self ./bio_wmpln_workload_rules.metta)

!(import! &bio-wmpln-rb ./bio_wmpln_rulebase_1m.metta)

!(import! &bio-wmpln-kb ../eqtl_for_mining.metta)
!(import! &bio-wmpln-kb ../bio_refseq_723k.metta)
!(import! &bio-wmpln-kb ../bio_multirelational_kb_scaled.metta)
!(import! &bio-wmpln-kb ../bio_hetionet_kb.metta)
!(import! &bio-wmpln-kb ../discovered_patterns_1k.metta)
