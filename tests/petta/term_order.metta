; PeTTa msort uses SWI's standard term order and preserves duplicate
; occurrences.  It sorts evaluated list alternatives independently.
!(msort (3 1 2 1))
!(msort (z 2 1.5 "s" (f a) a () (b) 1))
!(msort ((z 0) (a 2) (a 1) (a) z))
!(sort-atom (3 2 1))
!(msort (superpose ((3 1 3) (2 1) (2 1))))
