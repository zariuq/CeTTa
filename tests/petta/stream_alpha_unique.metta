; `alpha-unique` answers the collected answers of its argument with
; alpha-equivalent repeats removed, first occurrences kept, as `unique` does
; with equal repeats.
!(test (=alpha (collapse (alpha-unique (superpose ($x $y A)))) ($a A)) True)
!(test (=alpha
    (collapse (alpha-unique (superpose ((f $x) (f $y) (g $z) (f $w)))))
    ((f $a) (g $b)))
  True)
(= (gen) $u)
(= (gen) A)
(= (gen) $v)
!(test (=alpha (collapse (alpha-unique (gen))) ($a A)) True)
!(test (collapse (unique (superpose (a b a)))) (a b))

; An equation whose body is `(empty)` has no answer, also when its argument
; is an unbound variable and a stream operation collects the call.
(= (none $p) (empty))
!(test (collapse (unique (none $q))) ())
(= (at-least-two Nil $p) (empty))
(= (at-least-two (Cons $k $n $t) $p)
   (if (<= 2 $n) (let $k $p $k) (empty)))
(= (at-least-two (Cons $k $n $t) $p) (at-least-two $t $p))
!(test (collapse (alpha-unique
    (at-least-two (Cons A 2 (Cons B 1 Nil)) $pat)))
  (A))
