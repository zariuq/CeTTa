; A let whose pattern is an expression headed by a variable, inside a
; relation.  PeTTa evaluates a let's pattern and unifies its value with the
; let's value; a pattern headed by a variable no earlier binding gives a
; value is data, and evaluates to itself (SWI-PeTTa's answers).

(= (f $x) (+ $x 1))
(= (g) (5 6))
(= (split-list $l) (let ($h $t) $l (Got $h $t)))
(= (split-call) (let ($h $t) (g) (Got $h $t)))
(= (split-number) (let ($h $t) (f 2) (Got $h $t)))
(= (bound-head $v) (let $h f (let ($h 2) $v (Bound $h))))
(= (deep $x) (let ($h ($a $b)) $x (Deep $h $a $b)))
(= (each) (let ($h $t) (superpose ((1 2) (3 4))) (Pair $t $h)))
(= (walk $l) (case $l ((() done) ($else (let ($h $t) (decons-atom $l) (cons-atom $h (walk $t)))))))
(= (sequential $l)
   (let* (($first $l) (($h $t) $first) ($n (+ $h 1))) (Seq $n $t)))
(= (known $h $v) (let ($h $t) $v (Got $t)))

!(split-list (1 2))
!(split-list (x (y z)))
!(collapse (split-list (1 2 3)))
!(split-call)
!(collapse (split-number))
!(bound-head 3)
!(collapse (bound-head 4))
!(deep (x (y z)))
!(collapse (deep (x y)))
!(msort (collapse (each)))
!(walk (a b c))
!(sequential (4 rest))
!(known a (a b))
!(collapse (known a (c b)))
