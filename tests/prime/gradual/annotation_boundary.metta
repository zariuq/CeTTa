(: prime-grad:twice (-> Number Number))
(= (prime-grad:twice $x) (+ $x $x))
(: prime-grad:wrong-result (-> String))
(= (prime-grad:wrong-result) 7)

(= (prime-grad:status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))

!(prime-grad:status ValidAnnotation
   (prime-judge &self (Check (prime-grad:twice 21) Number)))
!(prime-grad:status InvalidAnnotation
   (prime-judge &self
     (Must (PrimeEvaluate (prime-grad:wrong-result)) String)))
!(prime-grad:wrong-result)
