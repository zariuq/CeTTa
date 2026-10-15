; context-space reads the interpreter's context, the space of the running
; evaluation; evalc and metta choose the space that looks up equations.
!(bind! &other (new-space))
(= (which) root)
!(add-atom &other (= (which) target))
!(add-atom &other (= (where) (context-space)))
; evalc looks up equations in its space ...
!(evalc (which) &other)
; ... and leaves the context as it is, read directly or through an equation.
!(let $s (evalc (context-space) &other) (== $s &self))
!(let $s (evalc (where) &other) (== $s &self))
!(== (context-space) &self)
