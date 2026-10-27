!(import! &self ./need_region_support_restriction_lib)

; A ground result can discard wholly local temporary cells. Updates to cells
; held by its caller must survive even when the returned result is scalar.
!(prime-need:region-ground 2)

; A suspension crossing the same boundary retains its cell.  Both reads must
; observe one call-time value, rather than resampling or losing the cell.
!(let $promise (prime-need:region-carry 2)
   (Pair (force $promise) (force $promise)))
