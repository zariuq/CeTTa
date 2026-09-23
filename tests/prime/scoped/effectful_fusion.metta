; Pure map fusion does not justify effectful fusion.  With effect-free maps
; the two evaluation orders agree on the value; with a logging map they agree
; on the value and disagree on the observable order of effects, so a
; rewrite that fused them would change what an observer sees.
!(bind! &log (new-space))
(= (map $h ()) ())
(= (map $h ($x . $xs)) (let $v ($h $x) ($v . (map $h $xs))))
(= (map-comp $h $k ()) ())
(= (map-comp $h $k ($x . $xs)) (let $u ($k $x) (let $v ($h $u) ($v . (map-comp $h $k $xs)))))
(= (inc $n) (+ $n 1))
(= (dbl $n) (* $n 2))
; pure: same value either way
!(map inc (map dbl (1 . (2 . ()))))
!(map-comp inc dbl (1 . (2 . ())))
; logging maps: the value still agrees...
(= (inc! $n) (let $_ (add-atom &log (step inc $n)) (+ $n 1)))
(= (dbl! $n) (let $_ (add-atom &log (step dbl $n)) (* $n 2)))
!(map inc! (map dbl! (1 . (2 . ()))))
!(collapse (match &log (step $f $n) (logged $f $n)))
!(let $_ (collapse (match &log (step $f $n) (remove-atom &log (step $f $n)))) done)
!(map-comp inc! dbl! (1 . (2 . ())))
!(collapse (match &log (step $f $n) (logged $f $n)))
