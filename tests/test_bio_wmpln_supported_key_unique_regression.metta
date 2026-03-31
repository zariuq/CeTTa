; Regression for the post-fix WM-PLN hypothesis dedup surface.
; Positive example: proof-level route competition can yield many supported-drug
; proofs, while unique over the ground supported-key tuples collapses them to
; the true hypothesis set.
;
; Negative example: treating every proof path as a distinct hypothesis even
; after projecting away the proof metadata.

!(import! &self support/bio_wmpln_core_1m.metta)

!(assertEqualToResult
  (let $hits
       (collapse
         (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
              (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                  (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
           (supported-drug-proof rs10000544 $gene $disease $drug $tissue
                                 (proof-trace-route $proof)
                                 (proof-trace-mode $proof)
                                 (proof-trace-bound-gene $proof))))
    (supported-drug-proof-count (size-atom $hits)))
  ((supported-drug-proof-count 34)))

!(assertEqualToResult
  (let $hits
       (collapse
         (unique
           (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
                (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                    (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
             (supported-key $gene $disease $drug $tissue))))
    (supported-key-unique-count (size-atom $hits)))
  ((supported-key-unique-count 8)))
