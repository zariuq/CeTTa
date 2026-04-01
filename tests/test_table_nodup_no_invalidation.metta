!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (cache-probe nodup) one)))
    (() (assertEqualToResult (cache-probe nodup) (one)))
    (() (reset-runtime-stats!))
    (() (add-atom-nodup &self (= (cache-probe nodup) one)))
    (() (assertEqualToResult (cache-probe nodup) (one)))
    ($stats (runtime-stats!))
    (() (assertEqualToResult
          (match $stats (runtime-counter table-hit $n) (> $n 0))
          (True)))
    (() (assertEqualToResult
          (match $stats (runtime-counter table-stale-miss $n) $n)
          (0)))
    (() (assertEqualToResult
          (match $stats (runtime-counter space-revision-bump $n) $n)
          (0)))
  )
  ())
