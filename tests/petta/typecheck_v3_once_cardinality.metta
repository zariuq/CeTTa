; `once` caps nondeterministic search at semideterminism: it may still return
; no answer, so it cannot strengthen a nondeterministic producer to det.

(: choose (-[nondet]-> Number Number))
(= (choose $value)
   (superpose ($value (+ $value 1))))

(: choose-once (-[semidet]-> Number Number))
(= (choose-once $value)
   (once (choose $value)))

(: choose-once-too-strong (-[det]-> Number Number))
(= (choose-once-too-strong $value)
   (once (choose $value)))

; On an already deterministic computation `once` is the identity grade.
(: bump (-[det]-> Number Number))
(= (bump $value) (+ $value 1))

(: bump-once (-[det]-> Number Number))
(= (bump-once $value)
   (once (bump $value)))
