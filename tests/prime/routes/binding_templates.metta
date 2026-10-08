; Implicit template ownership and captured-name refinement under rule M.
(scope:profile mercury-implicit per-call reference)

; Template activation (rule 3 of the binding structure), under the declared
; scope profile, mercury-implicit per-call reference.  A lambda body, and the
; template of map-atom or foldl-atom, owns the store names written directly
; in it that no enclosing scope writes; each activation (an application, one
; use per element) copies them.  A name written outside is captured: it is
; the outside slot, shared even while unbound.  An authored lambda is a
; value: nothing under its binder is evaluated before an application.

(= (mk) (lam z (let $y z (g $y))))
(= (mk-sum) (lam z (+ 1 2)))
(= (fe $z) (let $y $z (g $y)))
(= (wrapped) (W (lam z (+ 1 2))))

; ---- Lambda templates.
!(row B1)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
!(row B2)
!(Pair ((lam z (let $y z (g $y))) 1) ((lam z (let $y z (g $y))) 2))
!(row B3)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 1)))
!(row B4)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) $y))
!(row B5)
!(let $f (lam z (let $y z (g $y))) (Triple ($f 1) ($f 2) $y))
!(row B6)
!(let $f (lam z (Pair z $y)) (let $y 5 (Pair ($f 1) ($f 2))))
!(row B7)
!(let $y 5 (let $f (lam z (Pair z $y)) (Pair ($f 1) ($f 2))))
!(row B8)
!((lam z (Pair (let $y z $y) ((lam w (let $y w $y)) 3))) 3)
!(row B9)
!((lam z (Pair (let $y z $y) ((lam w (let $y w $y)) 3))) 4)
!(row B10)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
!(row B11)
!(let $f (mk) (Pair ($f 1) ($f 2)))
!(row B12)
!(Pair (fe 1) (fe 2))
!(row B13)
!(let $f (lam z ((lam y (g y)) z)) (Pair ($f 1) ($f 2)))

; ---- map-atom templates.
!(row C1)
!(map-atom (1 2) $e (let $y $e (g $y)))
!(row C2)
!(let $r (map-atom (1 2) $e (Pair $e $y)) (let $y 5 $r))
!(row C3)
!(let $y 5 (map-atom (1 2) $e (Pair $e $y)))
!(row C4)
!(Pair (map-atom (1 2) $e (let $y $e (g $y))) $y)
!(row C5)
!(Pair (map-atom (1 2) $e $e) $e)

; ---- foldl-atom templates, the same five.
!(row F1)
!(foldl-atom (1 2) () $acc $e (let $y $e (cons-atom (g $y) $acc)))
!(row F2)
!(let $r (foldl-atom (1 2) () $acc $e (cons-atom (Pair $e $y) $acc)) (let $y 5 $r))
!(row F3)
!(let $y 5 (foldl-atom (1 2) () $acc $e (cons-atom (Pair $e $y) $acc)))
!(row F4)
!(Pair (foldl-atom (1 2) () $acc $e (let $y $e (cons-atom (g $y) $acc))) $y)
!(row F5)
!(Pair (foldl-atom (1 2) 0 $acc $e (+ $acc $e)) $e)

; ---- A returned lambda is a value on every route: its body is not
; evaluated under the binder, with own names or without.
!(row R0)
!(mk)
!(row R1)
!(let $m (mk) (let $b (index-atom $m 2) (car-atom $b)))
!(row R2)
!(mk-sum)
!(row R3)
!(wrapped)
!(row R4)
!(let $f (mk-sum) ($f 0))
