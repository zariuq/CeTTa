(= (slot-relation $value) $value)
(= (scalar-segment-seed) 2)

; A let*-bound value feeds a bounded deterministic arithmetic tree.  The
; second occurrence keeps the outer relation genuinely nondeterministic.
(= (nested-scalar-argument $value)
   (let* (($seed (scalar-segment-seed)))
     (slot-relation (+ (- $value 1) (* $seed 1)))))
(= (nested-scalar-argument $value) (empty))

; Division is deliberately outside the admitted scalar segment and must use
; the exact canonical path.
(= (division-scalar-argument $value)
   (let* (($denominator (scalar-segment-seed)))
     (slot-relation (/ $value $denominator))))
(= (division-scalar-argument $value) (empty))

; An open relation call remains owned by the ordinary branch hub.  Equal
; authored occurrences therefore remain two equal results.
(= (two-answer $value) $value)
(= (two-answer $value) $value)
(= (open-scalar-boundary $value)
   (let* (($seed (scalar-segment-seed)))
     (slot-relation (two-answer $seed))))
(= (open-scalar-boundary $value) (empty))

; An effect occurs before the candidate-local scalar segment and is neither
; duplicated nor erased.
!(bind! scalar-segment-state (new-state 0))
(= (effect-source $value)
   (let $_
     (change-state! scalar-segment-state
       (+ (get-state scalar-segment-state) 1))
     $value))
(= (effect-scalar-boundary $value)
   (let* (($candidate (effect-source $value)))
     (slot-relation (+ $candidate 1))))
(= (effect-scalar-boundary $value) (empty))

!(nested-scalar-argument 4)
!(division-scalar-argument 4)
!(open-scalar-boundary seed)
!(effect-scalar-boundary 4)
!(get-state scalar-segment-state)
