; test_tail_recursion.metta — tests that tail-recursive MeTTa programs
; don't overflow the C stack.  Without TCO in eval.c, these would segfault.

; Tail-recursive countdown
(= (countdown $n)
   (if (<= $n 0) done (countdown (- $n 1))))
!(assertEqual (countdown 50) done)

; Tail-recursive accumulator
(= (sum-to $n $acc)
   (if (<= $n 0) $acc (sum-to (- $n 1) (+ $acc $n))))
!(assertEqual (sum-to 20 0) 210)

; Nested let chain (tests let TCO specifically)
(= (process-chain $n)
   (if (<= $n 0) finished
     (let $next (- $n 1)
       (let $r (if (== (% $next 2) 0) even odd)
         (process-chain $next)))))
!(assertEqual (process-chain 30) finished)

; Tail-recursive iteration over expression indices
(= (scan-expr $expr $idx)
   (if (>= $idx (size-atom $expr)) scanned
     (let $tok (index-atom $expr $idx)
       (scan-expr $expr (+ $idx 1)))))
!(assertEqual
  (scan-expr (a b c d e f g h i j k l m n o p q r s t u v w x y z) 0)
  scanned)
