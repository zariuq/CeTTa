; Positive: a relational LHS occurrence constrains the caller value while the
; RHS retains one activation-local identity for repeated variables.
(= (identity $x) $x)
(= (relational-view (identity $x)) (pair $x $x))

!(relational-view (identity value))

; Negative/positive arity pair: a shorter call must decline the larger
; equation without reading or scheduling a nonexistent argument.  The exact
; equation still precedes currying/overapplication behavior.
(= (overloaded one) exact)
(= (overloaded one two) larger)

!(overloaded one)
!(overloaded one two)
