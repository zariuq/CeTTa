;; Ground-call memo: a repeated ground call replays the same value it first
;; computed.  With the mode off this passes trivially; the runner also checks
;; a cache hit was recorded, so the memo is genuinely exercised.
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (add-atom &self (= (gc:f $n) (* $n 10))))
    (() (assertEqual (gc:f 7) 70))
    (() (assertEqual (gc:f 7) 70))
    (() (assertEqual (gc:f 8) 80))
  ) ())
