; Schema construction shares the checking request budget. A representation
; limit reports incomplete computation; malformed indices and an unbounded
; Separation body remain outside the declared schema grammar.

; small
!(car-atom (try (set:known-proposition (boundedSeparation 0 (set:eq 0 0)))))

; huge-prefix
!(try (set:known-proposition (boundedSeparation 1000000000 (set:eq 0 0))))

; bigint-prefix
!(try (set:known-proposition (boundedSeparation 18446744073709551616 (set:eq 0 0))))

; negative-count
!(try (set:known-proposition (boundedSeparation -1 (set:eq 0 0))))

; escaped-free-index
!(try (set:known-proposition (boundedSeparation 0 (set:eq 0 1))))

; unrestricted-rejected
!(try (set:known-proposition (boundedSeparation 0 (set:all (set:eq 0 0)))))

; escaped-bound-parent
!(try (set:known-proposition (boundedSeparation 0 (set:all-in 1 (set:eq 0 0)))))

; nested-bounded
!(car-atom (try (set:known-proposition (boundedSeparation 1 (set:all-in 1 (set:exists-in 2 (set:and (set:mem 0 3) (set:eq 1 2))))))))

; strong-unbounded
!(car-atom (try (set:known-proposition (strongCollection 1 (set:all (set:eq 0 3))))))

; subset-unbounded
!(car-atom (try (set:known-proposition (subsetCollection 1 (set:exists (set:mem 0 4))))))

; large-valid-prefix
!(car-atom (try (set:known-proposition (boundedSeparation 2000 (set:eq 0 0)))))

; deep-expanded-body
!(car-atom (try (set:known-proposition (boundedSeparation 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:exists-in 0 (set:eq 0 0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

; publish-actual-schema-theorem
!(let $proposition (set:known-proposition (boundedSeparation 20 (set:eq 0 0)))
    (set:theorem budgeted-schema $proposition (pf:known (boundedSeparation 20 (set:eq 0 0)))))

; budget-before-prefix
!(try (set:native-proof budgeted-schema 1))

; budget-at-prefix
!(try (set:native-proof budgeted-schema 20))

; budgeted-success
!(car-atom (try (set:native-proof budgeted-schema 10000000)))

; Rechecking a virtual signature instance uses the same expansion boundary.
!(try (set:recheck (boundedSeparation 1000000000 (set:eq 0 0))))
!(car-atom (try (set:recheck (boundedSeparation 0 (set:eq 0 0)))))
