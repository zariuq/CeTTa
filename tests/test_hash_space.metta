!(bind! &h (new-space hash))
!(assertEqualToResult (get-type &h) ((Space hash)))
!(assertEqualToResult (space-len &h) (0))
!(add-atom &h (seen alpha))
!(add-atom &h (seen beta))
!(assertEqualToResult (space-len &h) (2))
!(assertEqualToResult (match &h (seen alpha) found) (found))

; Empty-only case is the Boolean observation of a match.  Both the absent
; insertion and present rejection are language behavior, independent of the
; storage realization used to answer existence.
(= (insert-gamma-if-new)
   (case (collapse (match &h (seen gamma) found))
     ((() (let () (add-atom &h (seen gamma)) inserted)))))
!(assertEqualToResult (insert-gamma-if-new) (inserted))
!(assertEqualToResult (insert-gamma-if-new) ())
!(assertEqualToResult (space-len &h) (3))

; An open stored row lies outside exact-membership admission.  The ordinary
; matcher remains authoritative and observes that it covers the query.
!(bind! &open-h (new-space hash))
!(add-atom &open-h (seen $anything))
(= (insert-delta-if-new)
   (case (collapse (match &open-h (seen delta) found))
     ((() (let () (add-atom &open-h (seen delta)) inserted)))))
!(assertEqualToResult (insert-delta-if-new) ())
!(assertEqualToResult (space-len &open-h) (1))

!(add-atom-nodup &h (seen beta))
!(assertEqualToResult (space-len &h) (3))
!(remove-atom &h (seen alpha))
!(assertEqualToResult (space-len &h) (2))
!(assertEqualToResult (match &h (seen beta) found) (found))
