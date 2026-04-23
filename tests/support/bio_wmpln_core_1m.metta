; Shared realistic WM-PLN workload core over the current 1.4M-atom tier.
; Positive example:
; keep the engine/helper definitions in &self while loading raw facts and
; rule/bridge declarations into dedicated spaces that bc consults via match.
;
; Negative example:
; mix raw data, leaf adapters, and rule declarations into the evaluator space
; and then pretend bc is generic.

!(import! &self ./bio_wmpln_workload_rules.metta)

!(import! &bio-wmpln-rb ./bio_wmpln_rulebase_1m.metta)

!(import! &bio-wmpln-kb ./bio_wmpln_data_1m.metta)
