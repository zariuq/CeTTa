; Ordinary Prime judgments are unbounded and unmetered. The three-argument
; form is the explicit, inspectable bounded variant.

(: Nat Type)
(: Z Nat)
(= (only) Z)

(= (resource-mode
      (PrimeVerdict Established $judgment
        (MayWitness $index $value $type $edge)))
   NoStepBound)
(= (resource-mode
      (PrimeVerdict $status $judgment
        (PrimeEvidenceV1 $evidence $information $ledger)))
   ExplicitProducerBound)

!(resource-mode
   (prime-judge &self (May (PrimeEvaluate (only)) Nat)))
!(resource-mode
   (prime-judge &self (May (PrimeEvaluate (only)) Nat) 100))
