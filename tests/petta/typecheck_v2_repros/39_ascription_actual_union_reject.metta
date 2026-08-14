; Explicit narrowing may inspect an actual union existentially, but a target
; disjoint from every member is still a definite type error.

(: maybe-value (-> (| Number String)))
(= (maybe-value) 1)

(: use-bool (-> Bool))
(= (use-bool) (the Bool (maybe-value)))
