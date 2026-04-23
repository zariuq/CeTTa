; Top-level PeTTa checkpoint validation for the post-split WM-PLN workload.
; Keep the checkpoint on the shipped flat-loader seam rather than repeating
; many imports into the same named space.

!(import! &self ./support/bio_wmpln_core_1m_petta_flat.metta)

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

!(let $hits
      (collapse
        (unique
          (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
               (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                   (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
            (supported-drug-hypothesis rs10000544 $gene $disease $drug $tissue))))
   (supported-drug-hyp-count (size-atom $hits)))
