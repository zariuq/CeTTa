;; A single-answer ground call must replay exactly one occurrence on a hit,
;; never a duplicated bag.
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (add-atom &self (= (gc:one $n) (+ $n 1))))
    (() (assertEqual (gc:one 5) 6))
    (() (assertEqual (collapse (gc:one 5)) (6)))
  ) ())
