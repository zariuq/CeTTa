; Rows holding variables match a cell built while the program runs, which
; no row holds and nothing stored before: counting its live neighbours must
; still find the open rows that match it.
(neighbors ($q $r (S $s)) ($q (S $r) $s))
(neighbors ($q (S $r) $s) ($q $r (S $s)))
(alive (Z (S Z) Z))
(alive (Z Z (S (S Z))))
(= (live-count $c) (size-atom (collapse (match &self (, (neighbors $c $n) (alive $n)) $n))))
!(let $x (S Z) (live-count (Z Z $x)))
!(let $x (S Z) (live-count (Z $x $x)))
!(let $x (S Z) (match &self (neighbors (Z Z $x) $n) $n))
!(let $x (S (S Z)) (live-count (Z $x $x)))
