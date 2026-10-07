; Taking code apart with its binders.  A quoted pattern meets quoted code
; binder by binder: a binder position matches the code's binder by
; identity, never by spelling.  A part under binders of the code that it
; mentions comes as contextual code, `(quote M (k ...))`, never as an open
; term; `lift let` fills its binders and `*` opens it as their function.
; Where the continuation quotes the hole again, the part's syntax is put
; back.  In Prime `==` compares bound names by position, as these matches
; do.
!(import! &self template)

; ---- Binders by identity.
!(row alpha-variant)
!(let (quote (lam w w)) (quote (lam z z)) alpha-variant)
!(row unify-alpha-variant)
!(unify @(lam z z) (quote (lam w w)) alpha-variant none)
!(row case-alpha-variant)
!(case @(lam z z) (((quote (lam w z)) wrong) ((quote (lam w w)) right)))
; The pattern's z is free; the code's is bound.
!(row free-is-not-bound)
!(let (quote (lam w z)) (quote (lam z z)) wrong)
; A pattern variable at a binder position takes the code's binder.
!(row binder-variable)
!(let (quote (lam $x (f $x))) (quote (lam z (f z))) (Got $x))
!(row binder-variable-unused)
!(let (quote (lam $x (f $x))) (quote (lam z (f c))) wrong)

; ---- A part under the binder is contextual code; lift let opens it.
!(row contextual)
!(let (quote (lam z $b)) (quote (lam z (f z))) (Got $b))
!(row contextual-lift-let)
!(let (quote (lam z $b)) (quote (lam z (f z))) (lift let z @7 $b))
!(row contextual-from-variable)
!(let $code @(lam y (f y)) (unify $code (quote (lam $x $body)) (lift let $x @7 $body) none))
!(row contextual-dollar-binder)
!(unify @(lam $p (g $p $p)) (quote (lam $x $body)) (Got $x $body) none)
!(row contextual-nested)
!(let (quote (lam $x $b)) (quote (lam a (lam b a))) (Got $b))
!(row contextual-two-binders)
!(let (quote (lam $x (lam $y $b))) (quote (lam a (lam b (f a b)))) (Got $b (lift let ($x $y) (@1 @2) $b)))
!(row contextual-one-filled)
!(let (quote (lam $x (lam $y $b))) (quote (lam a (lam b (f a b)))) (lift let $y @2 $b))
!(row contextual-opened-by-star)
!(let (quote (lam z $b)) (quote (lam z (f z))) (*$b 9))
; Quoting the hole again puts the part back under binders of its own.
!(row spliced-back)
!(unify @(lam y (f y)) (quote (lam $x $body)) (Pair @$body @(lam $x (g $body))) none)
; A part that mentions no binder is the part itself.
!(row closed-part)
!(let (quote (lam z $b)) (quote (lam z (f c))) (Got $b))
!(row closed-lambda-part)
!(let (quote (f $a)) (quote (f (lam z z))) (Got $a))

; ---- Held syntax obeys the rule quotations do: lift let fills its slot.
(: open-held (-> (Data Atom) (Data Atom) (Data Atom)))
(= (open-held (lam $x $body) $v) (lift let $x $v $body))
!(row held)
!(let $h (lam $y (g $y $y)) (Pair (open-held $h 0) (open-held $h 1)))

; ---- A template passed as a value is code, opened by lift let.
(= (each-twice $xs $key $code) (template:map $xs $key $code))
!(row template-forwarded)
!(each-twice (1 2) x @(Pair x x))
!(row template-fold)
!(template:foldl (1 2 3) 0 acc x @(+ acc x))

; ---- `==` compares bound names by position.
!(row eq-binders)
!(== (lam w w) (lam z z))
!(row eq-own-names)
!(== (lam z (let $t z $t)) (lam z (let $u z $u)))
!(row eq-code)
!(== @(lam w w) @(lam z z))
!(row eq-free-is-not-bound)
!(== (lam w z) (lam z z))
!(row eq-free-symbol)
!(== (lam z (f z a)) (lam z (f z b)))
!(row eq-binder-order)
!(== (lam x (lam y (f x y))) (lam a (lam b (f b a))))
!(row eq-quotation-sealed)
!(== (lam x @x) (lam y @y))
; Contextual code binds the names of its list, as a lambda binds its
; parameters.
!(row eq-contextual)
!(let (quote (lam z $b)) (quote (lam z (f z))) (let (quote (lam w $c)) (quote (lam w (f w))) (== $b $c)))
!(row eq-contextual-binder-order)
!(let (quote (lam a (lam b $p))) (quote (lam a (lam b (f a b)))) (let (quote (lam c (lam d $q))) (quote (lam c (lam d (f d c)))) (== $p $q)))
; Captured names are not bound by the lambda: they are compared as the
; runtime has them, the same name, two names, or the value bound.
!(row eq-captured-same)
!(let $f (lam z (Pair z $n)) (let $g (lam w (Pair w $n)) (Pair (== $f $g) $n)))
!(row eq-captured-different)
!(let $f (lam z (Pair z $n)) (let $g (lam w (Pair w $m)) (Triple (== $f $g) $n $m)))
!(row eq-captured-bound)
!(let $f (lam z (Pair z $n)) (let $g (lam w (Pair w $m)) (let $n 5 (let $m 5 (== $f $g)))))
