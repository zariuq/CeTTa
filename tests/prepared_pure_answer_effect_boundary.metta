(= (answer-effect-step $value) (+ $value 1))

; The selected value arm remains observable when the other arm is choice
; zero. Acceleration preserves the canonical answer occurrences, including
; the empty branch; specialization refusal keeps canonical interpretation.
!(if True (answer-effect-step 41) (empty))

; Selecting choice zero contributes no occurrence.
!(if False wrong (empty))

; Negative: the same head with an argument is not choice zero. Its dialect
; result must remain observable rather than being erased.
!(if False wrong (empty tag))
