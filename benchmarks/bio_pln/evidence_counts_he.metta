; Portable PLN evidence-count benchmark, HE import wrapper.
; Run HE with tests/ on the include path so module bio_kb_chr16 resolves.

!(import! &self bio_kb_chr16)

(= (bio-evidence-counts $snp $gene)
   (match &self
     (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (bio-evidence $snp $gene (+ (+ $ep $ap) $rp) (+ (+ $en $an) $rn))))

!(bio-evidence-counts $snp $gene)
!(bio-evidence-counts rs9923732 ensg00000166455)
