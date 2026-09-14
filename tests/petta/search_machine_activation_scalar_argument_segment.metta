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

; The same scalar interpretation applies at the result boundary. No outer
; relation consumer is required, and the input may come through a binding.
(= (scalar-result $x $y) (+ (- $x 1) (* $y 2)))
(= (scalar-result $x $y) (empty))
(= (scalar-result-frame $pair)
   (let* ((($x $y) $pair)) (+ (* $x 2) (- $y 1))))
(= (scalar-result-frame $pair) (empty))
(= (scalar-result-truth $x) (== (+ $x 1) 5))
(= (scalar-result-truth $x) (empty))
(= (scalar-result-division $x) (+ (/ $x 2) (* $x 3)))
(= (scalar-result-division $x) (empty))
(= (scalar-result-choice) 2)
(= (scalar-result-choice) 2)
(= (scalar-result-open) (+ (scalar-result-choice) (* 3 4)))
(= (scalar-result-open) (empty))
(= (scalar-result-effects $x) (+ (effect-source $x) (effect-source $x)))
(= (scalar-result-effects $x) (empty))

!(scalar-result 9 3)
!(scalar-result-frame (3 9))
!(scalar-result-truth 4)
!(scalar-result-truth 5)
!(scalar-result-division 5)
!(scalar-result-open)
!(scalar-result-effects 4)
!(get-state scalar-segment-state)

; Overflow, unresolved variables and nonnumeric values retain the ordinary
; numeric and error semantics. Effects and duplicate answers remain visible.
!(scalar-result 9223372036854775807 2)
!(scalar-result 9 9223372036854775807)
!(scalar-result $free 3)
