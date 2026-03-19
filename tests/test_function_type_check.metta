; Function type checking — arity and arg types
; Ref: metta.md lines 384-427, TypeCheck.lean:55-116

; Correct arity and types
(: add2 (-> Number Number Number))
(= (add2 $x $y) (+ $x $y))
!(assertEqual (add2 3 4) 7)

; Wrong arity — IncorrectNumberOfArguments
!(assertEqualToResult (add2 1) ((Error (add2 1) IncorrectNumberOfArguments)))
!(assertEqualToResult (add2 1 2 3) ((Error (add2 1 2 3) IncorrectNumberOfArguments)))

; Wrong arg type — BadArgType
!(assertEqualToResult (add2 1 "str") ((Error (add2 1 "str") (BadArgType 2 Number String))))
