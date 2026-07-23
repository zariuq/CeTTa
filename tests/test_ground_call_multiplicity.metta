;; A memoized nondeterministic ground call replays its whole answer multiset,
;; multiplicities intact — never a set-collapse.
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (add-atom &self (= (gc:pick $n) (superpose (a a b)))))
    (() (assertEqual (collapse (gc:pick 1)) (a a b)))
    (() (assertEqual (collapse (gc:pick 1)) (a a b)))
  ) ())
