; A head takes its relation while a match walks its rows: the body adds the
; head's 4,096th fact, its rows move into the relation, and the walk keeps
; the rows it began with.  A multiset's second copy of a fact stays a row,
; and every reading answers as the rows did.
(= (fill $i $n) (if (>= $i $n) done (let $u (add-atom &self (edge $i (succ $i))) (fill (+ $i 1) $n))))
!(fill 0 4000)
!(add-atom &self (edge 7 (succ 7)))
!(size-atom (collapse (match &self (edge 3 $y) $y)))
!(size-atom (collapse (match &self (edge $x (succ $y)) (let $u (if (== $x 10) (fill 4000 4200) done) $x))))
!(size-atom (collapse (match &self (edge $x $y) $x)))
!(collapse (match &self (edge 7 $y) $y))
!(collapse (match &self (edge 4100 $y) $y))
!(remove-atom &self (edge 7 (succ 7)))
!(collapse (match &self (edge 7 $y) $y))
!(remove-atom &self (edge 7 (succ 7)))
!(collapse (match &self (edge 7 $y) $y))
!(size-atom (collapse (match &self (edge $x $y) $x)))
!(fill 0 3)
!(size-atom (collapse (match &self (edge $x $y) $x)))
