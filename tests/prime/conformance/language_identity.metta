(= (prime-contract:package-class
      (PrimeDefV3 $identity $language $contexts $checking $results $effects
                  $evidence))
   PrimePackage)
(= (prime-contract:package-class (prime-package $space))
   InertPrimeSyntax)

(= (prime-contract:type-class True)
   PrimeTypeJudgment)
(= (prime-contract:type-class (type:formed $type))
   InertPrimeSyntax)

!(prime-contract:package-class (prime-package &self))
!(let $result (type:formed Number)
   (prime-contract:type-class $result))
