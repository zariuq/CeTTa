; Caller variables remain open while rule-local slots are standardized apart.
; The authoritative orientation must preserve the caller's alias structure.
(= (alias-match (Alias (Pair $x $y) $x)) ok)
(= (alias-run $n $left $right)
   (if (== $n 0)
       alias-done
       (let $_ (alias-match (Alias (Pair $left $right) $left))
         (alias-run (- $n 1) $left $right))))

!(alias-run 50000 $left $right)
