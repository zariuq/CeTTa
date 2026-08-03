; Stable stream deduplication preserves the first occurrence.
!(test
    (collapse (unique (superpose (a b c d d))))
    (a b c d))

; Union is bag concatenation, not set union.
!(test
    (collapse
      (union
        (superpose (a b b c))
        (superpose (b c c d))))
    (a b b c b c c d))

; Intersection consumes matching occurrences one at a time.
!(test
    (collapse
      (intersection
        (superpose (a b c c))
        (superpose (b c c c d))))
    (b c c))

; Subtraction removes only the matched multiplicity.
!(test
    (collapse
      (subtraction
        (superpose (a b b c))
        (superpose (b c c d))))
    (a b))

; Disjoint streams have an empty intersection.
!(test
    (collapse
      (intersection
        (superpose (a b))
        (superpose (c d))))
    ())
