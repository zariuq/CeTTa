; One sequence view, read by every operation on a sequence's elements: a
; list's elements are those after its tag, and a braces node, for which no
; set form is defined yet, has none.  No internal tag is ever an element.

; A list is folded, mapped and filtered by its elements, and what is built
; from it is a list, written inline or bound by `let`, empty or not.
!(foldl-atom [1 2 3] 0 $a $x (+ $a $x))
!(let $l [1 2 3] (foldl-atom $l 0 $a $x (+ $a $x)))
!(foldl-atom [] 0 $a $x (+ $a $x))
!(map-atom [1 2 3] $x (* $x 10))
!(let $l [1 2 3] (map-atom $l $x (* $x 10)))
!(map-atom [] $x (* $x 10))
!(filter-atom [1 2 3] $x (> $x 1))
!(let $l [1 2 3] (filter-atom $l $x (> $x 1)))
!(filter-atom [] $x (> $x 1))

; A body with an effect is mapped by the library, by the same elements.
!(bind! &seen (new-space))
!(map-atom [a b] $x (let $u (add-atom &seen (saw $x)) $x))
!(collapse (match &seen (saw $x) $x))

; Choice, sort and index read the same elements.
!(collapse (superpose [c a b]))
!(sort-atom [c a b])
!(index-atom [c a b] 0)

; A braces node is no sequence.  A choice over it has no alternatives; an
; operation that reads elements does not reduce on it, or refuses it; none
; reads its tag.
!(collapse (superpose {c a b}))
!(collapse (hyperpose {c a b}))
!(collapse (superpose {}))
!(sort-atom {c a b})
!(index-atom {c a b} 0)
!(size-atom {c a b})
!(if-error (cons-atom z {c a b}) refused accepted)
!(if-error (foldl-atom {c a b} 0 $a $x (+ $a 1)) refused accepted)
!(if-error (map-atom {c a b} $x (w $x)) refused accepted)
!(if-error (filter-atom {c a b} $x True) refused accepted)

; Negative: a plain expression is read as it always was.
!(foldl-atom (1 2 3) 0 $a $x (+ $a $x))
!(map-atom (1 2 3) $x (* $x 10))
!(filter-atom (1 2 3) $x (> $x 1))
!(collapse (superpose (c a b)))
!(cons-atom z (c a b))
