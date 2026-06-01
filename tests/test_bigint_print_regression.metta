; Bigints print as canonical decimal atoms, not strings or floats.

!(+ +0009223372036854775808 1)

; Bigint mixed into approximate float arithmetic must not print as an
; exact-looking decimal integer.
!(+ 100000000000000000000000000 0.0)
