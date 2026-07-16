; Prime 0.2 budget-monotonicity observations.

(: Nat Type)
(: Bool Type)
(: Query Type)
(: Z Nat)
(: Truth Bool)
(: q Query)

(= (budget-observation $label $budget
      (PrimeVerdict $status $judgment $evidence))
   (BudgetObservation $label $budget $status))

(= (choose) Z)
(= (choose) Truth)
(= (only) Z)

(= (delayed-value $n)
   (if (== $n 0) Truth (delayed-value (- $n 1))))
(= (delayed-answer)
   (superpose (Z (delayed-value 600))))

; Each label is sampled at increasing caller-declared producer budgets. The
; external checker enforces that once a determinate status appears, every later
; sample preserves it. Finite structural judgments remain independent of this
; control; partial producers exercise refinement in both polarities.
!(budget-observation OpenExpected 1
   (prime-judge &self (Check Z $Expected) 1))
!(budget-observation OpenExpected 8
   (prime-judge &self (Check Z $Expected) 8))
!(budget-observation OpenExpected 64
   (prime-judge &self (Check Z $Expected) 64))
!(budget-observation OpenExpected 256
   (prime-judge &self (Check Z $Expected) 256))

!(budget-observation FormNat 1
   (prime-judge &self (Form Nat) 1))
!(budget-observation FormNat 8
   (prime-judge &self (Form Nat) 8))
!(budget-observation FormNat 64
   (prime-judge &self (Form Nat) 64))
!(budget-observation FormNat 256
   (prime-judge &self (Form Nat) 256))

!(budget-observation SynthZ 1
   (prime-judge &self (Synth Z) 1))
!(budget-observation SynthZ 8
   (prime-judge &self (Synth Z) 8))
!(budget-observation SynthZ 64
   (prime-judge &self (Synth Z) 64))
!(budget-observation SynthZ 256
   (prime-judge &self (Synth Z) 256))

!(budget-observation CheckZ 1
   (prime-judge &self (Check Z Nat) 1))
!(budget-observation CheckZ 8
   (prime-judge &self (Check Z Nat) 8))
!(budget-observation CheckZ 64
   (prime-judge &self (Check Z Nat) 64))
!(budget-observation CheckZ 256
   (prime-judge &self (Check Z Nat) 256))

!(budget-observation RejectTruth 1
   (prime-judge &self (Check Truth Nat) 1))
!(budget-observation RejectTruth 8
   (prime-judge &self (Check Truth Nat) 8))
!(budget-observation RejectTruth 64
   (prime-judge &self (Check Truth Nat) 64))
!(budget-observation RejectTruth 256
   (prime-judge &self (Check Truth Nat) 256))

!(budget-observation RefineNat 1
   (prime-judge &self (Refine Nat) 1))
!(budget-observation RefineNat 8
   (prime-judge &self (Refine Nat) 8))
!(budget-observation RefineNat 64
   (prime-judge &self (Refine Nat) 64))
!(budget-observation RefineNat 256
   (prime-judge &self (Refine Nat) 256))

!(budget-observation MayOnly 1
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 1))
!(budget-observation MayOnly 8
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 8))
!(budget-observation MayOnly 64
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 64))
!(budget-observation MayOnly 256
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 256))

!(budget-observation MustChoose 1
   (prime-judge &self (Must (PrimeEvaluate (choose)) Nat) 1))
!(budget-observation MustChoose 8
   (prime-judge &self (Must (PrimeEvaluate (choose)) Nat) 8))
!(budget-observation MustChoose 64
   (prime-judge &self (Must (PrimeEvaluate (choose)) Nat) 64))
!(budget-observation MustChoose 256
   (prime-judge &self (Must (PrimeEvaluate (choose)) Nat) 256))

!(budget-observation ConvertArithmetic 1
   (prime-judge &self (Convert (+ 1 1) 2) 1))
!(budget-observation ConvertArithmetic 8
   (prime-judge &self (Convert (+ 1 1) 2) 8))
!(budget-observation ConvertArithmetic 64
   (prime-judge &self (Convert (+ 1 1) 2) 64))
!(budget-observation ConvertArithmetic 256
   (prime-judge &self (Convert (+ 1 1) 2) 256))

; This family refines from resource-incomplete to a witnessed counterexample.
!(budget-observation DelayedCounterexample 100
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 100))
!(budget-observation DelayedCounterexample 520
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 520))
!(budget-observation DelayedCounterexample 2000
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 2000))
!(budget-observation DelayedCounterexample 20000
   (prime-judge &self
     (Must (PrimeEvaluate (delayed-answer)) Nat) 20000))
