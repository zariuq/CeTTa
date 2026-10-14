; A value handed on unevaluated can reach a pattern that takes it apart.
; `unify`, `let` and `case` then demand the parts their pattern inspects,
; as an equation head does, and leave the parts a pattern variable binds.
(= (foo) bar)

; The value is (w (pair $v (foo))), whose pair was passed on unevaluated.
(= (check-unify $d $v) (unify $d (w (pair $x $t)) (noreduce-eq $x $v) no))
(= (check-let $d $v) (let (w (pair $x $t)) $d (noreduce-eq $x $v)))
(= (check-case $d $v)
   (case $d (((w (pair $x $t)) (noreduce-eq $x $v)) ($other (other $other)))))
(= (check-head (w (pair $x $t)) $v) (noreduce-eq $x $v))
(= (via-unify $c $v) (check-unify (w $c) $v))
(= (via-let $c $v) (check-let (w $c) $v))
(= (via-case $c $v) (check-case (w $c) $v))
(= (via-head $c $v) (check-head (w $c) $v))
!(via-unify (pair $q (foo)) $q)
!(via-let (pair $q (foo)) $q)
!(via-case (pair $q (foo)) $q)
!(via-head (pair $q (foo)) $q)

; The parts that are bound keep their values.
(= (parts-unify $d) (unify $d (w (pair $x $t)) (got $x $t) no))
(= (parts-let $d) (let (w (pair $x $t)) $d (got $x $t)))
(= (parts-case $d) (case $d (((w (pair $x $t)) (got $x $t)) ($o (other $o)))))
!(parts-unify (w (pair a (foo))))
!(parts-let (w (pair a (foo))))
!(parts-case (w (pair a (foo))))

; A literal inside the pattern demands its own position too.
(= (literal-case $d) (case $d (((w (pair $x bar)) (bar-at $x)) ($o none))))
(= (via-literal $c) (literal-case (w $c)))
!(via-literal (pair a (foo)))

; A part a pattern variable binds is never demanded: its computation here
; would not end.
(= (loop) (loop))
(= (lazy-case $d) (case $d (((w (pair $x $t)) (first $x)) ($o none))))
(= (via-lazy $c) (lazy-case (w $c)))
!(via-lazy (pair a (loop)))

; A demanded part with several values gives a branch for each.
(= (two) (superpose (bar baz)))
(= (branch-case $d) (case $d (((w (pair $x bar)) (is-bar $x)) ($o (not-bar $o)))))
(= (via-branch $c) (branch-case (w $c)))
!(via-branch (pair a (two)))
