; Exact integer arithmetic must not silently promote overflow to float.

!(assertEqualToResult
   (+ 9223372036854775807 1)
   ((Error (+ 9223372036854775807 1) IntegerOverflow)))

!(assertEqualToResult
   (- -9223372036854775807 2)
   ((Error (- -9223372036854775807 2) IntegerOverflow)))

!(assertEqualToResult
   (* 4611686018427387904 2)
   ((Error (* 4611686018427387904 2) IntegerOverflow)))
