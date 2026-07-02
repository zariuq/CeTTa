; Current 1.4M-atom data tier for the realistic WM-PLN workload.
; Positive example:
; keep a stable current-tier corpus while the rule catalogue evolves.
;
; Negative example:
; hard-code imports into benchmark frontends and make future 5M/15M scaling
; a semantic rewrite instead of a data swap.

!(import! &self ../data/kb_eqtl_gene_links.metta)
!(import! &self ../data/kb_refseq_gene_annotations.metta)
!(import! &self ../data/kb_multirelational_scaled.metta)
!(import! &self ../data/kb_hetionet_biomedical_graph.metta)
!(import! &self ../data/kb_mined_patterns.metta)
