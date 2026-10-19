; A filter whose condition is written in place keeps that condition's plan
; for every item still waiting to be tested.  Conditions that allocate make
; the machine collect its heap while items wait; each item is still tested
; with its own condition, and every kept item is kept.
(= (upto $n $acc) (if (== $n 0) $acc (upto (- $n 1) (cons-atom $n $acc))))
(= (heavy $x) (let $l (upto 40 ()) (> (+ $x (size-atom $l)) 0)))
(= (third $x) (let* (($l (upto 30 ())) ($m (size-atom $l))) (== (% (+ $x $m) 3) 0)))
!(size-atom (filter-atom (upto 2000 ()) $x (heavy $x)))
!(size-atom (filter-atom (upto 3000 ()) $x (third $x)))
!(filter-atom (upto 9 ()) $x (third $x))
