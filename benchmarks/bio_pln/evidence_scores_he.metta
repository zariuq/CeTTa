; Portable PLN evidence-score benchmark, HE import wrapper.
; Run HE with tests/ on the include path so module bio_kb_chr16 resolves.

!(import! &self bio_kb_chr16)

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
