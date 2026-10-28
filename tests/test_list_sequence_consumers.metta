; A list is folded, mapped and filtered by its elements, after its tag, and
; what is built from it is a list.  An empty list folds to the initial value
; and maps and filters to itself.  No internal tag is ever an element.

!(foldl-atom [1 2 3] 0 $a $x (+ $a $x))
!(let $l [1 2 3] (foldl-atom $l 0 $a $x (+ $a $x)))
!(foldl-atom [] 0 $a $x (+ $a $x))
!(map-atom [1 2 3] $x (* $x 10))
!(let $l [1 2 3] (map-atom $l $x (* $x 10)))
!(map-atom [] $x (* $x 10))
!(filter-atom [1 2 3] $x (> $x 1))
!(let $l [1 2 3] (filter-atom $l $x (> $x 1)))
!(filter-atom [] $x (> $x 1))

; A body the generated machine does not take is mapped by the library, by
; the same elements.
!(bind! &seen (new-space))
!(map-atom [a b] $x (let $u (add-atom &seen (saw $x)) $x))
!(collapse (match &seen (saw $x) $x))

; Negative: a plain expression is read as it always was.
!(foldl-atom (1 2 3) 0 $a $x (+ $a $x))
!(map-atom (1 2 3) $x (* $x 10))
!(filter-atom (1 2 3) $x (> $x 1))
!(filter-atom () $x (> $x 1))
