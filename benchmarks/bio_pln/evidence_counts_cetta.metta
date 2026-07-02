; Portable PLN evidence-count benchmark, CeTTa import wrapper.
; The score row in the historical snapshot is derived from these raw counts:
;   positive = eQTL-positive + ABC-positive + regulatory-positive
;   negative = eQTL-negative + ABC-negative + regulatory-negative

!(import! &self ../data/kb_chr16_variants.metta)

(= (bio-evidence-counts $snp $gene)
   (match &self
     (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (bio-evidence $snp $gene (+ (+ $ep $ap) $rp) (+ (+ $en $an) $rn))))

!(bio-evidence-counts $snp $gene)
!(bio-evidence-counts rs9923732 ensg00000166455)
