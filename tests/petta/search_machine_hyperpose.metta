(= (choices) (superpose (a b b)))
(= (hide) (empty))

; Hyperpose has the same pure answer bag as superpose.  Scheduling order is
; deliberately not asserted here; msort makes the semantic bag observable.
!(test
    (msort (collapse (hyperpose (c a b b))))
    (a b b c))

; A branch list supplied through a variable remains a branch list.
!(test
    (msort
        (collapse
            (let $branches (c a b)
                (hyperpose $branches))))
    (a b c))

; Branches are computations, failed branches disappear, and multiplicity is
; retained.
!(test
    (msort
        (collapse
            (hyperpose ((choices) (hide)))))
    (a b b))

; Once observes one successful branch and cancels the remaining search.
!(test
    (once (hyperpose (same same same)))
    same)
