; HE oracle fixture: typed `==` reports BadArgType when inferred constructor
; argument types mismatch. he-prime may choose a different equality policy.

!(== (42 (1 2 3)) (42 1 2 3))
