; A let whose pattern has a variable head.  PeTTa dispatches such a pattern
; at run time: a head first bound by the pattern itself keeps it data, so
; the let destructures; a head the destination binds to a function makes it
; a call.  Every route answers as the reference does.
(= (inc $x) (+ $x 1))
(= (zf) 5)
(= (lpfirst $s) (let ($h $t) $s $h))
(= (lprest $s) (let ($h $t) $s $t))
(= (lponly $s) (let ($h) $s $h))
(= (lpinner $s) (let (($a $b) $c) $s $b))
(= (lpboth $s) (let* ((($x $y) $s) ($z $y)) ($x $z)))
(= (lpmem $xs) (if (== $xs ()) (empty) (let ($h $t) (decons-atom $xs) $h)))
(= (lpmem $xs) (if (== $xs ()) (empty) (let ($h $t) (decons-atom $xs) (lpmem $t))))
(= (mk-inc) (cons-atom inc (7)))
(= (mk-num) (cons-atom 7 (1)))
(= (mk-zf) (cons-atom zf ()))
; Fresh heads destructure.
!(collapse (lpfirst (a b)))
!(collapse (lprest (a b)))
!(collapse (lpinner ((a b) c)))
!(collapse (lpboth (p q)))
!(sort-atom (collapse (lpmem (a b c))))
; Wrong lengths and non-expressions give no answer.
!(collapse (lpfirst (a b c)))
!(collapse (lpfirst x))
!(collapse (lponly (a b)))
; A destination that is data leaves the pattern data.
!(collapse (let b (lpmem (a b c)) yes))
!(collapse (let 7 (lpfirst (mk-num)) yes))
!(collapse (let $v (lpfirst (mk-inc)) $v))
; A destination naming a function makes the pattern a call.
!(collapse (let inc (lpfirst (mk-inc)) yes))
!(collapse (let zf (lponly (mk-zf)) yes))
!(collapse (let foo (lpfirst (mk-inc)) yes))
; A head bound before the let, then specialized to a function, still makes
; the pattern a run-time dispatch: an error raised there fails that path
; alone, and a head that names no function keeps the pattern data.
(= (lppat $p $s) (let ($p $q) $s $q))
!(collapse (lppat inc 8))
!(collapse (superpose ((lppat inc 8) ok)))
!(collapse (lppat foo (foo 8)))
