; StackOverflow — infinite recursion should produce error, not hang
; Ref: metta.md line 137
(= (loop) (loop))
!(loop)
