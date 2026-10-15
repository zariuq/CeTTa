; let* binds in sequence, as nested lets, wherever a pure call is compiled:
; each value sees the bindings before it, patterns destructure, and a value
; with several answers or a stuck operation gives the same answers as the
; evaluator's nested lets. A name bound twice is unified, not rebound.
(= (seq $n) (let* (($a $n) ($b (+ $a 1)) ($c (* $a $b))) $c))
(= (pair-of $n) ($n (+ $n 1)))
(= (destructure $n) (let* ((($p $q) (pair-of $n)) ($s (+ $p $q))) $s))
(= (empty-bindings $n) (let* () (+ $n 5)))
(= (several $n) (let* (($a (superpose (1 2))) ($b (+ $a $n))) $b))
(= (stuck $n) (let* (($a (+ x $n))) $a))
(= (nested $n) (let* (($a (let* (($b (+ $n 1))) (* $b 2))) ($c (+ $a 1))) $c))
(= (depth $n) (if (== $n 0) 0 (let* (($x (depth (- $n 1))) ($y (+ $x 1))) $y)))
(= (sum-to $n $acc) (if (== $n 0) $acc (let* (($m (- $n 1)) ($a (+ $acc $n))) (sum-to $m $a))))
(= (same-twice $n) (let* (($x $n) ($x $n)) (ok $x)))
!(seq 3)
!(destructure 5)
!(empty-bindings 7)
!(collapse (several 10))
!(stuck 2)
!(nested 3)
!(depth 2000)
!(sum-to 100 0)
!(same-twice 4)
; Negative cases: a pattern that does not match, and a name bound twice to
; different values, give no answer.
(= (mismatch $n) (let* (((a $q) (b $n))) $q))
(= (rebind $n) (let* (($x $n) ($x (+ $x 1))) $x))
!(collapse (mismatch 6))
!(collapse (rebind 4))
