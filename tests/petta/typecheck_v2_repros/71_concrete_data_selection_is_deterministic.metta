; `data` constructs an inert expression, but typecheck-v2 does not expose its
; source fields as runtime clause-selection evidence.  A deterministic caller
; must therefore remain unproven even when the wrapped fields look concrete.

(= (decode-form (left $value)) 1)
(= (decode-form (right $value)) 2)

(: concrete-data-selector (-[det]-> Number))
(= (concrete-data-selector)
   (decode-form (data left 0)))
