(= (greater-than-three $item) (> $item 3))

!(test
    (filter-atom (1 2 3 4 5) $item (> $item 3))
    (4 5))

!(test
    (filter-atom (1 2 3 4 5) greater-than-three)
    (4 5))

; Its list operand is demanded before filtering.
!(test
    (filter-atom (append (1 2) (3 4 5))
        $item (> $item 3))
    (4 5))

; The predicate is existential.  For item 2, the first answer is false and
; the second is true; the item must still be retained exactly once.
!(test
    (filter-atom (1 2 3) $item
        (superpose
            ((== $item 2)
             (== $item 2))))
    (2))

; Failure and all-false predicates omit an item rather than failing the
; entire traversal.
!(test
    (filter-atom (1 2 3) $item
        (if (== $item 2) (empty) False))
    ())

; A non-list source has no relational answer.
!(test
    (collapse
        (filter-atom not-a-list greater-than-three))
    ())
