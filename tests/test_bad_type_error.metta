; BadType error — type cast mismatch
; Ref: metta.md line 147, TypeCheck.lean:126-148
(: myval Int)
!(assertEqualToResult (+ myval 1) ((Error (+ myval 1) (BadArgType 1 Number Int))))
