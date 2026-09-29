; decons-atom inside a function gives a list's first element and the rest, a
; view sharing the list's storage: a walk by deconstruction stays linear in
; the list's length, and the rest is the data it is, however it begins.  A
; cons cell is its head and its tail.  Every answer is SWI-PeTTa's.
(= (walk-decons $l $acc) (if (== $l ()) $acc (let ($h $t) (decons-atom $l) (walk-decons $t (+ $acc 1)))))
(= (dc $l) (decons-atom $l))
(= (dcc) (decons-atom (cons a (b c))))
(= (count-down $n) (if (> $n 0) (superpose ($n (count-down (- $n 1)))) (empty)))
!(walk-decons (collapse (count-down 100000)) 0)
!(dc (x cons a (b)))
!(dcc)
!(dc (single))
