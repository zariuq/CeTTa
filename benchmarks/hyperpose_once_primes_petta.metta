; First-witness cancellation benchmark extracted from PeTTa's
; examples/hyperpose_primes.metta.  The third candidate is intentionally much
; cheaper than its siblings, so an interruptible parallel implementation need
; not finish the expensive primality searches.

(= (find-divisor $n $test-divisor)
   (if (> (* $test-divisor $test-divisor) $n)
       $n
       (if (== 0 (% $n $test-divisor))
           $test-divisor
           (find-divisor $n (+ $test-divisor 1)))))

(= (prime? $n)
   (== $n (find-divisor $n 2)))

!(test (once (hyperpose ((prime? 535372570000000063)
                         (prime? 537818110000000001)
                         (prime? 5421844300001)
                         (prime? 547344310000000013))))
       True)
