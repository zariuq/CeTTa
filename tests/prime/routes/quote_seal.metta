; A quotation is sealed code: substitution never enters it, whichever binder
; substitutes.  Inside @e a variable is the literal syntax $x; outside it the
; same variable is bound as usual.  So one variable means one thing in a body,
; and the answer is the same on every route.  The sealed calculus proves this
; of its terms: every route of a program ends in the same name
; (Mettapedia.TypeTheory.Calculi.SealedCode.name_unique; Astra's program,
; twoPath_outer_first and twoPath_inner_first), while sealing as written, where
; substitution enters the code, gives one program two names
; (asWritten_incoherent).  That reading stays available as the explicit
; quote-as-written profile (tests/prime/quote_profiles).

; ---- Every binder seals, as lam does.
!((lam $x @$x) 5)
!(let $x 5 @$x)
!(let $x 5 @(foo $x))
!(let* (($x 5) ($y 6)) @(pair $x $y))
!(case 5 (($x @$x)))
!(chain 5 $x @$x)
!(unify $y 5 @$y no)
(pair 7)
!(match &self (pair $x) @$x)
!(switch 5 (($x @$x)))
!(foldl-atom (1 2) () $acc $x (Pair $acc @$x))
!(map-atom (1 2) $x (case @$x (((quote $v) (get-metatype $v)))))

; ---- Outside the quotation the variable is bound.
!(let $x 5 (Pair $x @$x))
!(match &self (pair $x) (Pair $x @$x))

; ---- An equation's body is sealed too.  A rule's variables are renamed
; apart at each call, the quoted one with the rest, so the answer holds a
; fresh variable; observed here by its metatype.
(= (f $x) @$x)
(= (k $x) (Pair $x @$x))
(= (quoted-kind (quote $v)) (get-metatype $v))
!(quoted-kind (f 5))
!(quoted-kind (f (+ 1 2)))
!(case (k (+ 1 2)) (((Pair $v (quote $c)) (Pair $v (get-metatype $c)))))

; ---- The route does not matter: binding the computation by let, by call or
; by case gives the same quotation, never the computation's source and never
; its value.
!(let $x (+ 1 2) @$x)
!(case (+ 1 2) (($x @$x)))
!(quoted-kind (f (+ 1 2)))

; ---- A nested quotation is sealed at every depth.
!(let $x 5 @(outer @(inner $x)))

; ---- The drop *@$x of a quoted variable still refers to it, as *@x does
; for lam: running the code that names the variable reads the variable.
!((lam $x *@$x) 5)
!(let $x 5 *@$x)
!(case 5 (($x *@$x)))

; ---- Matching may still take quoted code apart: $-patterns inside a quoted
; pattern bind.
(= (parts (quote (f $y))) (Got $y))
!(case @(f a) (((quote (f $y)) (Got $y))))
!(let (quote (f $y)) @(f a) (Got $y))
!(unify @(f a) (quote (f $y)) (Got $y) no)
!(parts @(f a))

; ---- Code runs when activated, and only then.
!(let $c @(+ 1 2) (Pair $c *$c))
