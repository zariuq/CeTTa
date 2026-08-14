; Explicit narrowing uses symmetric overlap rather than directional ordinary
; compatibility.  A Number can overlap a nominal Number representation even
; though an unguarded Number-to-Count promise cannot acquire the nominal role.

(: Count (Newtype Number))

(: number-value (-> Number))
(= (number-value) 1)

(: use-count (-> Count))
(= (use-count) (the Count (number-value)))
