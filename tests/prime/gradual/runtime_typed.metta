(: prime-grad:twice (-> Number Number))
(: prime-grad:choose (-> Number))

(= (prime-grad:twice $x) (+ $x $x))
(= (prime-grad:choose) 1)
(= (prime-grad:choose) 1)
(= (prime-grad:choose) 2)

!(prime-grad:twice 21)
!(reify (prime-grad:choose))
!(prime-judge &self
   (Must (PrimeEvaluate (prime-grad:choose)) Number))
!(prime-judge &self
   (May (PrimeAnswers Complete
          (PrimeFailure branch-fault)
          (PrimeValue 7))
        Number))
!(prime-judge &self
   (Must (PrimeAnswers Complete
           (PrimeFailure branch-fault)
           (PrimeValue 7))
         Number))

!(bind! &prime-grad:state (new-space))
!(add-atom &prime-grad:state (seen alpha))
!(add-atom &prime-grad:state (seen beta))
!(reify (match &prime-grad:state (seen $x) $x))
!(remove-atom &prime-grad:state (seen beta))
!(reify (match &prime-grad:state (seen $x) $x))
