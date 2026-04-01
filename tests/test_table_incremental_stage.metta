!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (stage-probe $x) (pair $x left))))
    (() (add-atom &self (= (stage-probe $x) (pair $x right))))
    (() (reset-runtime-stats!))
    (() (assertEqual
          (collapse (stage-probe seed))
          ((pair seed left) (pair seed right))))
    ($stats (runtime-stats!))
    (() (assertEqualToResult
          (match $stats (runtime-counter table-answer-staged $n) (> $n 1))
          (True)))
  )
  ())
