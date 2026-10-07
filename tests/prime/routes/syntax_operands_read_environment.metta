; `unify`, `switch` and `match` read their operands through the current
; environment, as `let` reads its source.  A name the environment binds is
; its value there: a pattern cannot bind it again, a `match` finds only the
; rows that agree with it, and `unify` takes its `else` branch when the
; environment disagrees.  This file is run with and without --fuel; the
; answers must be the same on both routes, and they are the reference HE's.

(= (h $x) (unify $x 5 ok no))
(fact 5)
(fact 6)

; ---- After `(h $y)` the environment binds $y to 5.
!(let $r (h $y) (unify $y 6 yes no))
!(let $r (h $y) (switch $y ((6 six) (5 five))))
!(let $r (h $y) (match &self (fact $y) (found $y)))

; ---- A binding that agrees still matches.
!(let $r (h $y) (unify $y 5 yes no))
!(let $r (h $y) (switch $y ((5 five) (6 six))))
!(let $r (h $y) (switch-minimal $y ((6 six) (5 five))))
!(let $r (h $y) (switch (pair $y 6) (((pair 5 $w) (yes $w $y)) ($_ no))))

; ---- A name the environment does not bind still binds.
!(let $r (h $y) (unify $z 6 (yes $z) no))
!(let $r (h $y) (match &self (fact $z) (found $z)))
!(let $r (h $y) (switch (pair 5 6) (((pair $k $w) (yes $k $w)) ($_ no))))

; ---- What the environment holds is read as a value: a part an argument
; left to be computed is computed before it is compared, and a binding that
; would make a name contain itself through the environment is no unifier.
(= (g $x) $x)
(= (pair-of $x $v) (unify $x (pair $v) ok no))
!(let $r (pair-of $y (g 5)) (unify $y (pair 5) yes no))
!(let $r (pair-of $y (g 5)) (unify $y (pair 6) yes no))
(= (inside $x $z) (unify $x (f $z) ok no))
!(let $r (inside $y $x) (unify $x (g $y) yes no))

; ---- A branch pattern is read through the environment as well.
!(let $r (h $k) (switch 6 (($k yes) ($_ no))))
!(let $r (h $k) (switch (pair 6 6) (((pair $k $w) (yes $w)) ($_ no))))

; ---- What a switch's scrutinee binds is kept for its branch.
!(switch (h $q) ((ok (got $q)) (no none)))
!(let $r (switch (h $q) ((ok ok))) (Pair $q $r))

; ---- A match keeps the environment it ran in: the caller's variable is
; refined by the equation, and the answer says so.
(= (found-at $y) (unify $y 5 (match &self (fact $y) found) no))
!(let $r (found-at $w) (Pair $w $r))
(= (found-after $y) (let $r (h $y) (match &self (fact $y) found)))
!(let $r (found-after $w) (Pair $w $r))
(= (five-after $y) (let $r (h $y) (switch $y ((5 five)))))
!(let $r (five-after $w) (Pair $w $r))

; ---- The same inside equations: a `case` or a `match` body that the
; relational machine runs, and one it hands back to the evaluator.
(= (case-unify $n) (case $n ((0 (let $r (h $y) (unify $y 6 yes no))))))
(= (case-match $n) (case $n ((0 (let $r (h $y) (match &self (fact $y) (found $y)))))))
(= (case-switch $n) (case $n ((0 (let $r (h $y) (switch $y ((6 six) (5 five))))))))
(= (row-unify $n) (match &self (fact $n) (let $r (h $y) (unify $y 6 yes no))))
!(case-unify 0)
!(case-match 0)
!(case-switch 0)
!(row-unify 5)
