; Post-split WM-PLN checkpoint regression.
; Positive example:
; save the cleaned-up rule-space benchmark shape before widening the disease
; and drug layers.
;
; Negative example:
; widen the biology and then lose track of whether a behavior change came from
; the data expansion or from a regression in the generic chainer.

!(import! &self ./support/bio_wmpln_core_1m.metta)

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

(= (checkpoint-supported-drug-groups)
   (fold-by-key
     (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
          (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
              (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
       (pair (supported-key $gene $disease $drug $tissue) (proof-stv $proof)))
     (stv 0.0 0.0)
     $acc
     $item
     (let (pair $key $next) $item $key)
     (let (pair $key $next) $item
       (eval (revise-stv-step $acc $next)))))

!(let $groups
      (collapse (checkpoint-supported-drug-groups))
   (supported-drug-hyp-count (size-atom $groups)))

!(collapse
    (let ($key $revised)
         (checkpoint-supported-drug-groups)
      (let (supported-key $gene $disease $drug $tissue) $key
        (let $score (wm-posterior-mean $revised)
          (if (> $score 0.30)
              (scored-supported-drug rs10000544 $gene $disease $drug $tissue
                                     $revised
                                     $score)
              (superpose ()))))))

!(threshold-count
    (fold
      (checkpoint-supported-drug-groups)
      0
      $count
      $group
      (eval
        (let ($key $revised) $group
          (let $score (wm-posterior-mean $revised)
            (if (> $score 0.30)
                (+ $count 1)
                $count))))))
