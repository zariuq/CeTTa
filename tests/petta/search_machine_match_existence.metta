!(bind! &existence-space (new-space))
!(add-atom &existence-space (key a))

; A constant-template once/match has one visible answer exactly when the
; ground row exists.  Both equality orientations denote the same observer.
!(== () (collapse (once (match &existence-space (key a) hit))))
!(== () (collapse (once (match &existence-space (key missing) hit))))
!(== (collapse (once (match &existence-space (key a) hit))) ())

; Multiplicity does not affect existence.
!(add-atom &existence-space (key a))
!(== () (collapse (once (match &existence-space (key a) hit))))
