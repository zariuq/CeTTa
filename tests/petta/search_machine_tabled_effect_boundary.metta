!(bind! &effects (new-space))

!(tabled (changing $x))
(= (changing one) pure)
!(collapse (changing one))

; Adding an effectful clause changes the relation's revision.  The table
; admission cache must be invalidated, and ordinary search must run each
; effect exactly once rather than caching, replaying, or rejecting it.
(= (record-change)
   (add-atom &effects changed))
(= (changing one)
   (record-change))
!(collapse (changing one))
!(collapse (get-atoms &effects))
