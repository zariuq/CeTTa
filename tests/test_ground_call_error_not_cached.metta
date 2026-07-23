;; A call that produces a runtime error is not admitted as a memoized value;
;; it yields the same error deterministically.
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (add-atom &self (= (gc:bad $n) (car-atom ()))))
    (() (assertEqualToResult (gc:bad 3)
          ((Error (car-atom ()) "car-atom expects a non-empty expression as an argument"))))
    (() (assertEqualToResult (gc:bad 3)
          ((Error (car-atom ()) "car-atom expects a non-empty expression as an argument"))))
  ) ())
