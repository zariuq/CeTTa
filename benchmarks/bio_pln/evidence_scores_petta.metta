; Portable PLN evidence-score benchmark, PeTTa import wrapper.
; PeTTa is a distinct MeTTa dialect, so the benchmark keeps the arithmetic
; local instead of depending on CeTTa/HE library import semantics.

!(import! &self ../data/kb_chr16_variants.metta)

(= (evidence-strength $pos $neg)
   (/ (* 1.0 $pos) (+ $pos $neg)))

(= (evidence-confidence $pos $neg)
   (/ (* 1.0 (+ $pos $neg)) (+ (+ $pos $neg) 1)))

(= (bio-evidence-score $snp $gene)
   (match &self
     (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (let* (($pos (+ (+ $ep $ap) $rp))
            ($neg (+ (+ $en $an) $rn)))
       (bio-score $snp $gene
                  (evidence-strength $pos $neg)
                  (evidence-confidence $pos $neg)))))

!(bio-evidence-score $snp $gene)
!(bio-evidence-score rs9923732 ensg00000166455)
