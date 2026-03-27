; HE math.rs registers PI and EXP as grounded numeric tokens. Direct `!(PI)` and
; `!(EXP)` therefore behave like singleton grounded expressions, while arithmetic
; contexts consume them as numbers.

!(PI)
!(EXP)
!(+ PI 1)
!(+ EXP 1)
