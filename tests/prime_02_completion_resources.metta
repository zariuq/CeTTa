; Prime 0.2 completion polarity and aggregate-resource fixtures.

(: Nat Type)
(: Bool Type)
(: Query Type)
(: Z Nat)
(: One Nat)
(: Truth Bool)
(: q Query)

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
(= (prime-order-and-resources
      (PrimeSemanticPackageV0
        $identity $syntax $judgments $results $information $resources
        $typing $runtime $nondeterminism $trust))
   ($information $resources))

; Producer-written Complete is not closure evidence. Prefix witnesses and
; counterexamples remain valid; universal or absence claims do not.
!(prime-labeled-status RawMayWitness
   (prime-judge &self
     (May (PrimeAnswers Complete (PrimeValue Z)) Nat) 1000))
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

; Check, Refine, and May share the same aggregate ledger and refine only from
; indeterminate to determinate as budget increases.
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

; Increasing the declared budget refines an incomplete conversion and never
; removes the established verdict once reached.
!(prime-labeled-status ConvertLow
   (prime-judge &self (Convert (+ 1 1) 2) 1))
!(prime-labeled-status ConvertHigh
   (prime-judge &self (Convert (+ 1 1) 2) 100))
!(prime-labeled-status ConvertHigher
   (prime-judge &self (Convert (+ 1 1) 2) 200))

; TagA is immediate; TagB is deliberately beyond the former internal cap.
; A truncated prefix is Incomplete, while sufficient budget exposes ambiguity.
(= (delayed-tag $n)
   (if (== $n 0) TagB (delayed-tag (- $n 1))))
(: delayed-source (-> Query Atom))
(= (delayed-source $x)
   (superpose (TagA (delayed-tag 600))))
(type-level-function delayed-source)
(type-level-function superpose)
!(prime-labeled-status DelayedLow
   (prime-judge &self (Convert (delayed-source q) TagA) 520))
!(prime-labeled-status DelayedHigh
   (prime-judge &self (Convert (delayed-source q) TagA) 20000))
!(prime-information
   (prime-judge &self (Convert (delayed-source q) TagA) 520))
!(prime-information
   (prime-judge &self (Convert (+ 1 1) 2) 100))

; The ledger is part of every well-formed judgment verdict.
!(prime-ledger (prime-judge &self (Check Z Nat) 1000))
!(prime-order-and-resources (prime-package &self))
