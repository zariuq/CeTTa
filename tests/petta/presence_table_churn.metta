; A set over ids far into a large universe keeps its keys as a table: adds,
; removals that shift later keys back, re-adds and duplicates answer as a set.
(= (pad $n) (if (== $n 0) done (let $u (add-atom &self (pad-row $n)) (pad (- $n 1)))))
!(pad 20000)
!(bind! &s (new-space set))
(= (fill $i $n) (if (> $i $n) done (let $u (add-atom &s (r $i)) (fill (+ $i 1) $n))))
!(fill 1 100)
(= (drop-odd $i $n) (if (> $i $n) done
  (let $u (if (== (% $i 2) 1) (remove-atom &s (r $i)) ()) (drop-odd (+ $i 1) $n))))
!(drop-odd 1 100)
!(size-atom (collapse (get-atoms &s)))
(= (back-threes $i $n) (if (> $i $n) done
  (let $u (if (== (% $i 3) 0) (add-atom &s (r $i)) ()) (back-threes (+ $i 1) $n))))
!(back-threes 1 100)
!(size-atom (collapse (get-atoms &s)))
!(fill 1 100)
!(size-atom (collapse (get-atoms &s)))
!(match &s (r 99) yes)
!(remove-atom &s (r 99))
!(collapse (match &s (r 99) yes))
!(size-atom (collapse (get-atoms &s)))
