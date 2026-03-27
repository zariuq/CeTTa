; Audit: empty-space helper with its return value explicitly ignored.
; This isolates the operational effect from the witness-payload mismatch.

(= (empty-space $space)
   (collapse (match $space $atom (remove-atom $space $atom))))

!(bind! &bag (new-space))
!(add-atom &bag keep-a)
!(add-atom &bag keep-b)
!(let $_ (empty-space &bag) empty-space-cleared)
!(get-atoms &bag)
