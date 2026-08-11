; PeTTa state names address process-local values rather than exposing the
; shared evaluator's StateMonad carrier.
!(bind! state (new-state rest))
!(test (get-state state) rest)
!(change-state! state active)
!(test (get-state state) active)

; change-state! also creates a name, and its value argument is evaluated.
!(change-state! counter (+ 20 22))
!(test (get-state counter) 42)

; A resource-shaped state name denotes the location here, not the value
; already stored at that location.  Nested use must preserve that boundary.
!(change-state! &counter-resource 41)
!(test (let $value (get-state &counter-resource) (+ $value 1)) 42)

; Malformed bind! forms have no relational answer.
!(test (collapse (bind! invalid not-a-new-state)) ())

; Outside bind!'s recognized state pattern, new-state remains ordinary data
; while its known nested arguments still evaluate.
!(test (new-state (+ 1 2)) (new-state 3))
