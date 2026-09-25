; The cumulativity curriculum: the programs that tell the cumulativity rules
; apart, in one theory. A : Sort 0; F : A -> Sort 0; G : A -> A -> Sort 0;
; p : Sigma (y : A). Sort 0; X Y : Sort 0; e : Id (Sort 0) X Y. Cumulativity
; is covariant: a type is usable at another when both are universes in order,
; both are function types with equal domains and codomains in order, or both
; are pair types with both components in order. Each comment names the theorem
; of the Lean development (TypedEquality) that licenses the verdict.

; A variable of a universe is a term of every higher universe
; (Below.universe_iff).
!(type:check (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0))
             (Sort (LevelConst 1)))

; A family F : A -> Sort 0 is a family into Sort 1 with no eta wrapper
; (Below.pi_iff, family_typed_at_raised_codomain), and so is its
; eta-expansion lam x. F x.
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (idx 1) (Sort (LevelConst 1))))
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))
                          (Lam (idx 1) (App (idx 1) (idx 0))))
             (Pi (idx 1) (Sort (LevelConst 1))))

; Two arguments deep: G : A -> A -> Sort 0 at A -> A -> Sort 1, and its
; eta-expansion (Below.pi_iff twice).
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Pi (idx 1) (Sort (LevelConst 0))))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (idx 1) (Pi (idx 2) (Sort (LevelConst 1)))))
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Pi (idx 1) (Sort (LevelConst 0))))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))
                          (Lam (idx 1) (Lam (idx 2) (App (App (idx 2) (idx 1)) (idx 0)))))
             (Pi (idx 1) (Pi (idx 2) (Sort (LevelConst 1)))))

; A pair p : Sigma (y : A). Sort 0 at Sigma (y : A). Sort 1 (Below.sigma_iff),
; and its eta-expansion (pair (fst p) (snd p)).
!(type:check (PrimeScoped (PrimeCtxCons (Sigma (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Sigma (idx 1) (Sort (LevelConst 1))))
!(type:check (PrimeScoped (PrimeCtxCons (Sigma (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))
                          (Pair (Fst (idx 0)) (Snd (idx 0))))
             (Sigma (idx 1) (Sort (LevelConst 1))))

; A function on families: lam f. f at (A -> Sort 0) -> (A -> Sort 1): the
; body f is checked at the larger codomain (Below.pi_iff on the codomain), and
; so is its eta-expansion lam f. lam x. f x.
!(type:check (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                          (Lam (Pi (idx 0) (Sort (LevelConst 0))) (idx 0)))
             (Pi (Pi (idx 0) (Sort (LevelConst 0))) (Pi (idx 1) (Sort (LevelConst 1)))))
!(type:check (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                          (Lam (Pi (idx 0) (Sort (LevelConst 0))) (Lam (idx 1) (App (idx 1) (idx 0)))))
             (Pi (Pi (idx 0) (Sort (LevelConst 0))) (Pi (idx 1) (Sort (LevelConst 1)))))

; An identity proof e : Id (Sort 0) X Y is not a proof at Id (Sort 1) X Y: the
; carrier of an identity type is invariant, and an identity type is usable
; only at equal types (RigidShape.below_iff, raised_carrier_ne).
!(type:check (PrimeScoped (PrimeCtxCons (Id (Sort (LevelConst 0)) (idx 1) (idx 0))
                            (PrimeCtxCons (Sort (LevelConst 0))
                              (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (idx 0))
             (Id (Sort (LevelConst 1)) (idx 2) (idx 1)))

; Reflexivity at a raised carrier: refl X : Id (Sort 1) X X holds because
; X : Sort 1 by cumulativity and refl is checked against the identity type it
; is given (KernelTyping.reflCheck). Id (Sort 0) X X and Id (Sort 1) X X are
; distinct types (raised_carrier_ne), so refl X has no least type
; (refl_type_no_least_type).
!(type:check (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (Refl (idx 0)))
             (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (Id (Sort (LevelConst 0)) (idx 0) (idx 0)))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (Id (Sort (LevelConst 1)) (idx 0) (idx 0))))

; Usable at is not equal to: A -> Sort 0 and A -> Sort 1 are not equal types
; (Below.pi_codomain_raise).
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (Pi (idx 0) (Sort (LevelConst 0))))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (Pi (idx 0) (Sort (LevelConst 1)))))

; Domains are invariant, so contravariance is refused: h : (A -> Sort 1) ->
; Sort 0 is not a function of (A -> Sort 0) (Below.pi_iff,
; domain_contravariance_refused).
!(type:check (PrimeScoped (PrimeCtxCons (Pi (Pi (idx 0) (Sort (LevelConst 1))) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (Pi (idx 1) (Sort (LevelConst 0))) (Sort (LevelConst 0))))

; Reflexivity at a raised function carrier: refl F proves F = F at
; Id (A -> Sort 1) F F too (refl_family_at_both_carriers); the two identity
; types stay distinct (raised_function_carrier_ne), so refl F has no least
; type either (refl_family_no_least_type).
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (Refl (idx 0)))
             (Id (Pi (idx 1) (Sort (LevelConst 1))) (idx 0) (idx 0)))
!(type:eq (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))
                       (Id (Pi (idx 1) (Sort (LevelConst 0))) (idx 0) (idx 0)))
          (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))
                       (Id (Pi (idx 1) (Sort (LevelConst 1))) (idx 0) (idx 0))))

; A family into Sort 1 is not a family into Sort 0: cumulativity does not
; lower (Below.pi_iff, Below.universe_iff).
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 1) (Sort (LevelConst 1)))
                            (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                              (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (idx 0))
             (Pi (idx 2) (Sort (LevelConst 0))))

; Equality at a common upper type: F and (lam (y : A -> Sort 1). y) F have
; the distinct synthesized types A -> Sort 0 and A -> Sort 1, yet F is also a
; term of A -> Sort 1, where the two are equal by beta.
!(type:eq (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
          (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))
                       (App (Lam (Pi (idx 1) (Sort (LevelConst 1))) (idx 0)) (idx 0))))

; Refutations of equality under cumulativity. F and A have types with no
; common upper bound, a function type and a universe (Synth.not_equal_types,
; not_boundedAbove_universe_pi). F : A -> Sort 0 and G : A -> Sort 1 do share
; the type A -> Sort 1, which is G's principal type; there they differ, so they
; are equal at no type (not_equal_of_unrelated, Algorithmic.terms_down).
!(type:eq (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
          (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 1)))
!(type:eq (PrimeScoped (PrimeCtxCons (Pi (idx 1) (Sort (LevelConst 1)))
                         (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                           (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (idx 1))
          (PrimeScoped (PrimeCtxCons (Pi (idx 1) (Sort (LevelConst 1)))
                         (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                           (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (idx 0)))

; Quotation stays inert: a quoted check is data, compared as written, and is
; not run.
!(== (quote (type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                                   (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
                        (Pi (idx 1) (Sort (LevelConst 1)))))
     (quote True))

; Fuel: with too small a producer budget the check is left unresolved, the
; judgment staying as written, and never refuted; with enough, the raised
; family is accepted and the mismatched domain refuted.
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (idx 1) (Sort (LevelConst 1))) 8)
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (idx 1) (Sort (LevelConst 1))) 256)
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (Sort (LevelConst 1)) (Sort (LevelConst 0))) 8)
!(type:check (PrimeScoped (PrimeCtxCons (Pi (idx 0) (Sort (LevelConst 0)))
                                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0))
             (Pi (Sort (LevelConst 1)) (Sort (LevelConst 0))) 256)

; A program: a family of types defined by recursion, fam : num -> Sort 0,
; used where a family into Sort 1 is expected (Below.pi_iff), with no eta
; wrapper; its eta-expansion and a value at a raised universe too.
!(bind! &thy (new-space))
!(set:inductive &thy num (u 0) (: zero num) (: succ (-> num num)))
!(set:define &thy fam (-> num (u 0))
   (= (fam zero) num)
   (= (fam (succ $n)) (fam $n)))
!(type:check &thy fam (-> num (u 1)))
!(type:check &thy (lam x (fam x)) (-> num (u 1)))
!(type:check &thy (fam (succ zero)) (u 1))
