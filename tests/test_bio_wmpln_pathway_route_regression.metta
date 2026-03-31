; Regression for the restored WM-PLN pathway-mediated routes.
; Positive example:
; the fixed chainer reaches CRP and GLUL through the shared-pathway transfer
; route, and the restored support surface includes the CRP treatment path.
;
; Negative example:
; over-admitting those restored hits as if they were direct disease routes.

!(import! &self support/bio_wmpln_core_1m.metta)

!(assertEqualToResult
  (collapse
    (unique
      (let (: $proof (disease-hyp rs10000544 ensg00000090316 doid_12365 tissue-moderate))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 6)
               (: $proof (disease-hyp rs10000544 ensg00000090316 doid_12365 tissue-moderate)))
        (disease-route ensg00000090316 doid_12365 tissue-moderate
                       (proof-trace-route $proof)))))
  (((disease-route ensg00000090316 doid_12365 tissue-moderate route-pathway))))

!(assertEqualToResult
  (collapse
    (unique
      (let (: $proof (disease-hyp rs10000544 ensg00000145217 doid_3571 tissue-moderate))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 6)
               (: $proof (disease-hyp rs10000544 ensg00000145217 doid_3571 tissue-moderate)))
        (disease-route ensg00000145217 doid_3571 tissue-moderate
                       (proof-trace-route $proof)))))
  (((disease-route ensg00000145217 doid_3571 tissue-moderate route-pathway))))

!(assertEqualToResult
  (let $hits
       (collapse
         (unique
           (let (: $proof (disease-hyp rs10000544 ensg00000090316 doid_12365 tissue-moderate))
                (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 6)
                    (: $proof (disease-hyp rs10000544 ensg00000090316 doid_12365 tissue-moderate)))
             (if (== (proof-trace-route $proof) route-direct)
                 (disease-route ensg00000090316 doid_12365 tissue-moderate route-direct)
                 (superpose ())))))
    (crp-direct-route-count (size-atom $hits)))
  ((crp-direct-route-count 0)))

!(assertEqualToResult
  (collapse
    (unique
      (let (: $proof (supported-drug-hyp rs10000544 ensg00000090316 doid_12365 db00806 tissue-moderate))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
               (: $proof (supported-drug-hyp rs10000544 ensg00000090316 doid_12365 db00806 tissue-moderate)))
        (support-route ensg00000090316 doid_12365 db00806 tissue-moderate
                       (proof-trace-route $proof)
                       (proof-trace-mode $proof)
                       (proof-trace-bound-gene $proof)))))
  (((support-route ensg00000090316 doid_12365 db00806 tissue-moderate
                   route-pathway mode-treat ensg00000138735))))
