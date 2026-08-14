; An alias is transparent to explicit narrowing.  Its union representation
; overlaps Number even though an unguarded union-to-Number promise would need
; every member to fit.

(: Choice (Alias (| Number String)))

(: maybe-choice (-> Choice))
(= (maybe-choice) 1)

(: use-number (-> Number))
(= (use-number) (the Number (maybe-choice)))
