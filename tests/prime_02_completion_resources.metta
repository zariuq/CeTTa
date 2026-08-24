; Prime 0.2 completion polarity and aggregate-resource fixtures.

(: Nat Type)
(: Bool Type)
(: Query Type)
(: Done Type)
(: Z Nat)
(: One Nat)
(: Truth Bool)
(: q Query)
(: done Done)
(: unconstrained $u)
(: dynamic-term %Undefined%)

(= (prime-order-and-resources
      (PrimeDefV3
        $identity $language $contexts $checking
        (ResultAlgebraV1 $results $information $nondeterminism)
        (EffectsAndResourcesV1 $effects $resources)
        $evidence))
   ($information $resources))
(= (prime-def-roots
      (PrimeDefV3
        $identity
        (LanguageDefV1 $syntax $binders $equations $rewrites)
        (ContextsAndSchemesV1
          $classes $markers $no-generalization $fresh $rigid $substitutions)
        (CheckingV1 $gradual $authority-indexed $catalog
          (PrimeDependentCheckingV1
            $judgments $edges $checked $telescopes $bidirectional $convert
            $refinements))
        (ResultAlgebraV1 $results $information $nondeterminism)
        (EffectsAndResourcesV1 $effects $resources)
        (EvidenceSchemaV2
          $verdict $prime-evidence $ledger $nik-receipt $authority-replay
          $answer $substitution $producer $recheck $normal-forms
          $correspondence $source-identity)))
   (PrimeDefRoots
     LanguageDefV1 ContextsAndSchemesV1 CheckingV1 ResultAlgebraV1
     EffectsAndResourcesV1 EvidenceSchemaV2))

; Closed judgments never solve rigid or out-of-scope variables silently.
!(let $answer (type:check Z $Expected 1000)
   (OpenExpected $answer))
!(let $answer (type:check unconstrained Bool 1000)
   (UnconstrainedDeclaration $answer))
!(let $answer (type:formed (-> (: $x Nat) $outside) 1000)
   (EscapedBinder $answer))
!(let $answer (type:formed (Type 1) 1000)
   (UniverseDeferred $answer))
; `type:analyze` keeps success-typing consistency separate from exact/structural
; checking at a gradual boundary.
!(let $answer (type:analyze dynamic-term Nat 1000)
   (AnalyzeDynamic $answer))
!(let $answer (type:check dynamic-term Nat 1000)
   (CheckDynamic $answer))
; The ordinary syntax is genuinely unbounded. This terminating evaluation
; takes more than 100000 evaluator steps; the explicit bounded form is the
; only one that reports resource incompleteness.
(= (long-default $n)
   (if (<= $n 0) done (long-default (- $n 1))))
!(let $answer (type:may (long-default 30000) Done)
   (UnboundedDefault $answer))
!(let $answer (type:may (long-default 30000) Done 100000)
   (ExplicitBound $answer))

; Public may/must always derive completion from the evaluator. Producer bag
; structure and exact ledgers are checked through the direct C service.
(= (choose) Z)
(= (choose) Truth)
(= (only) Z)
!(let $answer (type:may (choose) Nat 1000)
   (EvaluatedMayWitness $answer))
!(let $answer (type:must (choose) Nat 1000)
   (EvaluatedMustCounterexample $answer))
!(let $answer (type:must (only) Nat 1000)
   (EvaluatedMustAll $answer))
!(let $answer (type:must (superpose ()) Nat 1000)
   (EvaluatedMustEmpty $answer))

; Nine independent types for each component create 81 structural tuple types
; and 81 applicability environments. Both frontiers grow dynamically: neither
; the ordinary nor explicitly producer-bounded syntax truncates them.
(: cap-x X0)
(: cap-x X1)
(: cap-x X2)
(: cap-x X3)
(: cap-x X4)
(: cap-x X5)
(: cap-x X6)
(: cap-x X7)
(: cap-x X8)
(: cap-y Y0)
(: cap-y Y1)
(: cap-y Y2)
(: cap-y Y3)
(: cap-y Y4)
(: cap-y Y5)
(: cap-y Y6)
(: cap-y Y7)
(: cap-y Y8)
!(let $types (reify (type:of (cap-x cap-y)))
   (DynamicTypeStorage $types))
(: cap-pairer (-> $A $B Nat))
(type-scheme cap-pairer)
(= (cap-pairer $x $y) Z)
!(let $answer (type:must (cap-pairer cap-x cap-y) Nat 20000)
   (DynamicApplicability $answer))
!(let $answer (type:must (cap-pairer cap-x cap-y) Nat)
   (DynamicApplicabilityDefault $answer))

; Finite structural formation, synthesis, checking, and refinement are observed
; but never failed by a producer budget. Evaluation and other potentially
; partial producers refine only toward determinate results as that explicit
; budget increases.
!(let $answer (type:formed Nat 1) (FormLow $answer))
!(let $answer (type:formed Nat 100) (FormHigh $answer))
!(let $answer (type:of Z 1) (SynthLow $answer))
!(let $answer (type:of Z 100) (SynthHigh $answer))
!(let $answer (type:check Z Nat 1) (CheckLow $answer))
!(let $answer (type:check Z Nat 100) (CheckHigh $answer))
!(let $answer (type:refine Nat 1) (RefineLow $answer))
!(let $answer (type:refine Nat 100) (RefineHigh $answer))
!(let $answer (type:may (only) Nat 1) (MayLow $answer))
!(let $answer (type:may (only) Nat 100) (MayHigh $answer))

; Structural conversion is not charged against the optional producer budget,
; so increasing that budget leaves its determinate verdict stable.
!(let $answer (type:eq (+ 1 1) 2 1) (ConvertLow $answer))
!(let $answer (type:eq (+ 1 1) 2 100) (ConvertHigh $answer))
!(let $answer (type:eq (+ 1 1) 2 200) (ConvertHigher $answer))

; A user marker makes computation available to the experimental he-prime
; layer, but does not admit that equation into Prime's conversion relation.
(= (user-normal $x) TagA)
(type-level-function user-normal)
!(let $answer (type:eq (user-normal q) TagA 1000)
   (ProvisionalUserRule $answer))

; Z is immediate; the Bool counterexample lies beyond the small explicit
; evaluation budget. A truncated answer frontier is Incomplete, while a
; sufficient caller-selected budget exposes the counterexample.
(= (delayed-value $n)
   (if (== $n 0) Truth (delayed-value (- $n 1))))
(= (delayed-answer)
   (superpose (Z (delayed-value 600))))
!(let $answer (type:must (delayed-answer) Nat 520)
   (DelayedLow $answer))
!(let $answer (type:must (delayed-answer) Nat 20000)
   (DelayedHigh $answer))
; PrimeDef is a structured package. Its nucleus and each added semantic
; component are separate fields rather than a prose descriptor.
!(prime-def-roots (prime-package &self))
!(prime-order-and-resources (prime-package &self))
; Negative control: malformed package requests are rejected.
!(prime-package &self extra)
