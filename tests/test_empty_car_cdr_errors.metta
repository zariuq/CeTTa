; HE-backed destructor surface: empty and non-expression inputs should error,
; not silently return Empty or ().

!(assertEqualToResult
  (car-atom ())
  ((Error (car-atom ()) "car-atom expects a non-empty expression as an argument")))

!(assertEqualToResult
  (car-atom A)
  ((Error (car-atom A) "car-atom expects a non-empty expression as an argument")))

!(assertEqualToResult
  (cdr-atom ())
  ((Error (cdr-atom ()) "cdr-atom expects a non-empty expression as an argument")))

!(assertEqualToResult
  (cdr-atom a)
  ((Error (cdr-atom a) "cdr-atom expects a non-empty expression as an argument")))

!(assertEqualToResult
  (cdr-atom $a)
  ((Error (cdr-atom $a) "cdr-atom expects a non-empty expression as an argument")))
