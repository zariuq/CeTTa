; Portable PLN evidence-count benchmark, PeTTa import wrapper.
; PeTTa is a distinct MeTTa dialect, but this benchmark keeps the body simple:
; integer evidence counts are the cross-engine contract.

!(import! &self ../../tests/bio_kb_chr16.metta)

(= (bio-evidence-counts $snp $gene)
   (match &self
     (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (bio-evidence $snp $gene (+ (+ $ep $ap) $rp) (+ (+ $en $an) $rn))))

!(bio-evidence-counts $snp $gene)
!(bio-evidence-counts rs9923732 ensg00000166455)
