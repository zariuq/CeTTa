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

; A source variable is a value occurrence.  If its outer binding is
; expression-shaped data, match still returns that data without executing its
; head, so the same existence observation applies.
(= (expression-existence $Space $Atom)
   (== () (collapse (once (match $Space $Atom $Atom)))))
!(expression-existence &existence-space (key a))
!(expression-existence &existence-space (key missing))
