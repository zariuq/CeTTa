!(import! &self lts:he)

; HE frontier LangDef pack witnesses.
;
; The one-step frontier's covered rule classes (grounded dispatch, leftmost
; expression congruence) are routed through the HE frontier pack and gated by
; rule ID.  These witnesses pin the pack-driven behavior; the companion
; Makefile lane (test-langdef-pack) re-runs this file with individual pack
; rules disabled via CETTA_LANGDEF_DISABLED_RULES and requires the witnesses
; to FAIL, proving the pack is load-bearing rather than decorative.

(= (f $x) (g $x))
(= (f $x) (h $x))
(= (g 1) done)
(= (k $x) AA)
(= (k BB) AA)

; Pack reflection: language/profile/granularity, source digest, claim level,
; rule inventory, and the (empty) disabled set.
!(assertEqual
  (lts:he:pack-info)
  (langdef-pack HE he-extended frontier "fnv1a64:4b3784d4f94a42d5"
    (claim bag-tested-adequate)
    (rules HEF_GroundedDispatch HEF_LeftmostExprCongruence HEF_QuoteQuiescent
           HEF_ReturnQuiescent HEF_EmptyQuiescent HEF_ErrorQuiescent)
    (disabled)))

; G1 — congruence witness: nested grounded argument steps once, leftmost.
!(assertEqualToResult
  (collapse (lts:he:transitions (+ 1 (+ 2 3))))
  (((quote (+ 1 5)))))

!(assertEqual (lts:he:is-normal-form (+ 1 (+ 2 3))) False)

!(assertEqual (lts:he:is-can-step (+ 1 (+ 2 3))) True)

; G3 — branching congruence: a nondeterministic inner step fans the outer
; frontier out to every inner successor.
!(assertEqualToResult
  (collapse (lts:he:transitions (g (f 1))))
  (((quote (g (g 1))) (quote (g (h 1))))))

; G4 — multiplicity: duplicate inner results yield duplicate outer
; successors (the frontier is a bag, not a set).
!(assertEqualToResult
  (collapse (lts:he:transitions (pair (k BB))))
  (((quote (pair AA)) (quote (pair AA)))))

!(assertEqual (lts:he:is-can-step (pair (k BB))) True)

; G5 — soundness: genuine normal forms stay quiescent under the pack route.
!(assertEqual (lts:he:is-normal-form 5) True)

!(assertEqual (lts:he:is-normal-form (g done)) True)

!(assertEqualToResult
  (collapse (lts:he:transitions (g done)))
  (()))

!(assertEqual (lts:he:is-normal-form (foo bar baz)) True)
