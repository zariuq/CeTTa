(set:profile megalodon-hotg)

; A lambda that returns a lambda, applied to two arguments, is a curried
; application read one argument at a time: each argument gives the domain of
; the lambda it meets, unless the lambda writes one, and the kernel checks
; the redex with all its arguments before it reduces it.  A definition
; applied to such a lambda unfolds to the same redexes.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
(= (verdict $result) (car-atom $result))

; The redex against the proposition it reduces to, closed and under a
; quantifier, as the goal and as the hypothesis.
!(verdict (try (set:proves &thy (quote (pf:assume h h))
   (quote (imp (In Empty Empty) ((lam u (lam v (In u v))) Empty Empty))))))
!(verdict (try (set:proves &thy (quote (pf:fix x (pf:assume h h)))
   (quote (all set (lam x (imp (In x x) ((lam u (lam v (In u v))) x x))))))))
!(verdict (try (set:proves &thy (quote (pf:fix x (pf:assume h h)))
   (quote (all set (lam x (imp ((lam u (lam v (In u v))) x x) (In x x))))))))
; A written domain on the outer lambda, none on the inner one.
!(verdict (try (set:proves &thy (quote (pf:fix x (pf:assume h h)))
   (quote (all set (lam x (imp (In x x) ((lam (u : set) (lam v (In u v))) x x))))))))
; Three arguments.
!(verdict (try (set:proves &thy (quote (pf:fix x (pf:fix y (pf:fix z (pf:assume h h)))))
   (quote (all set (lam x (all set (lam y (all set (lam z
     (imp (imp (In x y) (In y z)) ((lam u (lam v (lam w (imp (In u v) (In v w))))) x y z))))))))))))
; A witness that is a curried redex over a variable of the proof.
!(verdict (try (set:proves &thy
   (quote (pf:fix x (pf:assume h (pf:all-elim h ((lam u (lam v (Union u))) x x)))))
   (quote (all set (lam x (imp (all set (lam s (In s (Power s)))) (In (Union x) (Power (Union x))))))))))
; A definition applied to the lambda: R a a for R the curried relation.
!(set:define &thy diagonal (-> (-> set set prop) set prop) (= (diagonal $R $a) ($R $a $a)))
!(verdict (try (set:proves &thy (quote (pf:fix x (pf:assume h h)))
   (quote (all set (lam x (imp (In x x) (diagonal (lam u (lam v (In u v))) x))))))))
; A theorem stated with the redex has a native package; its statement's code
; carries the domains the arguments gave the two lambdas.
!(pf:theorem &thy curried (all set (lam x (imp (In x x) ((lam u (lam v (In u v))) x x))))
   (pf:fix x (pf:assume h h)))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy curried)
   (let (App (DeclConst $family) $code) $type (CurriedPackage $code)))

; Controls.  The arguments keep their places: exchanged, the redex is another
; proposition, and the hypothesis does not prove it.
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:fix x (pf:fix y (pf:assume h h))))
        (quote (all set (lam x (all set (lam y (imp (In x y) ((lam u (lam v (In v u))) x y)))))))))
   ($verdict (car-atom $why)))
; An argument the reduct drops is checked all the same: at a written domain
; set, a proposition is refused, though the reduct is the hypothesis.
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:fix x (pf:assume h h)))
        (quote (all set (lam x (imp (In x x)
          ((lam (u : set) (lam (v : set) (In u u))) x (In x x))))))))
   ($verdict (car-atom $why)))
; Without a written domain the argument gives one, and the body must be a
; proposition at it: membership in a proposition is refused.
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:fix x (pf:assume h h)))
        (quote (all set (lam x (imp (In x x) ((lam u (lam v (In u v))) x (In x x))))))))
   ($verdict (car-atom $why)))
