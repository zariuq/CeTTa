; Audit: shared empty-space helper built only from match/remove/collapse.
; This is a good bridge witness for space-clearing behavior without delete-space.

(= (empty-space $space)
   (collapse (match $space $atom (remove-atom $space $atom))))

!(bind! &bag (new-space))
!(add-atom &bag keep-a)
!(add-atom &bag keep-b)
!(empty-space &bag)
!(get-atoms &bag)
