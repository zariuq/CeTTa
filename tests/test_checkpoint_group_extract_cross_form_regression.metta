; Regression for cross-top-level checkpoint extraction corruption.
; Positive example:
; after materializing the raw supported-key groups, a later top-level form
; should still see the repaired extracted grouping count as 8.
;
; Negative example:
; a shared copy into the ephemeral eval arena used to leave dangling
; structure behind, so the second top-level form miscounted 4 groups.

!(import! &self ./support/bio_wmpln_core_1m.metta)

(= (checkpoint-supported-key-groups)
   (fold-by-key
     (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
          (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
              (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
       (supported-key $gene $disease $drug $tissue))
     1
     $acc
     $item
     $item
     $acc))

(= (checkpoint-extracted-key-groups)
   (fold-by-key
     (let $item
          (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
               (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                   (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
            (pair (supported-key $gene $disease $drug $tissue) (proof-stv $proof)))
       (let (pair $key $next) $item $key))
     1
     $acc
     $item
     $item
     $acc))

!(assertEqual
  (let $groups
       (collapse (checkpoint-supported-key-groups))
    (raw-key-count (size-atom $groups)))
  (raw-key-count 8))

!(assertEqual
  (let $groups
       (collapse (checkpoint-extracted-key-groups))
    (extracted-key-count (size-atom $groups)))
  (extracted-key-count 8))

!(assertEqual
  (let $extracted
       (collapse (checkpoint-extracted-key-groups))
    (extracted-key-count (size-atom $extracted)))
  (extracted-key-count 8))
