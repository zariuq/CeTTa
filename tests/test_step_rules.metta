!(import! &self lts:he)

; HE small-step rule-table witnesses.
;
; The one-step relation's covered rule classes (grounded dispatch, leftmost
; expression congruence, equation match) are routed through the HE small-step
; rule table and gated by rule ID.  These witnesses pin the table-driven
; behavior; the companion Makefile lane (test-step-rules) re-runs this file
; with individual rules disabled via CETTA_LANGDEF_DISABLED_RULES and requires
; the witnesses to FAIL, proving the table is load-bearing rather than
; decorative.

(= (f $x) (g $x))
(= (f $x) (h $x))
(= (g 1) done)
(= (k $x) AA)
(= (k BB) AA)

; Rule-table reflection: language/granularity, source digest, schema, the
; rule inventory with per-rule claim levels, and the (empty) disabled set.
; Deliberately no active-profile field; claim levels are evidence about
; rules (excluded from the digest) and upgrade per-rule as Lean soundness
; theorems land.
!(assertEqual
  (lts:he:step-rules)
  (step-rules HE small-step "fnv1a64:b38899af8133fc21"
    (schema 2)
    (rules (HES_GroundedDispatch rule-sound)
           (HES_LeftmostExprCongruence rule-sound-on-fragment)
           (HES_EquationMatch rule-sound)
           (HES_Eval rule-sound)
           (HES_Chain rule-sound-on-fragment)
           (HES_Let bag-tested-adequate)
           (HES_LetStar bag-tested-adequate)
           (HES_Case bag-tested-adequate)
           (HES_Switch bag-tested-adequate)
           (HES_SwitchMinimal bag-tested-adequate)
           (HES_SpaceMeet bag-tested-adequate)
           (HES_SpaceAdditiveUnion bag-tested-adequate)
           (HES_SourcedConjunctionMatch bag-tested-adequate)
           (HES_QuoteQuiescent bag-tested-adequate)
           (HES_ReturnQuiescent bag-tested-adequate)
           (HES_EmptyQuiescent bag-tested-adequate)
           (HES_ErrorQuiescent bag-tested-adequate))
    (disabled)))

; Congruence witness: nested grounded argument steps once, leftmost.
!(assertEqualToResult
  (collapse (lts:he:transitions (+ 1 (+ 2 3))))
  (((quote (+ 1 5)))))

!(assertEqual (lts:he:is-normal-form (+ 1 (+ 2 3))) False)

!(assertEqual (lts:he:is-can-step (+ 1 (+ 2 3))) True)

; Equation-match witness: a user equation steps at the root, relationally
; (both matching equations contribute successors).
!(assertEqualToResult
  (collapse (lts:he:transitions (f 5)))
  (((quote (g 5)) (quote (h 5)))))

; Branching congruence: a nondeterministic inner step fans the outer
; successor set out to every inner successor.
!(assertEqualToResult
  (collapse (lts:he:transitions (g (f 1))))
  (((quote (g (g 1))) (quote (g (h 1))))))

; Multiplicity: duplicate inner results yield duplicate outer successors
; (the one-step successor set is a bag, not a set).
!(assertEqualToResult
  (collapse (lts:he:transitions (pair (k BB))))
  (((quote (pair AA)) (quote (pair AA)))))

!(assertEqual (lts:he:is-can-step (pair (k BB))) True)

; Special-form witnesses (value-source cases, where each form's own
; semantics is visibly distinct from inert-head congruence fallback).
!(assertEqualToResult
  (collapse (lts:he:transitions (eval (+ 1 2))))
  (((quote (+ 1 2)))))

!(assertEqualToResult
  (collapse (lts:he:transitions (chain 7 $x (pair $x))))
  (((quote (pair 7)))))

!(assertEqualToResult
  (collapse (lts:he:transitions (let $x 7 (pair $x))))
  (((quote (pair 7)))))

!(assertEqualToResult
  (collapse (lts:he:transitions (let* (($x 7)) (pair $x))))
  (((quote (let $x 7 (let* () (pair $x)))))))

!(assertEqualToResult
  (collapse (lts:he:transitions (case 3 ((3 three) ($_ other)))))
  (((quote three))))

!(assertEqualToResult
  (collapse (lts:he:transitions (switch 3 ((3 three) ($_ other)))))
  (((quote three))))

; The switch / switch-minimal distinction (upstream contract, ratified
; 2026-06-10): switch EVALUATES its scrutinee first; switch-minimal
; matches the raw scrutinee structurally.  The same reducible scrutinee
; distinguishes them.
!(assertEqual
  (collapse (lts:he:transitions (switch (+ 1 2) (((+ $a $b) (got $a $b)) (3 three)))))
  ((quote (switch 3 (((+ $a $b) (got $a $b)) (3 three))))))
!(assertEqual
  (collapse (lts:he:transitions (switch-minimal (+ 1 2) (((+ $a $b) (got $a $b)) (3 three)))))
  ((quote (got 1 2))))

!(assertEqualToResult
  (collapse (lts:he:transitions (chain 7 $x)))
  (((Error (chain 7 $x) IncorrectNumberOfArguments))))

; Soundness: genuine normal forms stay quiescent under the table route.
!(assertEqual (lts:he:is-normal-form 5) True)

!(assertEqual (lts:he:is-normal-form (g done)) True)

!(assertEqualToResult
  (collapse (lts:he:transitions (g done)))
  (()))

!(assertEqual (lts:he:is-normal-form (foo bar baz)) True)
