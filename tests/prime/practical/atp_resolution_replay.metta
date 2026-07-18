!(import! &self lib_atp)

; This is the four-step binary-resolution spine emitted by E for the full
; prior-theory encoding of set.mm theorem ja.  E's preprocessing names are
; replaced by stable object-variable names, but the clauses, pivots, and
; substitutions are unchanged.

; pm2.61d1: P -> (Q -> R), ~Q -> R |- P -> R
!(bind! &c8
  (atp:resolution:horn-clause
    ((proved (imp (ATP.Var c8-p) (imp (ATP.Var c8-q) (ATP.Var c8-r))))
     (proved (imp (neg (ATP.Var c8-q)) (ATP.Var c8-r))))
    (proved (imp (ATP.Var c8-p) (ATP.Var c8-r)))))

; Hypothesis 1: ~ph -> ch
!(bind! &c9
  ((ATP.Pos (proved (imp (neg f-ph-183) f-ch-183)))))

; imim2i: P -> Q |- (R -> P) -> (R -> Q)
!(bind! &c10
  (atp:resolution:horn-clause
    ((proved (imp (ATP.Var c10-p) (ATP.Var c10-q))))
    (proved
      (imp
        (imp (ATP.Var c10-r) (ATP.Var c10-p))
        (imp (ATP.Var c10-r) (ATP.Var c10-q))))))

; Hypothesis 2: ps -> ch
!(bind! &c11
  ((ATP.Pos (proved (imp f-ps-183 f-ch-183)))))

; Negated conjecture: ~proved((ph -> ps) -> ch)
!(bind! &c15
  ((ATP.Neg
     (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))))

!(bind! &c13
  ((ATP.Pos (proved (imp (ATP.Var c8-p) f-ch-183)))
   (ATP.Neg
     (proved
       (imp (ATP.Var c8-p) (imp f-ph-183 f-ch-183))))))

!(bind! &c14
  ((ATP.Pos
     (proved
       (imp
         (imp (ATP.Var c10-r) f-ps-183)
         (imp (ATP.Var c10-r) f-ch-183))))))

!(bind! &c15-positive
  ((ATP.Pos
     (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))))

; Positive replay: the two source rules are first deterministically viewed as
; Horn clauses, then four explicit resolution certificates reach the empty
; clause.
!(assertEqual
  (atp:resolution:check-step
    &c8 &c9
    (ATP.Subst
      ((ATP.Bind c8-q f-ph-183)
       (ATP.Bind c8-r f-ch-183)))
    (ATP.Neg
      (proved (imp (neg (ATP.Var c8-q)) (ATP.Var c8-r))))
    (ATP.Pos (proved (imp (neg f-ph-183) f-ch-183)))
    &c13)
  True)

!(assertEqual
  (atp:resolution:check-step
    &c10 &c11
    (ATP.Subst
      ((ATP.Bind c10-p f-ps-183)
       (ATP.Bind c10-q f-ch-183)))
    (ATP.Neg
      (proved (imp (ATP.Var c10-p) (ATP.Var c10-q))))
    (ATP.Pos (proved (imp f-ps-183 f-ch-183)))
    &c14)
  True)

!(assertEqual
  (atp:resolution:check-step
    &c13 &c14
    (ATP.Subst
      ((ATP.Bind c8-p (imp f-ph-183 f-ps-183))
       (ATP.Bind c10-r f-ph-183)))
    (ATP.Neg
      (proved
        (imp (ATP.Var c8-p) (imp f-ph-183 f-ch-183))))
    (ATP.Pos
      (proved
        (imp
          (imp (ATP.Var c10-r) f-ps-183)
          (imp (ATP.Var c10-r) f-ch-183))))
    &c15-positive)
  True)

!(assertEqual
  (atp:resolution:check-step
    &c15-positive &c15
    (ATP.Subst ())
    (ATP.Pos
      (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))
    (ATP.Neg
      (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))
    ())
  True)

; Negative: a forged resolvent is rejected.
!(assertEqual
  (atp:resolution:check-step
    &c15-positive &c15
    (ATP.Subst ())
    (ATP.Pos
      (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))
    (ATP.Neg
      (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))
    ((ATP.Pos (proved forged))))
  False)

; Negative: omitting a required binding leaves non-complementary pivots.
!(assertEqual
  (atp:resolution:check-step
    &c8 &c9
    (ATP.Subst ((ATP.Bind c8-q f-ph-183)))
    (ATP.Neg
      (proved (imp (neg (ATP.Var c8-q)) (ATP.Var c8-r))))
    (ATP.Pos (proved (imp (neg f-ph-183) f-ch-183)))
    &c13)
  False)

; Negative: a cyclic candidate substitution is applied as finite syntax and
; cannot pretend that p(X) and p(f(X)) are complementary.
!(assertEqual
  (atp:resolution:check-step
    ((ATP.Pos (p (ATP.Var x))))
    ((ATP.Neg (p (f (ATP.Var x)))))
    (ATP.Subst ((ATP.Bind x (f (ATP.Var x)))))
    (ATP.Pos (p (ATP.Var x)))
    (ATP.Neg (p (f (ATP.Var x))))
    ())
  False)

; Negative: duplicate keys make a substitution ambiguous and fail closed.
!(assertEqual
  (atp:resolution:check-step
    &c15-positive &c15
    (ATP.Subst
      ((ATP.Bind x a) (ATP.Bind x b)))
    (ATP.Pos
      (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))
    (ATP.Neg
      (proved (imp (imp f-ph-183 f-ps-183) f-ch-183)))
    ())
  False)

; Positive: an acyclic dependency chain is a finite substitution.
!(assertEqual
  (atp:resolution:substitution-valid
    (ATP.Subst
      ((ATP.Bind x (ATP.Var y))
       (ATP.Bind y a))))
  True)

; Negative: an indirect cycle is invalid even when its bindings are irrelevant
; to a particular inference step.
!(assertEqual
  (atp:resolution:substitution-valid
    (ATP.Subst
      ((ATP.Bind x (ATP.Var y))
       (ATP.Bind y (ATP.Var x)))))
  False)

; The resolution DAG reconstructs the ordinary Curry--Howard proof term.  Its
; authority is the unchanged Prime type checker, not the resolution library.
!(bind! &ja-kb (new-space))
!(add-atom &ja-kb (: Type Type))
!(add-atom &ja-kb (: f-ph-183 Type))
!(add-atom &ja-kb (: f-ps-183 Type))
!(add-atom &ja-kb (: f-ch-183 Type))
!(add-atom &ja-kb (: imp (-> Type Type Type)))
!(add-atom &ja-kb (: neg (-> Type Type)))
!(add-atom &ja-kb
  (: h1 (imp (neg f-ph-183) f-ch-183)))
!(add-atom &ja-kb
  (: h2 (imp f-ps-183 f-ch-183)))
!(add-atom &ja-kb
  (: pm2.61d1
    (-> (imp $ph (imp $ps $ch))
        (imp (neg $ps) $ch)
        (imp $ph $ch))))
!(add-atom &ja-kb
  (: imim2i
    (-> (imp $ph $ps)
        (imp (imp $ch $ph) (imp $ch $ps)))))

!(assertEqual
  (check-type
    &ja-kb
    (pm2.61d1 (imim2i h2) h1)
    (imp (imp f-ph-183 f-ps-183) f-ch-183)
    100000)
  (he-accept (exact (imp (imp f-ph-183 f-ps-183) f-ch-183))))

!(println! (ATPResolutionReplaySummary 11 11 0))
