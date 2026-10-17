; Polymorphic instantiation: a theorem quantified over a type, proved once
; by tactics and instantiated at a base type and terms, by LCF steps, by a
; proof tree and inside a tactic; a polymorphic definition, unfolded by
; conversion; and an instantiation of the wrong type, refused.
!(import! &self megalodon)
!(let $state (mg:new-theory-state custom) (bind! &th $state))
(= (T) (mg:use (get-state &th)))
(= (AUTH) (mg:authority (get-state &th)))
(= (P) (Tm.named "P"))
(= (c) (Tm.named "c"))
(= (Pc) (Tm.app (Tm.named "P") (Tm.named "c")))

; For every type a, predicate p on a and x of a: p x -> p x.
(= (poly-refl)
   (Tm.typeAll
     (Tm.all (Tp.arr (Tp.var 0) (Tp.prop))
       (Tm.all (Tp.var 0) (Tm.imp (Tm.app (Tm.db 1) (Tm.db 0)) (Tm.app (Tm.db 1) (Tm.db 0)))))))
!(mg:statement (mg:prove-by (T) "poly_refl" (poly-refl) (Tac.intros Tac.assumption)))

!(mg:parameter (AUTH) "P" (Tp.arr (Tp.base 0) (Tp.prop)))
!(mg:parameter (AUTH) "c" (Tp.base 0))

; LCF: typeApp at the base type, then termApp at P and c.
!(mg:statement (mg:term-app (mg:term-app (mg:type-app (mg:known (T) "poly_refl") (Tp.base 0)) (P)) (c)))
!(mg:statement (mg:instantiate (mg:instantiate-types (mg:known (T) "poly_refl") ((Tp.base 0))) ((P) (c))))

; The same instance as a proof tree.
!(mg:statement (mg:check (T) (mg:empty-context)
                 (Pf.termApp (Pf.termApp (Pf.typeApp (Pf.known "poly_refl") (Tp.base 0)) (P)) (c))))

; Inside a tactic: backchain through the instance onto a hypothesis.
!(mg:statement
   (mg:run (Tac.apply (From.instance (From.type-instance (From.known "poly_refl") (types (Tp.base 0)))
                                     (terms (P) (c)))
                      Tac.assumption)
           (T) (Megalodon.context 0 (types) (hypotheses (Tm.app (Tm.named "P") (Tm.named "c")))) (Pc)))

; An instance of the wrong type is refused.
!(case (mg:try-term-app (mg:type-app (mg:known (T) "poly_refl") (Tp.base 0)) (c))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
!(case (mg:try-type-app (mg:type-app (mg:known (T) "poly_refl") (Tp.base 0)) (Tp.base 0))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))

; A polymorphic definition: the identity at every type.
!(mg:define (T) "id" (Tp.all (Tp.arr (Tp.var 0) (Tp.var 0))) (Tm.typeLam (Tm.lam (Tp.var 0) (Tm.db 0))))
!(mg:infer (T) (mg:empty-context) (Tm.app (Tm.typeApp (Tm.named "id") (Tp.base 0)) (c)))
!(mg:normalize (T) (Tm.app (Tm.typeApp (Tm.named "id") (Tp.base 0)) (c)))
!(mg:convertible (T) (mg:empty-context) (Tm.app (Tm.typeApp (Tm.named "id") (Tp.base 0)) (c)) (c))
; P (id c) -> P c, proved by conversion: the hypothesis is the goal.
!(mg:statement
   (mg:prove-by (T) "id_elim" (Tm.imp (Tm.app (P) (Tm.app (Tm.typeApp (Tm.named "id") (Tp.base 0)) (c))) (Pc))
                (Tac.intro Tac.assumption)))
; Under lazy conversion (the default) a judgment keeps the claim's source
; form; its reference form is the normal form, built only on request.
!(mg:normalize (T) (mg:proposition (mg:known (T) "id_elim")))
