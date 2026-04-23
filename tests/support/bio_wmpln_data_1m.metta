; Current 1.4M-atom data tier for the realistic WM-PLN workload.
; Positive example:
; keep a stable current-tier corpus while the rule catalogue evolves.
;
; Negative example:
; hard-code imports into benchmark frontends and make future 5M/15M scaling
; a semantic rewrite instead of a data swap.

!(import! &self ../eqtl_for_mining.metta)
!(import! &self ../bio_refseq_723k.metta)
!(import! &self ../bio_multirelational_kb_scaled.metta)
!(import! &self ../bio_hetionet_kb.metta)
!(import! &self ../discovered_patterns_1k.metta)
