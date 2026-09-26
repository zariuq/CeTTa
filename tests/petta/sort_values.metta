;; PeTTa's sort-atom and msort sort a list's values in standard order and
;; keep duplicates; sort-atom gives () for an atomic value.  The argument
;; is evaluated by PeTTa's rules first, nested list operations included,
;; and the order is the same wherever the sort runs.

(= (sorted $x) (sort-atom $x))
(= (msorted $x) (msort $x))
(= (pair-sorted $a $b) (sort-atom ($a $b)))
(= (let-sorted $x) (let $y (sort-atom $x) $y))
(= (guarded-sorted $x $n) (if (> $n 0) (sort-atom $x) ()))
(= (precedes $a $b) (and (not (== $a $b)) (== (sort-atom ($a $b)) ($a $b))))
(item (b aa))

!(sort-atom (b a a))
!(msort (b a a))
!(sort-atom ())
!(sort-atom atom)
!(sort-atom 7)
!(sort-atom (3 1.5 "s" z (f x) 2))
!(sort-atom ((f b) (f aa) (g) aa))

;; Standard order, not the shortest spelling first.
!(sort-atom (b aa))
!(sorted (b aa))
!(msorted (b aa))
!(pair-sorted b aa)
!(let-sorted (b aa))
!(guarded-sorted (b aa) 1)
!(once (pair-sorted b aa))
!(match &self (item $x) (sort-atom $x))
!(let $x (b aa) (sort-atom $x))

;; The argument's nested operations run first.
!(sort-atom (cons-atom b (cons-atom a ())))
!(sort-atom (cons-atom c (cons-atom b (cons-atom a ()))))
!(sort-atom (reverse (cons-atom b (a))))
!(sort-atom (union-atom (c) (union-atom (b) (a))))
!(sort-atom (cdr-atom (x (cons-atom b ()) a)))
!(msort (cons-atom b (cons-atom a ())))

;; A sorted pair as an order test.
!(once (pair-sorted G F))
!(once (pair-sorted (⊙ 2) (⊙ 1)))
!(collapse (pair-sorted G F))
!(once (precedes F G))
!(once (precedes G F))
!(collapse (let $x (superpose (F G D0)) (precedes $x G)))
