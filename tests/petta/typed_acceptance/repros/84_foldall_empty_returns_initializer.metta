; An empty generator never calls the accumulator.  The result is therefore
; the initializer itself, retaining its concrete type and the one-result
; cardinality promised by foldall.

(: empty-fold (-[det]-> Number Number))
(= (empty-fold $initial)
   (foldall + (empty) $initial))
