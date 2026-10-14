; Lazy nested maps and fused maps agree here on values and effect order:
; each observed head demands the matching inner head, not its whole tail.
; Collecting the complete inner map first creates a different observation
; boundary. Fusing across that boundary would change the logged order.
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
; Lazy logging maps: the two programs interleave the same effects.
(= (inc! $n) (let $_ (add-atom &log (step inc $n)) (+ $n 1)))
(= (dbl! $n) (let $_ (add-atom &log (step dbl $n)) (* $n 2)))
!(map inc! (map dbl! (1 . (2 . ()))))
!(collapse (match &log (step $f $n) (logged $f $n)))
!(let $_ (collapse (match &log (step $f $n) (remove-atom &log (step $f $n)))) done)
!(map-comp inc! dbl! (1 . (2 . ())))
!(collapse (match &log (step $f $n) (logged $f $n)))
; An explicit collection observes every inner field before the outer map.
; The values still agree, but both dbl effects now precede both inc effects.
!(let $_ (collapse (match &log (step $f $n) (remove-atom &log (step $f $n)))) done)
!(let $rows (collapse (map dbl! (1 . (2 . ())))) (map inc! (car-atom $rows)))
!(collapse (match &log (step $f $n) (logged $f $n)))
