; A call written inside constructor data is a pending computation.  Every
; demand that observes it, by pattern, by `let` or by printing, sees its
; value, whichever path the data took.  Here the same pending call reaches
; the interpreter's dispatch directly, through `to`, and through a function
; argument: all three must run it.
!(import! &self ../../../lib/prime/cbpv.metta)
(= (one-thunk) (Return (Const one)))
; through a function argument, forced in the body
!(cbpv:comp Nil (App (Lam (Force (Var 0))) (Thunk (one-thunk))) 0 Nil Nil Nil)
; through a function argument, returned and printed
!(cbpv:comp Nil (App (Lam (Return (Var 0))) (Thunk (one-thunk))) 0 Nil Nil Nil)
; through `to`
!(cbpv:comp Nil (Bind (Return (Thunk (one-thunk))) (Force (Var 0))) 0 Nil Nil Nil)
; directly
!(cbpv:comp Nil (Force (Thunk (one-thunk))) 0 Nil Nil Nil)
