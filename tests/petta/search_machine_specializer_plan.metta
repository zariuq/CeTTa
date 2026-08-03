(= (plan-map $f ()) ())
(= (plan-map $f (cons $x $xs))
   (cons ($f $x) (plan-map $f $xs)))

(= (plan-range $n)
   (if (== $n 0)
       ()
       (cons $n (plan-range (- $n 1)))))

; A specialization must retain the source-occurrence plan.  The computed
; list is evaluated once; each matched tail is subsequently a value.
!(test
   (let $items (plan-map (+ 1) (plan-range 200))
        (length $items))
   200)

!(test
   (let $items (plan-map (+ 1) (plan-range 400))
        (length $items))
   400)

; Source list children are still computations.  Avoiding repeated evaluation
; of an already-produced tail must not make authored list construction lazy.
!(test (plan-map id (1 (+ 2 3))) (1 5))
