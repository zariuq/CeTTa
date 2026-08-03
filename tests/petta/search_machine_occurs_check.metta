; Repeated variables enforce one acyclic value.  The first call is a valid
; non-linear match; the second would require $item = (wrap $item) and must
; fail rather than constructing an infinite rational term.
(= (same-value $value $value) consistent)

!(collapse (same-value (wrap item) (wrap item)))
!(collapse (same-value (wrap $item) $item))
