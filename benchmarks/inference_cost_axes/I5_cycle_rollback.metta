; Matching the same caller variable against a slot and a constructor of that
; slot exposes a genuine substitution cycle.  Every candidate must be rejected
; and its observer-free writes restored before the next iteration.
(= (cycle-match (Cycle $x (Wrap $x))) ok)
(= (cycle-run $n $outside)
   (if (== $n 0)
       cycle-done
       (let $_ (collapse (cycle-match (Cycle $outside $outside)))
         (cycle-run (- $n 1) $outside))))

!(cycle-run 50000 $outside)
