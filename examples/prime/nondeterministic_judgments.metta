; May and Must describe a nondeterministic answer bag without flattening its
; completion evidence into the answers themselves.

(: Nat Type)
(: Bool Type)
(: Z Nat)
(: Truth Bool)

(= (choose) Z)
(= (choose) Truth)
(= (only) Z)

(= (show-status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))

!(show-status MayWitness
   (prime-judge &self (May (PrimeEvaluate (choose)) Nat)))
!(show-status MustCounterexample
   (prime-judge &self (Must (PrimeEvaluate (choose)) Nat)))
!(show-status MustAll
   (prime-judge &self (Must (PrimeEvaluate (only)) Nat)))
!(show-status MustEmpty
   (prime-judge &self
     (Must (PrimeEvaluate (superpose ())) Nat)))
