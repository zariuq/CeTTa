!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (cache-probe remove) one)))
    (() (add-atom &self (= (cache-probe remove) two)))
    (() (assertEqual (collapse (cache-probe remove)) (one two)))
    (() (remove-atom &self (= (cache-probe remove) one)))
    (() (reset-runtime-stats!))
    (() (assertEqualToResult (cache-probe remove) (two)))
    ($stale (runtime-stats!))
    (() (assertEqualToResult
          (match $stale (runtime-counter table-stale-miss $n) (> $n 0))
          (True)))
  )
  ())
