(: prime-grad:twice (-> Number Number))
(= (prime-grad:twice $x) (+ $x $x))
(: prime-grad:wrong-result (-> String))
(= (prime-grad:wrong-result) 7)

; Applicable result contracts are alternatives for validation, not branches of
; computation: this effectful body must run once and Number must satisfy the
; overloaded Number-or-String contract set.
(: prime-grad:overloaded-result (-> Number))
(: prime-grad:overloaded-result (-> String))
!(bind! &prime-grad:effects (new-space))
(= (prime-grad:overloaded-result)
   (let $_receipt (add-atom &prime-grad:effects prime-grad:tick)
     7))

(= (prime-grad:status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))

!(prime-grad:status ValidAnnotation
   (prime-judge &self (Check (prime-grad:twice 21) Number)))
!(prime-grad:status InvalidAnnotation
   (prime-judge &self
     (Must (PrimeEvaluate (prime-grad:wrong-result)) String)))
!(prime-grad:overloaded-result)
!(count-atoms &prime-grad:effects)
; Keep the intentional type error last: top-level errors terminate the
; remaining directive stream.
!(prime-grad:wrong-result)
