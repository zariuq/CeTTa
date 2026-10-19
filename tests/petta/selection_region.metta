; Equations, case arms, typed calls and superpose select their candidates
; alike: an arm or equation its argument refutes by structure is skipped, and
; the last possible candidate keeps no alternative.

; Disjoint arms, then a catch-all.
(= (kind $x) (case $x ((LNil empty) ((LCons $h $t) (head $h)) ($other (other $other)))))
!(kind LNil)
!(kind (LCons 1 LNil))
!(kind (Foo bar))
!(kind 7)

; Arms whose roots agree and that differ below: the attempt decides.
(= (phase $m) (case $m (((Machine Running $s) (running $s)) ((Machine Done $s) (done $s)))))
!(phase (Machine Running 1))
!(phase (Machine Done 2))
!(collapse (phase (Machine Paused 3)))

; The first matching arm commits, even when its body has no answer.
(= (commit $x) (case $x (((A $y) (empty)) ($other fallback))))
!(collapse (commit (A 1)))
!(commit B)

; A repeated variable survives the structural test; unification refuses it.
(= (same $p) (case $p (((Pair $x $x) (equal $x)) ((Pair $x $y) (differ $x $y)))))
!(same (Pair a a))
!(same (Pair a b))

; Equations: every matching head answers, in order.
(= (color red) warm)
(= (color $any) any)
(= (color blue) cold)
!(collapse (color red))
!(collapse (color blue))
!(collapse (color green))

; A declared function whose arguments are data: one signature, nothing evaluated.
(: quoted (-> Atom Atom %Undefined%))
(= (quoted $x $y) (pair $x $y))
!(quoted (+ 1 2) (kind LNil))
(: count-to (-> Atom Atom %Undefined%))
(= (count-to $n $acc) (if (== $n 0) $acc (let* (($m (- $n 1)) ($next (LCons $n $acc))) (count-to $m $next))))
!(count-to 3 LNil)

; superpose in a tail position and under a let.
(= (both $x) (superpose ($x (S $x))))
!(collapse (both 1))
(= (paired $x) (let $y (superpose (a b)) (pair $x $y)))
!(collapse (paired 1))
