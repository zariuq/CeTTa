; True full raw-loader WM-PLN data tier matching benchmarks/bench_bio_1M.metta.
; Includes the TFBS shard in addition to the current post-split checkpoint tier.
;
; Raw line counts in the current tree:
;   eqtl_for_mining.metta                128,093
;   bio_refseq_723k.metta                723,534
;   bio_tfbs_415k.metta                  415,073
;   bio_multirelational_kb_scaled.metta  132,683
;   bio_hetionet_kb.metta                    591
;   discovered_patterns_1k.metta              23
;   total                              1,399,997

!(import! &self ../eqtl_for_mining.metta)
!(import! &self ../bio_refseq_723k.metta)
!(import! &self ../bio_tfbs_415k.metta)
!(import! &self ../bio_multirelational_kb_scaled.metta)
!(import! &self ../bio_hetionet_kb.metta)
!(import! &self ../discovered_patterns_1k.metta)
