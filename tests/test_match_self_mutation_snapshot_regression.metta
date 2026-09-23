; A match result is a snapshot: mutating the matched space while consuming
; the result must not make the same match chase newly-added atoms.

!(add-atom &self (snapshot-seed 0))

!(case (match &self (snapshot-seed $x) $x)
       (($x (add-atom &self (snapshot-seed (+ $x 1))))))

!(assertEqual
  (collapse (match &self (snapshot-seed $x) $x))
  (0 1))

; Consuming one equation can recursively select through the same compiled
; decision. Its scratch must not replace the outer call's remaining choices.
(= (snapshot-choice outer) (snapshot-choice inner))
(= (snapshot-choice outer) second)
(= (snapshot-choice outer) third)
(= (snapshot-choice inner) nested-first)
(= (snapshot-choice inner) nested-second)
!(assertEqual
  (collapse (snapshot-choice outer))
  (nested-first nested-second second third))
