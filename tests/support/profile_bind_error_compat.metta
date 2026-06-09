; he-compat propagates generated initializer errors from bind!, matching Rust HE.
; Literal Error atoms remain data and can still be bound.

!(bind! &literal-error (Error boom reason))
!(get-type &literal-error)
!(bind! &bad-space (new-space queue))
