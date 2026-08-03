; Exact PeTTa relations own their head even when the shared CeTTa symbol
; table contains a host operation with the same spelling.
(= (unify $space $pattern $then $else)
   relation-wins)
!(unify &self pattern then else)

; A variable-headed relation is not authority to capture an unrelated host
; operation.  With no exact repr/1 relation, the shared operation remains
; the fallback owner.
(= ($f host-shadow-probe)
   wildcard-must-not-win)
!(repr host-shadow-probe)
