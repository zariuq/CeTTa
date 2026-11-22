; A rational type equation does not erase later contradictions.  The first
; field can close a recursive List equation, but the repeated variable in the
; second field would then require the same type to be headed by Tree.

(: cyclic-conflict (-> ($a $a) ((List $a) (Tree $a))))
(= (cyclic-conflict $value) $value)
