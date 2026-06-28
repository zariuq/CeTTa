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
!(import! &bio-wmpln-kb-refseq ../bio_refseq_723k.metta)
!(import! &bio-wmpln-kb-tfbs ../bio_multirelational_kb_scaled.metta)
!(import! &bio-wmpln-kb-hetionet ../bio_hetionet_kb.metta)
!(import! &bio-wmpln-kb-patterns ../discovered_patterns_1k.metta)

; Keep the public seam unchanged for PeTTa, but avoid both:
; - CeTTa's restriction that import! may only target a fresh &name or &self
; - PeTTa's current no-op batch add-atoms behavior over get-atoms streams
(: copy-space-atoms-fold (-> Atom Atom Number))
(= (copy-space-atoms-fold $src $dst)
   (foldall + (match $src $x (let $u (add-atom $dst $x) 0)) 0))

!(copy-space-atoms-fold &bio-wmpln-kb-refseq &bio-wmpln-kb)
!(copy-space-atoms-fold &bio-wmpln-kb-tfbs &bio-wmpln-kb)
!(copy-space-atoms-fold &bio-wmpln-kb-hetionet &bio-wmpln-kb)
!(copy-space-atoms-fold &bio-wmpln-kb-patterns &bio-wmpln-kb)
