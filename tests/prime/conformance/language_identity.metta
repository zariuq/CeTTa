(= (prime-contract:package-class
      (PrimeDefV1 $identity $language $contexts $typing $results $effects
                  $evidence))
   PrimePackage)
(= (prime-contract:package-class (prime-package $space))
   InertPrimeSyntax)

(= (prime-contract:judgment-class
      (PrimeVerdict $status $judgment $evidence))
   (PrimeJudgment $status))
(= (prime-contract:judgment-class (prime-judge $space $judgment))
   InertPrimeSyntax)

!(prime-contract:package-class (prime-package &self))
!(prime-contract:judgment-class (prime-judge &self (Form Number)))
