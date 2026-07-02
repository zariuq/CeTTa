; PeTTa compatibility loader for the 1.4M-tier WM-PLN checkpoint.
; Positive example:
; keep the shared bc/rulebase design, but flatten the KB imports one level so
; PeTTa can validate the same workload without depending on nested import-to-
; named-space behavior.
;
; Negative example:
; rewrite the shared benchmark architecture around a runtime-specific import
; quirk.

!(import! &self ./lib_pln_inference_rules.metta)

!(import! &bio-wmpln-rb ./lib_pln_bridge_rules.metta)

!(import! &bio-wmpln-kb ../data/kb_eqtl_gene_links.metta)
!(import! &bio-wmpln-kb-refseq ../data/kb_refseq_gene_annotations.metta)
!(import! &bio-wmpln-kb-tfbs ../data/kb_multirelational_scaled.metta)
!(import! &bio-wmpln-kb-hetionet ../data/kb_hetionet_biomedical_graph.metta)
!(import! &bio-wmpln-kb-patterns ../data/kb_mined_patterns.metta)

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
