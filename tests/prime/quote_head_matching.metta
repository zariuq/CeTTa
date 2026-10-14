; An equation head that takes a quotation apart matches the quoted term as
; written.  Nothing inside the quotation runs before matching: a redex or a
; call inside it is syntax.  A call no head matches keeps its argument as
; written.
(= (∧ $p $q) (if $p $q False))

; A quoted redex is matched as a redex, by an equation head and by let.  Its
; body mentions the lambda's binder: both hand it over as contextual code,
; with that binder.
(= (show (quote ((lam $x $b) $c))) (got $x $b $c))
!(show @((lam $y (f $y)) 0))
!(let (quote ((lam $x $b) $c)) @((lam $y (f $y)) 0) (got $x $b $c))

; A quoted call is matched as the call, open or ground; unify agrees.
(= (parts (quote (∧ $p $q))) (both @$p @$q))
!(parts @(∧ (F $x) (G $x)))
!(parts @(∧ (F 0) (G 0)))
!(unify @(∧ (F $x) (G $x)) (quote (∧ $p $q)) (both @$p @$q) no)

; No head matches: the call is data, with its quotation as written.
!(show @(∧ (F 0) (G 0)))

; A quotation inside a constructor is still matched as written.
(= (inner (box (quote (∧ $p $q)))) (split @$p @$q))
!(inner (box @(∧ (F 0) (G 0))))

; Outside a quotation, a field the head inspects is still evaluated.
(= (two) (s (s z)))
(= (pred-of (box (s $n))) $n)
!(pred-of (box (two)))
