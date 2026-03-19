; type_cast — explicit type casting behavior
; Ref: metta.md lines 275-296, TypeCheck.lean:126-148

; Symbol with matching type passes through
(: x Int)
!(assertEqual x x)

; Grounded atom with correct type
!(assertEqual 42 42)

; %Undefined% type always matches (gradual typing)
; Unannotated symbol used freely
!(assertEqual (unknown-sym) (unknown-sym))
