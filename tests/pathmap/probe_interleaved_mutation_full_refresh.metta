; Interleaved program execution and exact-fact mutation must not rebuild the
; full native tuple projection on every mutation.  Equation and type dispatch
; descriptors are derived from stable occurrence ids; exact fact mutations do
; not invalidate them. The final count checks the workload's result; a
; separate owned-space binding probe exercises the indexed cursor afterward.
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

!(bind! &indexed-control (new-space pathmap))
!(add-atom &indexed-control (route-control reachable))
!(collapse (match &indexed-control (route-control $x) $x))
