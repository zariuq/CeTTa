; Prime syntax relations.
;
; The ordinary parse and repr operations are Prime's sole reflective reader
; and printer. On reader-admissible source forms S:
;   parse(repr(S)) =alpha S
; repr preserves repeated-variable sharing. Distinct internal variable
; identities with the same presentation are freshened bijectively on output.
;
; syn:syntactic-equal is deliberately identity-sensitive. It is the positive
; spelling of noreduce-eq, not alpha-equivalence; =alpha remains the separate
; relation for syntax modulo variable renaming.

(: syn:syntactic-equal (-> Atom Atom Bool))
