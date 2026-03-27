; HE surfaces wrong arity as IncorrectNumberOfArguments for the math.rs
; grounded operators.

!(assertEqualToResult (pow-math 2) ((Error (pow-math 2) IncorrectNumberOfArguments)))
!(assertEqualToResult (sqrt-math) ((Error (sqrt-math) IncorrectNumberOfArguments)))
