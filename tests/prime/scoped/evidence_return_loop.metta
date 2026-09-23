!(bind! &thy (new-space))
!(add-atom &thy (: A (u 0)))
!(add-atom &thy (: a A))
!(add-atom &thy (: b A))

; A native identity witness survives two dependent program calls.
!(set:define &thy returnWitness
    (-> (A : (u 2)) (x : A) (y : A) (e : (id A x y)) (id A x y))
    (= (returnWitness $A $x $y $e) $e))
!(set:define &thy relayWitness
    (-> (A : (u 2)) (x : A) (y : A) (e : (id A x y)) (id A x y))
    (= (relayWitness $A $x $y $e)
       (returnWitness $A $x $y (returnWitness $A $x $y $e))))
!(type:check &thy (relayWitness A a a (refl a)) (id A a a))
!(type:eq &thy (relayWitness A a a (refl a)) (refl a))
!(type:check &thy (relayWitness (u 1) (u 0) (u 0) (refl (u 0)))
    (id (u 1) (u 0) (u 0)))
!(type:eq &thy (relayWitness (u 1) (u 0) (u 0) (refl (u 0))) (refl (u 0)))
!(type:check &thy (relayWitness (u 2) (u 1) (u 1) (refl (u 1)))
    (id (u 2) (u 1) (u 1)))

; A dependent identity consumer requires the returned witness at its exact indices.
!(type:check &thy (relayWitness A a b (refl a)) (id A a b))
!(type:check &thy (relayWitness A a a (refl a)) (id A a b))
!(type:check &thy (relayWitness A a a (refl b)) (id A a a))

; A program cannot manufacture equality between arbitrary distinct inputs.
!(let $r (try (set:define &thy fabricateWitness
    (-> (A : (u 2)) (x : A) (y : A) (id A x y))
    (= (fabricateWitness $A $x $y) (refl $x)))) (car-atom $r))
!(== (collapse (match &thy (: fabricateWitness $type) $type)) ())
!(== (collapse (match &thy (type:rule fabricateWitness $n $pats $rhs) $rhs)) ())

; An uninterpreted witness does not acquire a type from its position.
!(type:check &thy (relayWitness A a b unresolvedWitness) (id A a b))
