;; A memoized call that observes a space must not go stale: mutating any space
;; bumps the global epoch and invalidates the entry, forcing a fresh answer.
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (bind! &kb (new-space)))
    (() (add-atom &kb (n 1)))
    (() (assertEqual (reify (match &kb (n $x) $x)) (1)))
    (() (add-atom &self (= (gc:read $k) (match &kb (n $x) $x))))
    (() (assertEqual (gc:read 0) 1))
    (() (remove-atom &kb (n 1)))
    (() (add-atom &kb (n 2)))
    (() (assertEqual (gc:read 0) 2))
  ) ())
