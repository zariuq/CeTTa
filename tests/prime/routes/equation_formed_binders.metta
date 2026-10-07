; A binder formed by an equation call is sealed as a written one is (stage
; 5, item 1).  The call fills the head variable of the equation's body with
; a binder's name; the body so formed is sealed where it is formed, by the
; application of bindings every route shares: a mention, in quoted code, of
; a name its pattern binds is the quotation's own, so the pattern's value
; never enters the quotation.  The call answers as the same equation written
; with the binder does, the seal's variable an instance of the call as the
; written equation's variables are.
(= (form $h) ($h $x 5 (quote $x)))
(= (written) (let $x 5 (quote $x)))
(= (form-case $h) ($h 5 (($x (quote $x)))))
(= (form-nested $h) (Pair ($h $x 5 (quote $x)) done))
(= (form-twice $h) ($h $x 5 ($h $y 6 (quote (Pair $x $y)))))

; ---- The formed binder answers as the written one.
!(row written)
!(written)
!(row equation-formed-let)
!(form let)
!(row equation-formed-case)
!(form-case case)
!(row equation-formed-nested)
!(form-nested let)
!(row equation-formed-twice)
!(form-twice let)
; ---- Negative controls.  A head that binds nothing seals nothing; a
; crossing set on the quotation lets the name in; a quotation built at run
; time in the body shares the name.
(= (form-data $h) ($h $x (quote $x)))
(= (form-crossing $h) ($h $x 5 (quote $x){$x}))
(= (form-built $h) ($h $x 5 (cons-atom quote ($x))))
!(row not-a-binder)
!(form-data f)
!(row crossing-set)
!(form-crossing let)
!(row quotation-built-in-body)
!(form-built let)
