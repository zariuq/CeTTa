; A direct slot can hold a structured value whose children acquire bindings
; after clause entry. Reusing the slot must retain that outer substitution.
(= (field-bind a) ready)
(= (field-pair (box a) (box a)) equal)
(= (field-pair (box b) (box b)) wrong)
(= (field-pair (box a) (box a)) equal)
(= (field-through $value $link)
   (let $_ (field-bind $link) (field-pair $value $value)))
(= (field-through $value $link) (empty))
!(field-through (box $x) $x)

; Aliased fields cannot independently choose incompatible substitutions.
(= (field-different (box a) (box b)) different)
(= (field-different (box c) (box d)) different)
(= (field-alias $value) (field-different $value $value))
(= (field-alias $value) (empty))
!(field-alias (box $x))

; Authored structure and a direct slot denote the same partially bound term.
; The caller and callee deliberately reuse source variable spellings.
(= (field-mixed $x $value)
   (let $_ (field-bind $x) (field-pair (box $x) $value)))
(= (field-mixed $x $value) (empty))
!(field-mixed $x (box $x))
!(field-mixed $x (box b))

; Scalar results and authored fields carry different environments. The
; singleton matcher must propagate a binding from one field to the next.
(= (field-scalar 2 (box $same) (box $same)) same)
(= (field-scalar-call $n $x $y)
   (field-scalar (+ $n 1) (box $x) (box $y)))
(= (field-scalar-call $n $x $y) (empty))
!(field-scalar-call 1 a a)
!(field-scalar-call 1 a b)
!(let $_ (field-scalar-call 1 $x a) $x)

; Entry variables remain open across alternatives. Freezing the query after
; matching the first row would erase the b row or duplicate the a row.
(= (field-choice 2 (box a) (box a)) a)
(= (field-choice 2 (box b) (box b)) b)
(= (field-choice 2 (box a) (box a)) a)
(= (field-choice-call $n $x)
   (field-choice (+ $n 1) (box $x) (box $x)))
(= (field-choice-call $n $x) (empty))
!(collapse (field-choice-call 1 $x))

; Arity is independent of the inline storage capacity and the evaluated
; field need not occupy the first argument position.
(= (field-wide a b c d e f g h 2 (box a)) wide)
(= (field-wide-call $n $x)
   (field-wide a b c d e f g h (+ $n 1) (box $x)))
(= (field-wide-call $n $x) (empty))
!(field-wide-call 1 a)
!(field-wide-call 1 b)
