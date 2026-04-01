!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (cache-probe add) one)))
    (() (reset-runtime-stats!))
    (() (assertEqualToResult (cache-probe add) (one)))
    ($cold (runtime-stats!))
    (() (assertEqualToResult
          (match $cold (runtime-counter table-miss $n) (> $n 0))
          (True)))
    (() (add-atom &self (= (cache-probe add) two)))
    (() (reset-runtime-stats!))
    (() (assertEqual (collapse (cache-probe add)) (one two)))
    ($stale (runtime-stats!))
    (() (assertEqualToResult
          (match $stale (runtime-counter table-stale-miss $n) (> $n 0))
          (True)))
  )
  ())
