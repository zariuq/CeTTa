; `alpha-unique` answers each alpha-equivalence class of its argument's
; answers once, as `unique` answers each equal answer once.  The answers are
; a bag, compared here in sorted order.
!(test (=alpha (let $answers (collapse (alpha-unique (superpose ($x $y A))))
                 (sort-atom $answers))
               ($a A))
  True)
!(test (=alpha
    (let $answers (collapse (alpha-unique (superpose ((f $x) (f $y) (g $z) (f $w)))))
      (sort-atom $answers))
    ((f $a) (g $b)))
  True)
(= (gen) $u)
(= (gen) A)
(= (gen) $v)
!(test (=alpha (let $answers (collapse (alpha-unique (gen))) (sort-atom $answers))
               ($a A))
  True)
!(test (let $answers (collapse (unique (superpose (a b a)))) (sort-atom $answers)) (a b))

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
