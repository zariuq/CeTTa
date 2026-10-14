; With several declarations, the result of the overload left for a call is
; a filter: a result of another type gives no answer, not an error.
(: tiny Small)
(: odd Small)
(: huge Large)
(: pick (-> Small Small))
(: pick (-> Large Large))
(= (pick $x) (if (== $x tiny) tiny huge))

!(test (pick tiny) tiny)
!(test (collapse (pick odd)) ())
