!(import! &self pat)
(evidence sensor-a (edge alice bob))
(evidence sensor-a (edge alice bob))
(evidence sensor-b (edge bob carol))

; Equal data can be distinct observations. Keep the supporting occurrences
; until an explicit evidence policy decides how they should be combined.
!(collapse
   (let (pat:hit $rows $w)
        (pat:query &self match%
          (, (evidence $first (edge $x $y))
             (evidence $second (edge $y $z))))
     (derivation (pat:apply $w (reachable $x $z)) $rows)))

; A missing premise does not create an evidence certificate.
!(collapse (pat:query &self match% (evidence $source (edge carol dana))))
