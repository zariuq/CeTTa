; Named-space shared-handle operational surface:
; new-space, add-atom, match, get-atoms, remove-atom.
;
; Positive example:
; - gives an empirical cross-runtime check for the explicit-space surface.
; - the handle-bound atomspace ops are the intended next bridge target beyond &self.
;
; Negative example:
; - dynamic allocation itself is still a separate runtime/allocator seam.

!(bind! &bag (new-space))
!(add-atom &bag (probe-item a))
!(add-atom &bag (probe-item b))
!(match &bag (probe-item $x) $x)
!(remove-atom &bag (probe-item a))
!(get-atoms &bag)
