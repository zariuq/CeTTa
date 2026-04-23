; Distillation of the remaining full factorial miss after tailcall-decr:
; recursive argument flow now works, but composing over the recursive result
; in a larger value expression still goes empty.

(= (rec-prod $n)
   (if (== $n 0)
       1
       (* 2 (rec-prod (- $n 1)))))

!(assertEqualToResult (rec-prod 1) (2))
