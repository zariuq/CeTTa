; Arithmetic hardening: exact large ints, float preservation, and divide/mod zero.

!(assertEqualToResult (/ 5 0) ((Error (/ 5 0) DivisionByZero)))
!(assertEqualToResult (% 7 0) ((Error (% 7 0) DivisionByZero)))
!(/ 5.0 0.0)
!(+ 9223372036854775807 0)
!(- 9223372036854775807 0)
!(+ 4611686018427387903 4611686018427387903)
!(+ 1.0 0)
!(* 2.0 1)
!(- 5.0 0)
