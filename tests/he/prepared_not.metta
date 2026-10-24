; `not` in a deterministic function body runs in the prepared machine.  A
; truth value is negated there; any other argument leaves the call to the
; canonical evaluator, which answers as the reference does.
(= (flip $x) (not $x))
(= (nand $a $b) (not (and $a $b)))
(= (count-up $n $limit $acc)
   (if (not (< $n $limit)) $acc (count-up (+ $n 1) $limit (+ $acc 1))))
(= (twice $x) (not (not $x)))
!(flip True)
!(flip False)
!(nand True True)
!(nand True False)
!(nand False False)
!(count-up 0 1000 0)
!(twice True)
!(flip foo)
!(flip (a b))
; HE stops at an error result, so the type error comes last.
!(flip 5)
