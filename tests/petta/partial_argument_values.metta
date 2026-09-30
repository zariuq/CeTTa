; A partial application holds the values of its argument occurrences: a
; variable's value is held as it is and never run again, whatever calls its
; expression spells.
(= (keep $k $a $x) (got $k $a $x))
(= (my-apply $f $a $x) ($f $a $x))
!(let $k (quote (+ 1 2)) (my-apply (keep $k) a b))
!(let $k (quote (+ 1 2)) (keep $k))
!(let $k (quote (Q (+ 1 2))) ((keep $k) a b))
!(let $g (quote (CPU F (* 2 3) o)) (let $p (keep 7 $g) ($p z)))
!(let $k (quote (+ 1 2)) (collapse (foldl-atom (1 2) () $acc $x (cons-atom ((keep $k) $acc $x) ()))))
