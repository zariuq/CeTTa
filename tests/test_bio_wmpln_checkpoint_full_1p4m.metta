; True full-1.4M WM-PLN checkpoint over the raw-loader tier.
; Positive example:
; emit labeled benchmark data that says which workload ran and why proof-count
; can exceed hypothesis-count.
;
; Negative example:
; reuse the shorter post-split checkpoint row and call it the same thing as the
; older full raw-loader benchmark.

!(import! &self ./support/bio_wmpln_core_full_1p4m.metta)

!(wmpln-dataset-tier full-1p4m-raw-loader 1399997 tfbs-included yes)

!(let $hits
      (collapse
        (let (: $proof (seed-target rs10000544 $gene))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 2)
                 (: $proof (seed-target rs10000544 $gene)))
          (seed-hit rs10000544 $gene)))
   (seed-target-count (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (contextual-target rs10000544 $gene $tissue))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 3)
                 (: $proof (contextual-target rs10000544 $gene $tissue)))
          (context-hit rs10000544 $gene $tissue)))
   (contextual-target-count (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (disease-hyp rs10000544 $gene $disease $tissue))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 6)
                 (: $proof (disease-hyp rs10000544 $gene $disease $tissue)))
          (disease-proof-hit rs10000544 $gene $disease $tissue (proof-trace-route $proof))))
   (disease-proof-count (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                 (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
          (supported-drug-proof rs10000544 $gene $disease $drug $tissue
                                (proof-trace-route $proof)
                                (proof-trace-mode $proof)
                                (proof-trace-bound-gene $proof))))
   (supported-drug-proof-count (size-atom $hits)))

!(collapse
    (unique
      (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
               (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
        (supported-drug-hypothesis rs10000544 $gene $disease $drug $tissue))))

!(let $hits
      (collapse
        (unique
          (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
               (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                   (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
            (supported-drug-hypothesis rs10000544 $gene $disease $drug $tissue))))
   (supported-drug-hyp-count (size-atom $hits)))

!(supported-drug-count-contract
    proofs-are-distinct-proof-traces
    hypotheses-are-unique-gene-disease-drug-tissue-keys)
