!(import! &self ../../../examples/atp/prime/search_modules.metta)

; Explicit, highest-to-lowest precedence.  Strictly positive unit weights make
; this an admissible KBO configuration without zero-weight special cases.
!(bind! &ground-kbo (ATP.KBO 1 1 () (b a p ATP.Eq)))
!(bind! &non-ground-kbo (ATP.KBO 1 1 () (f g q a ATP.Eq)))

!(assertEqual (atp:kbo:config-valid &ground-kbo) True)
!(assertEqual (atp:kbo:greater &ground-kbo b a) True)
!(assertEqual (atp:kbo:greater &ground-kbo a b) False)
!(assertEqual
  (atp:kbo:greater &ground-kbo a (ATP.Var x))
  False)

; Vampire's actual equality inference for the conformance problem:
;
;     ~p(b)    a = b
;     --------------  ordered superposition, b -> a at position 1
;          ~p(a)
!(bind! &eq-ab ((ATP.Pos (ATP.Eq a b))))
!(bind! &not-p-b ((ATP.Neg (p b))))
!(bind! &not-p-a ((ATP.Neg (p a))))

!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab &not-p-b (ATP.Subst ())
    (ATP.Pos (ATP.Eq a b)) ATP.RightToLeft
    (ATP.Neg (p b)) (ATP.Position (1))
    &ground-kbo &not-p-a)
  True)

; A non-ground equality certificate exercises explicit substitution before
; orientation and replacement.
!(bind! &non-ground-equality
  ((ATP.Pos
     (ATP.Eq (f (ATP.Var x)) (g (ATP.Var x))))))
!(bind! &non-ground-target ((ATP.Neg (q (f a)))))
!(bind! &non-ground-equality-literal
  (ATP.Pos
    (ATP.Eq (f (ATP.Var x)) (g (ATP.Var x)))))
!(bind! &non-ground-target-literal (ATP.Neg (q (f a))))
!(bind! &non-ground-child ((ATP.Neg (q (g a)))))
!(assertEqual
  (atp:superposition:check-unit-step
    &non-ground-equality &non-ground-target
    (ATP.Subst ((ATP.Bind x a)))
    &non-ground-equality-literal
    ATP.LeftToRight
    &non-ground-target-literal (ATP.Position (1))
    &non-ground-kbo
    &non-ground-child)
  True)

; Reversing the externally observed direction violates b > a.
!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab &not-p-b (ATP.Subst ())
    (ATP.Pos (ATP.Eq a b)) ATP.LeftToRight
    (ATP.Neg (p b)) (ATP.Position (1))
    &ground-kbo ((ATP.Neg (p a))))
  False)

; The target has no second argument.
!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab &not-p-b (ATP.Subst ())
    (ATP.Pos (ATP.Eq a b)) ATP.RightToLeft
    (ATP.Neg (p b)) (ATP.Position (2))
    &ground-kbo &not-p-a)
  False)

; Changing the claimed child clause is rejected.
!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab &not-p-b (ATP.Subst ())
    (ATP.Pos (ATP.Eq a b)) ATP.RightToLeft
    (ATP.Neg (p b)) (ATP.Position (1))
    &ground-kbo ((ATP.Neg (p forged))))
  False)

; Cyclic evidence fails substitution validation even when irrelevant to the
; ground inference.
!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab &not-p-b
    (ATP.Subst
      ((ATP.Bind x (ATP.Var y))
       (ATP.Bind y (ATP.Var x))))
    (ATP.Pos (ATP.Eq a b)) ATP.RightToLeft
    (ATP.Neg (p b)) (ATP.Position (1))
    &ground-kbo &not-p-a)
  False)

; A precedence that says a > b cannot certify the observed b -> a rewrite.
!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab &not-p-b (ATP.Subst ())
    (ATP.Pos (ATP.Eq a b)) ATP.RightToLeft
    (ATP.Neg (p b)) (ATP.Position (1))
    (ATP.KBO 1 1 () (a b p ATP.Eq)) &not-p-a)
  False)

; Ordered superposition does not rewrite at an original variable position.
!(assertEqual
  (atp:superposition:check-unit-step
    &eq-ab ((ATP.Neg (p (ATP.Var x))))
    (ATP.Subst ((ATP.Bind x b)))
    (ATP.Pos (ATP.Eq a b)) ATP.RightToLeft
    (ATP.Neg (p (ATP.Var x))) (ATP.Position (1))
    &ground-kbo &not-p-a)
  False)

; The next actual Vampire step resolves ~p(a) with p(a) to the empty clause.
!(assertEqual
  (atp:resolution:check-step
    &not-p-a ((ATP.Pos (p a))) (ATP.Subst ())
    (ATP.Neg (p a)) (ATP.Pos (p a)) ())
  True)

; The refutation also reconstructs an ordinary proof term for the unchanged
; Prime type checker.  Equality substitution is package data, not a kernel
; builtin and not an ATP acceptance shortcut.
!(bind! &equality-kb (new-space))
!(add-atom &equality-kb (: Obj Type))
!(add-atom &equality-kb (: a Obj))
!(add-atom &equality-kb (: b Obj))
!(add-atom &equality-kb (: EqT (-> Obj Obj Type)))
!(add-atom &equality-kb (: Pred (-> Obj Type)))
!(add-atom &equality-kb (: eq-ab-proof (EqT a b)))
!(add-atom &equality-kb (: p-a-proof (Pred a)))
!(add-atom &equality-kb
  (: equality-substitution
    (-> (EqT $left $right) (Pred $left) (Pred $right))))

!(assertEqual
  (check-type
    &equality-kb
    (equality-substitution eq-ab-proof p-a-proof)
    (Pred b)
    100000)
  (he-accept (exact (Pred b))))

!(println! (ATPSuperpositionReplaySummary 14 14 0))
