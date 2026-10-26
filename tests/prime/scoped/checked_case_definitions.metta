(hol:profile base)
!(hol:inductive &self N (u 0) (: z N) (: s (-> N N)))
!(hol:inductive &self B (u 0) (: yes B) (: no B))

; A comparison can choose its second argument as the recursor domain.
!(hol:define &self ge (-> N N B)
   (= (ge $m z) yes)
   (= (ge z (s $n)) no)
   (= (ge (s $m) (s $n)) (ge $m $n)))
!(eval (ge (s (s z)) (s z)) (admitted &self))
!(eval (ge (s z) (s (s z))) (admitted &self))
!(ge (s (s z)) (s z))

; All source equations and the checked recursor are retained for inspection.
!(match &self (hol:known ge $source definition
    (definition-evidence $rules (checked-recursor $position $body) $basis) $deps $revision $signature)
   (RecursorArgument $position))
!(size-atom (collapse (match &self (type:rule ge $arity $patterns $body) $arity)))

; Nested constructor cases without recursion compile through nested recursors.
!(hol:define &self small (-> N B)
   (= (small z) yes)
   (= (small (s z)) yes)
   (= (small (s (s $n))) no))
!(eval (small (s z)) (admitted &self))
!(eval (small (s (s z))) (admitted &self))

; Incomplete coverage, overlap, a wrong result type and nondecreasing calls
; create no declaration. Refusing a compilation does not refute a theorem.
!(car-atom (try (hol:define &self missing (-> N N N)
   (= (missing z $n) z)
   (= (missing (s $m) (s $n)) $m))))
!(car-atom (try (hol:define &self overlap (-> N N N)
   (= (overlap $m z) z)
   (= (overlap z $n) z)
   (= (overlap (s $m) (s $n)) $m))))
!(car-atom (try (hol:define &self wrong (-> N N N)
   (= (wrong $m z) yes)
   (= (wrong z (s $n)) z)
   (= (wrong (s $m) (s $n)) $m))))
!(car-atom (try (hol:define &self loop (-> N N N)
   (= (loop $m z) z)
   (= (loop z (s $n)) z)
   (= (loop (s $m) (s $n)) (loop (s $m) (s $n))))))
!(collapse (match &self (: missing $type) leaked))
!(collapse (match &self (: overlap $type) leaked))
!(collapse (match &self (: wrong $type) leaked))
!(collapse (match &self (: loop $type) leaked))

; The original equations depend on the checked recursor's declarations,
; even though execution does not repeatedly unfold the large witness.
!(match &self (: N-rec $type) (add-atom &self (saved-rec-type $type)))
!(match &self (: N-rec $type) (remove-atom &self (: N-rec $type)))
!(car-atom (try (eval (ge (s z) z) (admitted &self))))
!(match &self (saved-rec-type $type) (add-atom &self (: N-rec $type)))
!(eval (ge (s z) z) (admitted &self))
