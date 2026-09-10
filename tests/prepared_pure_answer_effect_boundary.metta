(= (answer-effect-step $value) (+ $value 1))

; The selected value arm remains observable when the other arm is choice
; zero.  A single-result accelerator must decline before construction and
; leave the canonical answer-effect evaluator authoritative.
!(if True (answer-effect-step 41) (empty))

; Selecting choice zero contributes no occurrence.
!(if False wrong (empty))

; Negative: the same head with an argument is not choice zero.  Its ordinary
; arity error must remain observable rather than being erased.
!(if False wrong (empty tag))
