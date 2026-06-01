; Exact integer arithmetic must promote past int64 into canonical bigints.

!(assertEqualToResult
   (+ 9223372036854775807 1)
   (9223372036854775808))

!(assertEqualToResult
   (- -9223372036854775807 2)
   (-9223372036854775809))

!(assertEqualToResult
   (* 4611686018427387904 2)
   (9223372036854775808))
