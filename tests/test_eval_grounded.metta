; eval on grounded vs pure expressions
; Ref: minimal-metta.md lines 75-101

; eval on pure MeTTa — equation lookup
(= (f a) b)
!(assertEqual (eval (f a)) b)

; eval on grounded — calls native function
!(assertEqual (eval (+ 1 2)) 3)

; eval on non-reducible — returns unchanged
!(assertEqualToResult (eval (no-equation x)) ((no-equation x)))
