; Width-safe tuple evaluation must keep the old recursive semantics: DFS order,
; left-to-right binding threading, and direct error propagation.

!(assertEqual
  (collapse ((superpose (a b)) (superpose (1 2))))
  ((a 1) (a 2) (b 1) (b 2)))

!(assertEqual
  (collapse ((let $x (superpose (a b)) $x)
             (if (== $x a) one two)))
  ((a one) (b two)))

!(assertEqualToResult
  ((car-atom ()) tail)
  ((Error (car-atom ()) "car-atom expects a non-empty expression as an argument")))

!(assertEqualToResult
  (collapse ((car-atom ()) tail))
  (((Error (car-atom ()) "car-atom expects a non-empty expression as an argument"))))
