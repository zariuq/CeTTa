; Prime keeps proof-grade checking separate from open-world analysis.

(: Nat Type)
(: Bool Type)
(: Z Nat)
(: Truth Bool)
(: unknown %Undefined%)

(= (show-status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))

!(show-status ExactPositive
   (prime-judge &self (Check Z Nat)))
!(show-status ExactNegative
   (prime-judge &self (Check Truth Nat)))
!(show-status GradualAnalyze
   (prime-judge &self (Analyze unknown Nat)))
!(show-status GradualCheck
   (prime-judge &self (Check unknown Nat)))
