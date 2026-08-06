; Interleaved program execution and exact-fact mutation must not rebuild the
; full native tuple projection on every mutation.  Equation and type dispatch
; descriptors are derived from stable occurrence ids; exact fact mutations do
; not invalidate them.  The final strict count also proves that the default
; indexed cursor remains reachable after the interleaved workload.
(= (load $n)
   (if (== $n 0)
       done
       (let $ig (add-atom &self (edge $n $n))
            (load (- $n 1)))))
!(load 2000)
(= (qloop $n $acc)
   (if (== $n 0)
       $acc
       (qloop (- $n 1) (+ $acc (match &self (edge 500 $x) 1)))))
!(qloop 100 0)
!(assertEqualToResult
  (size
    (collapse
      (match &self (edge 500 $x) hit)))
  (1))
; expected: [done], [100], [()]
