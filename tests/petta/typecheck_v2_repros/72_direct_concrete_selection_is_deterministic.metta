; A direct source expression exposes only its outer nonempty-list shape to
; typecheck-v2 selection.  Its nested constructor spelling is not a proof of
; the value that reaches the callee, so this deterministic promise remains
; unproven.  The Bool-selector oracle case is the positive atomic control.

(= (decode-form (left $value)) 1)
(= (decode-form (right $value)) 2)

(: direct-concrete-selector (-[det]-> Number))
(= (direct-concrete-selector)
   (decode-form (left 0)))
