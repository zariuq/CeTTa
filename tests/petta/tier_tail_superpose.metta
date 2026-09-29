; A literal superpose in a tail position returns its alternatives' answers to
; the equation's caller, as a tail call does.  A generator that recurses
; through its alternatives answers each value once, not through one return
; per level of recursion, and stays linear in its length.  Every answer is
; SWI-PeTTa's.
(= (count-down $n) (if (> $n 0) (superpose ($n (count-down (- $n 1)))) (empty)))
(= (pairs-from $n) (if (> $n 0) (superpose ((pair $n) (pairs-from (- $n 1)))) (empty)))
(= (tagged $x) (let $y (+ $x 1) (superpose ((a $y) (b $y)))))
(= (both $n) (superpose ((count-down $n) (count-down 2))))
!(size-atom (collapse (count-down 100000)))
!(collapse (count-down 5))
!(collapse (pairs-from 3))
!(collapse (let (pair $k) (pairs-from 3) $k))
!(collapse (tagged 1))
!(collapse (both 3))
; once answers some witness: any of the values.
!(let $w (once (count-down 1000)) (and (> $w 0) (<= $w 1000)))
