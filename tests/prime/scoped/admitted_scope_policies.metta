(hol:profile base)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math N (u 0) (: z N) (: s (-> N N)))
!(hol:define &math add (-> N N N)
   (= (add $x z) $x)
   (= (add $x (s $y)) (s (add $x $y))))
!(eval (add (s z) z) (admitted &math strict))
!(eval (add (s z) z) (admitted &math isolated))
!(car-atom (try (eval (add (s z) z) (admitted &math strict 1))))

; An unrelated ordinary helper does not conflict with an admitted head.
!(add-atom &math (= (helper $x) $x))
!(eval (add (s z) z) (admitted &math strict 10000))

; Isolation and strict editing are explicit alternatives over the same data.
!(add-atom &math (= (add $x z) z))
!(eval (add (s z) z) (admitted &math isolated))
!(let (Undetermined (scope-conflict $head $rule))
   (try (eval (add (s z) z) (admitted &math strict)))
   (ConflictAt $head))
!(eval (add (s z) z) &math)
!(remove-atom &math (= (add $x z) z))
!(eval (add (s z) z) (admitted &math strict))

; Constructors participate in the strict contract even without delta rules.
!(add-atom &math (= (s $x) z))
!(let (Undetermined (scope-conflict $head $rule))
   (try (eval (add z (s z)) (admitted &math strict)))
   (ConflictAt $head))
!(eval (add z (s z)) (admitted &math isolated))
!(car-atom (try (eval z (admitted &math unknown-policy))))
