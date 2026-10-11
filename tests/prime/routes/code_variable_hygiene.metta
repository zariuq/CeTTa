; A variable of the code never takes a pattern binder's bare name outside the
; code (stage 5, item 4).  The hole rule holds in both directions: wherever
; a match binds a variable to a part under binders of the code, on either
; side of it, the value is contextual code carrying its binder list.  Read
; inside the code where the variable stands, it is the part's syntax, so the
; code still denotes the binder there.

; ---- A variable of the value's code meets a pattern binder.
!(row escaped)
!(let (quote (lam w w)) (quote (lam z $hole)) (Got escaped $hole))
!(row escaped-outside)
!(let $r (let (quote (lam w w)) (quote (lam z $hole)) escaped) (Pair $r $hole))
!(row filled-by-lift-let)
!(let (quote (lam w (f w))) (quote (lam z $hole)) (lift let z @7 $hole))
!(row unify-direction)
!(unify (quote (lam z $hole)) (quote (lam w (g w w))) (Got $hole) none)

; ---- Read inside the code where it stands, the variable is the binder
; again: the code still denotes the identity, and runs as it.
!(row read-inside-code)
!(unify (quote (lam z $hole)) (quote (lam w w))
   (Pair (quote (lam z $hole)) $hole) none)
!(row read-inside-code-runs)
!(unify (quote (lam z $hole)) (quote (lam w (f w)))
   (*(quote (lam z $hole)) 7) none)

; ---- A pattern's hole meets the value's part: read where the hole stands,
; in the pattern's code, the part refers to the pattern's binders, so the
; value's binder never enters the pattern's code by its bare name.  A pattern
; variable at a binder position takes the code's binder itself.
!(row pattern-hole-names-by-the-pattern)
!(case (quote (lam w w)) (((quote (lam z $hole)) $hole)))
!(row let-pattern-hole-names-by-the-pattern)
!(let (quote (lam z $hole)) (quote (lam w (g w w))) (Got $hole))
!(row pattern-hole-read-inside-code)
!(case (quote (lam w w)) (((quote (lam z $hole)) (Pair (quote (lam z $hole)) $hole))))
!(row pattern-hole-read-inside-code-runs)
!(case (quote (lam w (f w))) (((quote (lam z $hole)) (*(quote (lam z $hole)) 7))))
!(row pattern-binder-variable-takes-the-code-binder)
!(case (quote (lam w (f w))) (((quote (lam $x $hole)) (Got $x $hole))))

; ---- Negative controls: a part not under the code's binders is itself, and
; a pattern variable meeting the code's part takes it as before.
!(row part-not-under-binders)
!(let (quote (f a)) (quote (f $v)) (Got $v))
!(row closed-part-under-binder)
!(let (quote (lam w c)) (quote (lam z $hole)) (Got $hole))
!(row pattern-hole)
!(let (quote (lam z $b)) (quote (lam z (f z))) (Got $b))
