; Data spelled with `cons` and `nil`, as the parser pack's forests are: an
; authored list pattern in a head, a `let` or a `case` reads a value spelled
; `(cons h t)` as the cell it spells, on the open-equation tier as in the
; machine; foldl reads only lists.  The gate runs it on both routes.

(item (cons a (cons b nil)))
(= (spelled) (match &self (item $l) $l))

(= (walk nil) done)
(= (walk (cons $h $t)) (walk $t))
(= (walk-spelled) (walk (spelled)))
!(walk-spelled)

(= (heads nil) ())
(= (heads (cons $h $t)) (cons-atom $h (heads $t)))
(= (heads-spelled) (heads (spelled)))
!(heads-spelled)

(= (first-by-case)
   (case (spelled) (((cons $h $t) $h) ($other none))))
!(first-by-case)

(= (nest $i $acc) (pair $i $acc))
(= (fold-spelled) (foldl nest (spelled) z))
!(fold-spelled)
