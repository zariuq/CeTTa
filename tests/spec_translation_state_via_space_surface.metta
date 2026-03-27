; Explicit state-via-space witness.
;
; Positive example:
; - this is the kind of space-backed state encoding that can support the
;   future theorem bridge for state atoms.
;
; Negative example:
; - it is not the builtin new-state/get-state/change-state! surface.

!(bind! &statebox (new-space))
!(add-atom &statebox (state-value 0))
!(match &statebox (state-value $x) $x)
!(remove-atom &statebox (state-value 0))
!(add-atom &statebox (state-value 1))
!(match &statebox (state-value $x) $x)
