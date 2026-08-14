; This separates intersection-based head refinement from Roman's permissive
; candidate accumulation.  The repeated variable receives a union slot and a
; Number slot, then the body attempts to consume it as Number.
(: repeated-head-use (-> (| Number String) Number Number))
(= (repeated-head-use $value $value) (+ $value 1))

!(test (repeated-head-use 5 5) 6)
