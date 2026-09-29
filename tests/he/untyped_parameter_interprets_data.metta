; A value passed to a parameter that is not Atom-typed is interpreted at the
; call, as any argument is, so a program's equations for the value's
; constructors apply there, directly or through a variable.  An Atom-typed
; parameter keeps the value as it is.  A function whose result is declared
; Atom returns data that is not interpreted again.
(= (Num $x) (Rewritten $x))
(: mk (-> Atom))
(= (mk) (Num 1))
(= (show $v) $v)
(: show-atom (-> Atom Atom))
(= (show-atom $v) $v)
!(U0 (mk))
!(U1 (show (mk)))
!(U2 (let $v (mk) (show $v)))
!(U3 (let $v (mk) (show-atom $v)))
!(U4 (show-atom (mk)))
