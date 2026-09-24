; The uniqueness rule K, offered as a definition by an equation at a reflexive
; index.  A case split on (id A x x) with the point repeated needs uniqueness
; of identity proofs; no profile adds it as a computation rule, so the
; definition abstains and publishes nothing, under every profile.  Where
; uniqueness is assumed, it is a declared postulate used through elimination.
!(bind! &thy (new-space))
!(set:inductive &thy num (u 0) (: zero num) (: suc (-> num num)))

; The language's eliminator computes at reflexivity.
!(type:eq &thy (id:eliminate num zero (lam y (lam e num)) zero zero (refl zero)) zero)

; K over any carrier and point.
!(let $r (try (set:define &thy kElim
   (-> (A : (u 0)) (x : A) (P : (-> (e : (id A x x)) (u 0))) (d : (P (refl x)))
       (e : (id A x x)) (P e))
   (= (kElim $A $x $P $d (refl $z)) $d))) (car-atom $r))
!(== (collapse (match &thy (type:rule kElim $arity $pats $rhs) $rhs)) ())
!(== (collapse (match &thy (: kElim $type) $type)) ())

; K at a closed carrier and point.
!(let $r (try (set:define &thy kZero
   (-> (P : (-> (e : (id num zero zero)) (u 0))) (d : (P (refl zero)))
       (e : (id num zero zero)) (P e))
   (= (kZero $P $d (refl $z)) $d))) (car-atom $r))
!(== (collapse (match &thy (type:rule kZero $arity $pats $rhs) $rhs)) ())
!(== (collapse (match &thy (: kZero $type) $type)) ())
