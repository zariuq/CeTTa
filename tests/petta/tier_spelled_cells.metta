; Data spelled with `cons` and `nil` is data, as in SWI-PeTTa: a list
; pattern in a head, a `let` or a `case` meets a stored `(cons a (cons b nil))`
; as the three-element expression it is, whose head is `cons`; `nil` is a
; symbol, so the walks find no list and answer nothing, and the case answers
; cons.  foldl reads only lists.  The gate runs it on both routes.

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
