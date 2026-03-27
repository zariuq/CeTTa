; HE-compatible let* semantics: an all-error binding result short-circuits the
; sequence instead of binding the Error atom and continuing later bindings.

!(assertEqualToResult
  (let* (($head (car-atom ()))
         (($x $y $z) (superpose ((a b 0) (d e 0) (g h 0)))))
    $y)
  ((Error (car-atom ()) "car-atom expects a non-empty expression as an argument")))
