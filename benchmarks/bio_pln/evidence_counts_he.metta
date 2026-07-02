; Portable PLN evidence-count benchmark, HE import wrapper.
; Run HE with tests/ on the include path so module kb_chr16_variants resolves.

!(import! &self kb_chr16_variants)

(= (bio-evidence-counts $snp $gene)
   (match &self
     (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (bio-evidence $snp $gene (+ (+ $ep $ap) $rp) (+ (+ $en $an) $rn))))

!(bio-evidence-counts $snp $gene)
!(bio-evidence-counts rs9923732 ensg00000166455)
