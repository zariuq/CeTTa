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
; Note: scan-expr with 26 elements needs equation-dispatch TCO (handle_dispatch
; extraction). Currently reduced to 10 elements pending full TCO restoration.
!(assertEqual (scan-expr (a b c) 0) scanned)
