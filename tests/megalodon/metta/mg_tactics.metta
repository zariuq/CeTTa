; Tactics: untrusted MeTTa search over the checked natives.  Each answer is
; a theorem made by native rules; a tactic that finds nothing has no
; answer.  Covered: assumption, implication and quantifier introduction,
; application (backchaining) with term and type instantiation, a first-of
; combinator, and a short composite (auto) whose theorem is published and
; reused by a later proof.
!(import! &self megalodon)
!(let $state (mg:new-theory-state custom) (bind! &th $state))
(= (T) (mg:use (get-state &th)))
(= (AUTH) (mg:authority (get-state &th)))
(= (A) (Tm.named "A"))
(= (B) (Tm.named "B"))
(= (C) (Tm.named "C"))
(= (imp $p $q) (Tm.imp $p $q))
!(mg:parameter (AUTH) "A" (Tp.prop))
!(mg:parameter (AUTH) "B" (Tp.prop))
!(mg:parameter (AUTH) "C" (Tp.prop))
!(mg:parameter (AUTH) "R" (Tp.arr (Tp.base 0) (Tp.prop)))

; assumption, after introducing the implication.
!(mg:statement (mg:run (Tac.intro Tac.assumption) (T) (mg:empty-context) (imp (A) (A))))
; assumption fails without a matching hypothesis: no answer.
!(collapse (mg:run Tac.assumption (T) (mg:empty-context) (A)))
; apply a hypothesis: A -> (A -> B) -> B.
!(mg:statement (mg:run (Tac.intros (Tac.apply (From.hyp 0) Tac.assumption)) (T) (mg:empty-context)
                 (imp (A) (imp (imp (A) (B)) (B)))))
; Term quantifier introduction: forall x. R x -> R x.
!(mg:statement (mg:run (Tac.intros Tac.assumption) (T) (mg:empty-context)
                 (Tm.all (Tp.base 0) (imp (Tm.app (Tm.named "R") (Tm.db 0)) (Tm.app (Tm.named "R") (Tm.db 0))))))
; A first-of combinator: the first alternative fails, the second proves.
!(mg:statement (mg:run (Tac.first Tac.assumption (Tac.intro Tac.assumption)) (T) (mg:empty-context) (imp (B) (B))))

; auto: transitivity of implication, published.
!(mg:statement (mg:prove-by (T) "imp_trans" (imp (imp (A) (B)) (imp (imp (B) (C)) (imp (A) (C))))
                 (Tac.auto 3 (lemmas))))
; A lemma with a type quantifier, then auto backchaining through a
; type-instantiated, term-instantiated lemma.
!(mg:statement (mg:prove-by (T) "poly_mp"
                 (Tm.typeAll (Tm.all (Tp.arr (Tp.var 0) (Tp.prop)) (Tm.all (Tp.var 0)
                   (imp (Tm.app (Tm.db 1) (Tm.db 0)) (imp (imp (Tm.app (Tm.db 1) (Tm.db 0)) (Tm.named "A")) (Tm.named "A"))))))
                 (Tac.intros (Tac.apply (From.hyp 0) Tac.assumption))))
(= (c) (Tm.named "c"))
!(mg:parameter (AUTH) "c" (Tp.base 0))
!(mg:statement
   (mg:run (Tac.intros (Tac.apply (From.instance (From.type-instance (From.known "poly_mp") (types (Tp.base 0)))
                                                 (terms (Tm.named "R") (c)))
                                  Tac.assumption))
           (T) (mg:empty-context)
           (imp (Tm.app (Tm.named "R") (c)) (imp (imp (Tm.app (Tm.named "R") (c)) (A)) (A)))))
; Reuse of the published lemma inside auto.
!(mg:statement (mg:run (Tac.auto 2 (lemmas "imp_trans")) (T)
                 (Megalodon.context 0 (types) (hypotheses (imp (B) (C)) (imp (A) (B))))
                 (imp (A) (C))))
; auto within its depth finds nothing for an unprovable goal.
!(collapse (mg:run (Tac.auto 2 (lemmas "imp_trans")) (T) (mg:empty-context) (imp (A) (B))))
; prove-by reports a goal it could not prove; nothing is published.
!(case (mg:prove-by (T) "A_imp_B" (imp (A) (B)) (Tac.auto 2 (lemmas)))
   (((Error $call $why) (unproved $why)) ($theorem (accepted $theorem))))
!(mg:inventory (T))
