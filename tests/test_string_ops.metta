; String grounded type
; Ref: metta.md grounded atoms

; String equality
!(assertEqual (== "hello" "hello") True)
!(assertEqual (== "hello" "world") False)

; String is not a number
!(assertEqualToResult (+ 1 "hello") ((Error (+ 1 "hello") (BadArgType 2 Number String))))
