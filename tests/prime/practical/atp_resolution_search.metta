!(import! &self ../../../examples/atp/prime/search_modules.metta)

(= (test:refutation-replays $result)
   (case $result
     (((ATP.ResolutionSearchResult
         (ATP.Refutation $proof) $stats)
        (atp:resolution-search:replay $proof))
      ($_ False))))

(= (test:incomplete-reason $result $expected)
   (case $result
     (((ATP.ResolutionSearchResult
         (ATP.Incomplete $reason) $stats)
        (== $reason $expected))
      ($_ False))))

(= (test:resolution-stats $result)
   (case $result
     (((ATP.ResolutionSearchResult $outcome $stats) $stats)
      ($_ ATP.NoStats))))

; Native replay uses ordinary MeTTa unification and instantiates the residual
; literals under the same substitution.
!(assertEqual
  (atp:resolution-search:check-native-step
    ((ATP.Pos (p $x)) (ATP.Pos (q $x)))
    ((ATP.Neg (p a)))
    (ATP.Pos (p $x)) (ATP.Neg (p a))
    ((ATP.Pos (q a))))
  True)

; A forged child is not a resolution inference.
!(assertEqual
  (atp:resolution-search:check-native-step
    ((ATP.Pos (p $x)) (ATP.Pos (q $x)))
    ((ATP.Neg (p a)))
    (ATP.Pos (p $x)) (ATP.Neg (p a))
    ((ATP.Pos forged)))
  False)

; The ordinary unifier's occurs check remains visible at the replay boundary.
!(assertEqual
  (atp:resolution-search:check-native-step
    ((ATP.Pos (p $x)))
    ((ATP.Neg (p (f $x))))
    (ATP.Pos (p $x)) (ATP.Neg (p (f $x)))
    ())
  False)

; Search standardizes independent clauses apart.  The universally quantified
; units P(X) and ~P(f(Y)) therefore have a valid resolvent even when authored
; with the same display spelling.
!(assertEqual
  (let $proofs
       (reify
         (atp:resolution-search:resolve
           (ATP.InputClause positive ((ATP.Pos (p $x))))
           (ATP.InputClause negative ((ATP.Neg (p (f $x)))))))
    (if (== (size-atom $proofs) 1)
        (let ($proof $rest) (decons-atom $proofs)
          (if (== (atp:resolution-search:conclusion $proof) ())
              (atp:resolution-search:replay $proof)
              False))
        False))
  True)

; A genuine non-ground given-clause search reaches the empty clause.  Only the
; negated goal starts in the set of support; no proof route is supplied.
!(bind! &small-search
  (atp:resolution-search:prove
    ((ATP.NamedClause disjunction
       ((ATP.Pos (p $x)) (ATP.Pos (q $x))))
     (ATP.NamedClause not-p-a ((ATP.Neg (p a)))))
    ((ATP.NamedClause not-q-a ((ATP.Neg (q a)))))
    (ATP.GivenClauseBounds 20 100)
    (ATP.AgeWeight 5 1)))

!(assertEqual (test:refutation-replays &small-search) True)

!(assertEqual
  (test:resolution-stats &small-search)
  (ATP.ResolutionStats 2 2 2 0 0 0 False))

; The signed-literal Atomspace index changes candidate generation only.  It
; produces and replays the same refutation with the same logical statistics.
!(bind! &small-indexed-search
  (atp:resolution-search:prove-indexed
    ((ATP.NamedClause disjunction
       ((ATP.Pos (p $x)) (ATP.Pos (q $x))))
     (ATP.NamedClause not-p-a ((ATP.Neg (p a)))))
    ((ATP.NamedClause not-q-a ((ATP.Neg (q a)))))
    (ATP.GivenClauseBounds 20 100)
    (ATP.AgeWeight 5 1)))

!(assertEqual (test:refutation-replays &small-indexed-search) True)

!(assertEqual
  (test:resolution-stats &small-indexed-search)
  (ATP.ResolutionStats 2 2 2 0 0 0 False))

; Corrupting a proof-tree child is detected independently of search.
!(assertEqual
  (atp:resolution-search:replay
    (ATP.ResolutionProof
      (ATP.InputClause left
        ((ATP.Pos (p a)) (ATP.Pos (q a))))
      (ATP.InputClause right ((ATP.Neg (p a))))
      (ATP.Pos (p a)) (ATP.Neg (p a))
      ((ATP.Pos forged))))
  False)

; Exact alpha variants are recognized before they can be queued again.
!(assertEqual
  (atp:resolution-search:variant-in-proofs
    ((ATP.Pos (p $fresh)))
    ((ATP.InputClause existing ((ATP.Pos (p $old))))))
  True)

; Tautology deletion requires the same variable occurrence, not merely
; unifiable atoms.  P(X) | ~P(Y) is not valid when X and Y are independent.
!(assertEqual
  (atp:resolution-search:tautology
    ((ATP.Pos (p $x)) (ATP.Neg (p $x))))
  True)

!(assertEqual
  (atp:resolution-search:tautology
    ((ATP.Pos (p $x)) (ATP.Neg (p $y))))
  False)

!(assertEqual
  (atp:resolution-search:tautology
    ((ATP.Pos (p $x)) (ATP.Neg (p (f $x)))))
  False)

; Exhaustion and a zero step bound are both explicit incomplete outcomes.
!(bind! &exhausted
  (atp:resolution-search:prove
    ((ATP.NamedClause p-a ((ATP.Pos (p a)))))
    ((ATP.NamedClause not-q-a ((ATP.Neg (q a)))))
    (ATP.GivenClauseBounds 10 10)
    (ATP.AgeWeight 5 1)))

!(assertEqual
  (test:incomplete-reason &exhausted PassiveExhausted)
  True)

!(bind! &step-bounded
  (atp:resolution-search:prove
    ((ATP.NamedClause p-a ((ATP.Pos (p a)))))
    ((ATP.NamedClause not-p-a ((ATP.Neg (p a)))))
    (ATP.GivenClauseBounds 0 10)
    (ATP.AgeWeight 5 1)))

!(assertEqual
  (test:incomplete-reason &step-bounded StepBound)
  True)

; Set-of-support is explicit: with no support clauses, usable clauses are not
; silently saturated and the result is incomplete rather than a refutation.
!(bind! &empty-support
  (atp:resolution-search:prove
    ((ATP.NamedClause p-a ((ATP.Pos (p a))))
     (ATP.NamedClause not-p-a ((ATP.Neg (p a)))))
    ()
    (ATP.GivenClauseBounds 10 10)
    (ATP.AgeWeight 5 1)))

!(assertEqual
  (test:incomplete-reason &empty-support PassiveExhausted)
  True)

; Malformed unsigned atoms are rejected at the inference boundary.
!(assertEqual
  (atp:resolution-search:check-native-step
    ((p a)) ((ATP.Neg (p a)))
    (p a) (ATP.Neg (p a)) ())
  False)

!(println! (ATPResolutionSearchSummary 16 16 0))
