(= (greater-than-three $item) (> $item 3))

!(test
    (filter-atom (1 2 3 4 5) $item (> $item 3))
    (4 5))

; Kept occurrences are a sequence, not a set.
!(test
    (filter-atom (1 2 2 3) $item (> $item 1))
    (2 2 3))

; The compiled condition closes over the branch's current environment.
!(test
    (let $threshold 2
        (filter-atom (1 2 3 4) $item (> $item $threshold)))
    (3 4))

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

; An effectful predicate stays on the relational route.  Each item is tested
; once, even when it is omitted from the result.
!(bind! &filter-log (new-space))
!(test
    (filter-atom (1 2 3) $item
        (let $written (add-atom &filter-log (seen $item)) (> $item 1)))
    (2 3))
!(test
    (collapse (match &filter-log (seen $item) $item))
    (1 2 3))
