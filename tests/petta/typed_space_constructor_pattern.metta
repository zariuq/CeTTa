; A constructor that only a declaration names is literal structure in a typed
; space's match pattern, never a call to elaborate: the row value bound into
; the pattern is data, and its (* ...) is not evaluated.
(: Goal Type)
(: Row Type)
(: Call (-[det]-> $formula $arguments $result Goal))
(: &rows (SpaceOf (Row Atom Atom Atom)))
!(add-atom &rows (Call g (* (D 1) (D 2)) out))
!(let $args (quote (* (D 1) (D 2))) (collapse (match &rows (Call g $args $r) $r)))
