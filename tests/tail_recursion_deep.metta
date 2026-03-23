; Deep tail-recursion check. This is intentionally driven with --fuel so the
; trampoline is validated separately from the default evaluator budget.

(= (countdown $n)
   (if (<= $n 0) done (countdown (- $n 1))))
!(assertEqual (countdown 5000) done)

(= (sum-to $n $acc)
   (if (<= $n 0) $acc (sum-to (- $n 1) (+ $acc $n))))
!(assertEqual (sum-to 1000 0) 500500)
