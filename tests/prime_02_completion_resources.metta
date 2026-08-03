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

(= (prime-status (PrimeVerdict $status $judgment $evidence)) $status)
(= (prime-labeled-status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))
(= (prime-information
      (PrimeVerdict $status $judgment
        (PrimeEvidenceV1 $evidence (InformationClass $class) $ledger)))
   $class)
(= (prime-ledger
      (PrimeVerdict $status $judgment
        (PrimeEvidenceV1 $evidence $information $ledger)))
   $ledger)
(= (stable-prime-ledger
      (ResourceLedgerV1 $mode $initial $spent $remaining $work
        (DeclaredLimits $depth $types $applicability
          (evaluator-stack-budget-bytes $bytes))
        (Observed $max-depth $type-capacity
          $stack-capacity $applicability-capacity)))
   (ResourceLedgerV1 $mode $initial $spent $remaining $work
     (DeclaredLimits $depth $types $applicability
       (evaluator-stack-budget-bytes RuntimeDeclared))
     (Observed $max-depth $type-capacity
       $stack-capacity $applicability-capacity)))
(= (prime-resource-mode
      (PrimeVerdict Established $judgment
        (MayWitness $index $value $type $edge)))
   NoStepBound)
(= (prime-resource-mode
      (PrimeVerdict $status $judgment
        (PrimeEvidenceV1 $evidence $information $ledger)))
   ExplicitProducerBound)
(= (prime-order-and-resources
      (PrimeDefV1
        $identity $language $contexts $typing
        (ResultAlgebraV1 $results $information $nondeterminism)
        (EffectsAndResourcesV1 $effects $resources)
        $evidence))
   ($information $resources))
(= (prime-def-roots
      (PrimeDefV1
        $identity
        (LanguageDefV1 $syntax $binders $equations $rewrites)
        (ContextsAndSchemesV1
          $classes $markers $no-generalization $fresh $rigid $substitutions)
        (TypingRulesV1
          $judgments $edges $checked $telescopes $bidirectional $convert
          $refinements)
        (ResultAlgebraV1 $results $information $nondeterminism)
        (EffectsAndResourcesV1 $effects $resources)
        (EvidenceSchemaV1
          $verdict $prime-evidence $ledger $answer $substitution $producer
          $recheck $normal-forms $correspondence $source-identity)))
   (PrimeDefRoots
     LanguageDefV1 ContextsAndSchemesV1 TypingRulesV1 ResultAlgebraV1
     EffectsAndResourcesV1 EvidenceSchemaV1))

; Closed judgments never solve rigid or out-of-scope variables silently.
!(prime-labeled-status OpenExpected
   (prime-judge &self (Check Z $Expected) 1000))
!(prime-labeled-status UnconstrainedDeclaration
   (prime-judge &self (Check unconstrained Bool) 1000))
!(prime-labeled-status EscapedBinder
   (prime-judge &self (Form (-> (: $x Nat) $outside)) 1000))
!(prime-labeled-status UniverseDeferred
   (prime-judge &self (Form (Type 1)) 1000))
!(prime-labeled-status CastDeferred
   (prime-judge &self (Cast Z Nat) 1000))
!(prime-labeled-status AdmissionDeferred
   (prime-judge &self (AdmitTypeRule candidate-rule) 1000))
; Analyze keeps success-typing consistency separate from exact/structural
; checking at a gradual boundary.
!(prime-labeled-status AnalyzeDynamic
   (prime-judge &self (Analyze dynamic-term Nat) 1000))
!(prime-labeled-status CheckDynamic
   (prime-judge &self (Check dynamic-term Nat) 1000))
; The ordinary surface is genuinely unbounded. This terminating evaluation
; takes more than 100000 evaluator steps; the explicit bounded form is the
; only one that reports resource incompleteness.
(= (long-default $n)
   (if (<= $n 0) done (long-default (- $n 1))))
!(prime-labeled-status UnboundedDefault
   (prime-judge &self
     (May (PrimeEvaluate (long-default 30000)) Done)))
!(prime-labeled-status ExplicitBound
   (prime-judge &self
     (May (PrimeEvaluate (long-default 30000)) Done) 100000))
!(prime-resource-mode
   (prime-judge &self
     (May (PrimeEvaluate (long-default 30000)) Done)))
!(prime-resource-mode
   (prime-judge &self
     (May (PrimeEvaluate (long-default 30000)) Done) 100000))

; Producer-written Complete is not closure evidence. Prefix witnesses and
; counterexamples remain valid; universal or absence claims do not.
!(prime-labeled-status RawMayWitness
   (prime-judge &self
     (May (PrimeAnswers Complete (PrimeValue Z)) Nat) 1000))
!(prime-labeled-status RawMayNoWitness
   (prime-judge &self
     (May (PrimeAnswers Complete (PrimeFailure not-found)) Nat) 1000))
!(prime-labeled-status RawMustUncertified
   (prime-judge &self
     (Must (PrimeAnswers Complete (PrimeValue Z)) Nat) 1000))
!(prime-labeled-status RawMustCounterexample
   (prime-judge &self
     (Must (PrimeAnswers Complete (PrimeValue Truth)) Nat) 1000))

; PrimeEvaluate obtains closure status from the evaluator rather than from
; producer-authored data.
(= (choose) Z)
(= (choose) Truth)
(= (only) Z)
!(prime-labeled-status EvaluatedMayWitness
   (prime-judge &self (May (PrimeEvaluate (choose)) Nat) 1000))
!(prime-labeled-status EvaluatedMustCounterexample
   (prime-judge &self (Must (PrimeEvaluate (choose)) Nat) 1000))
!(prime-labeled-status EvaluatedMustAll
   (prime-judge &self (Must (PrimeEvaluate (only)) Nat) 1000))
!(prime-labeled-status EvaluatedMustEmpty
   (prime-judge &self
     (Must (PrimeEvaluate (superpose ())) Nat) 1000))

; Nine independent types for each component create 81 structural tuple types
; and 81 applicability environments. Both frontiers grow dynamically: neither
; the ordinary nor explicitly producer-bounded surface truncates them.
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
!(prime-labeled-status DynamicTypeStorage
   (prime-judge &self (Synth (cap-x cap-y))))
(: cap-pairer (-> $A $B Nat))
(type-scheme cap-pairer)
(= (cap-pairer $x $y) Z)
!(prime-labeled-status DynamicApplicability
   (prime-judge &self
     (Must (PrimeEvaluate (cap-pairer cap-x cap-y)) Nat) 20000))
!(prime-labeled-status DynamicApplicabilityDefault
   (prime-judge &self
     (Must (PrimeEvaluate (cap-pairer cap-x cap-y)) Nat)))
!(stable-prime-ledger (prime-ledger
   (prime-judge &self
     (Must (PrimeEvaluate (cap-pairer cap-x cap-y)) Nat) 20000)))

; Finite structural formation, synthesis, checking, and refinement are observed
; but never failed by a producer budget. Evaluation and other potentially
; partial producers refine only toward determinate results as that explicit
; budget increases.
!(prime-labeled-status FormLow
   (prime-judge &self (Form Nat) 1))
!(prime-labeled-status FormHigh
   (prime-judge &self (Form Nat) 100))
!(prime-labeled-status SynthLow
   (prime-judge &self (Synth Z) 1))
!(prime-labeled-status SynthHigh
   (prime-judge &self (Synth Z) 100))
!(prime-labeled-status CheckLow
   (prime-judge &self (Check Z Nat) 1))
!(prime-labeled-status CheckHigh
   (prime-judge &self (Check Z Nat) 100))
!(prime-labeled-status RefineLow
   (prime-judge &self (Refine Nat) 1))
!(prime-labeled-status RefineHigh
   (prime-judge &self (Refine Nat) 100))
!(prime-labeled-status MayLow
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 1))
!(prime-labeled-status MayHigh
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 100))

; Structural conversion is not charged against the optional producer budget,
; so increasing that budget leaves its determinate verdict stable.
!(prime-labeled-status ConvertLow
   (prime-judge &self (Convert (+ 1 1) 2) 1))
!(prime-labeled-status ConvertHigh
   (prime-judge &self (Convert (+ 1 1) 2) 100))
!(prime-labeled-status ConvertHigher
   (prime-judge &self (Convert (+ 1 1) 2) 200))

; A user marker makes computation available to the experimental he-prime
; layer, but does not admit that equation into Prime's conversion relation.
(= (user-normal $x) TagA)
(type-level-function user-normal)
!(prime-labeled-status ProvisionalUserRule
   (prime-judge &self (Convert (user-normal q) TagA) 1000))

; Z is immediate; the Bool counterexample lies beyond the small explicit
; evaluation budget. A truncated answer frontier is Incomplete, while a
; sufficient caller-selected budget exposes the counterexample.
(= (delayed-value $n)
   (if (== $n 0) Truth (delayed-value (- $n 1))))
(= (delayed-answer)
   (superpose (Z (delayed-value 600))))
!(prime-labeled-status DelayedLow
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 520))
!(prime-labeled-status DelayedHigh
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 20000))

; A counterexample is prefix-robust even when the producer admits that more
; answers remain. It refutes Must without trusting producer-authored closure.
!(prime-labeled-status PrefixCounterexample
   (prime-judge &self
     (Must (PrimeAnswers (Incomplete ongoing)
                         (PrimeValue Z) (PrimeValue Truth)) Nat) 2000))
!(prime-information
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 520))
!(prime-information
   (prime-judge &self (Convert (+ 1 1) 2) 100))

; A resource ledger is attached to explicitly bounded judgments.
; Type-annotation lookup buckets are keyed by spelling-stable symbol hashes,
; so these observations do not depend on which other language interned its
; symbols first.
!(stable-prime-ledger
   (prime-ledger (prime-judge &self (Check Z Nat) 1000)))
; PrimeDef is a structured package. Its nucleus and each added semantic
; component are separate fields rather than a prose descriptor.
!(prime-def-roots (prime-package &self))
!(prime-order-and-resources (prime-package &self))
; Negative control: malformed package requests are rejected.
!(prime-labeled-status PackageArity (prime-package &self extra))
