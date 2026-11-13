; Stable stream deduplication preserves the first occurrence.
!(test
    (sort-atom (collapse (unique (superpose (a b c d d)))))
    (sort-atom (a b c d)))

; Union is bag concatenation, not set union.
!(test
    (sort-atom (collapse
      (union
        (superpose (a b b c))
        (superpose (b c c d)))))
    (sort-atom (a b b c b c c d)))

; Intersection consumes matching occurrences one at a time.
!(test
    (sort-atom (collapse
      (intersection
        (superpose (a b c c))
        (superpose (b c c c d)))))
    (sort-atom (b c c)))

; Subtraction removes only the matched multiplicity.
!(test
    (sort-atom (collapse
      (subtraction
        (superpose (a b b c))
        (superpose (b c c d)))))
    (sort-atom (a b)))

; Disjoint streams have an empty intersection.
!(test
    (collapse
      (intersection
        (superpose (a b))
        (superpose (c d))))
    ())
