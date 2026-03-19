; context-space — returns the current working atomspace
; Ref: minimal-metta.md line 193
(= (fact-here) found)
!(match (context-space) (= (fact-here) $x) $x)
