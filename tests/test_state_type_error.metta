; After bind! resolves &s to a concrete state, HE reports the bad-arg error
; against the realized (State 0) value rather than the symbolic alias.
!(bind! &s (new-state 0))
!(change-state! &s True)
