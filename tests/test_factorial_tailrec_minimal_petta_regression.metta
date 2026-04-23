; Distillation of upstream factorial.metta:
; plain if/arithmetic work, and recursion to a closed constant works,
; but tail recursion whose next argument depends on the current binder
; currently misses.

(= (tailcall-const $n)
   (if (== $n 0)
       ok
       (tailcall-const 0)))

!(assertEqualToResult (tailcall-const 1) (ok))

(= (tailcall-decr $n)
   (if (== $n 0)
       ok
       (tailcall-decr (- $n 1))))

!(assertEqualToResult (tailcall-decr 1) (ok))
