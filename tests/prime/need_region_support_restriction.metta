!(import! &self ./need_region_support_restriction_lib)

; A ground result owes none of a pure equation call's speculative Need suffix.
!(prime-need:region-ground 2)

; A suspension crossing the same boundary retains its cell.  Both reads must
; observe one call-time value, rather than resampling or losing the cell.
!(let $promise (prime-need:region-carry 2)
   (Pair (force $promise) (force $promise)))
