!(import! &self ./support/bio_wmpln_core_1m.metta)

!(collapse
    (unique
      (let (: $proof (disease-hyp rs10000544 $gene $disease $tissue))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 6)
               (: $proof (disease-hyp rs10000544 $gene $disease $tissue)))
        (disease-route $gene $disease $tissue (proof-trace-route $proof)))))

!(collapse
    (unique
      (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
               (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
        (support-route $gene $disease $drug $tissue
                       (proof-trace-route $proof)
                       (proof-trace-mode $proof)
                       (proof-trace-bound-gene $proof))))))
