; Expressions whose head no application can take, and list natives over
; lists built cell by cell, on the tier.
;
; PeTTa's translator keeps an expression data when its head is a number, a
; string or the empty list, its elements evaluated in order.  `append`,
; `union-atom` and `length` read a list's spine alone, so a list that
; `cons-atom` builds cell by cell reads as the flat list of its elements.
; Every answer below is SWI-PeTTa's.

(= (lit-empty $n) (if (> $n 0) (()) (lit-empty 1)))
(= (lit-number $n) (if (> $n 0) (1 (+ $n 1)) (lit-number 1)))
(= (lit-empty-head $n) (if (> $n 0) (() (+ $n 1)) (lit-empty-head 1)))
(= (lit-string $n) (if (> $n 0) ("s" (+ $n 1)) (lit-string 1)))
(= (lit-float $n) (if (> $n 0) (1.5 (+ $n 1)) (lit-float 1)))
(= (lit-nested $n) (if (> $n 0) ((1 (+ $n 1)) (+ $n 2)) (lit-nested 1)))
!(lit-empty 0)
!(lit-number 0)
!(lit-empty-head 0)
!(lit-string 0)
!(lit-float 0)
!(lit-nested 0)

; A head that is a call is applied to the rest: its value, a number, keeps
; the expression data.  A head that names a function is a call.
(= (f $x) (+ $x 1))
(= (g $x) (* $x 10))
(= (call-head $n) (if (> $n 0) ((f $n) (+ $n 1)) (call-head 1)))
(= (named-head $n) (if (> $n 0) (g (+ $n 1)) (named-head 1)))
!(call-head 0)
!(named-head 0)

; Lists built cell by cell, read by `append`, `union-atom` and `length`.
(= (range-c $a $b) (if (< $a $b) (cons-atom $a (range-c (+ $a 1) $b)) ()))
(= (prefix-all $h ()) ())
(= (prefix-all $h (cons $c $cs)) (cons-atom (cons $h $c) (prefix-all $h $cs)))
!(append (prefix-all 0 ((1) (2 3) ())) ((9)))
!(length (prefix-all 0 ((1) (2 3) ())))
!(append (range-c 0 3) (range-c 3 5))
!(union-atom (range-c 0 2) (x y))
!(length (append (range-c 0 100000) (range-c 0 100000)))

; Combinations, as chooseK builds them.
(= (chooseKt $L 0) (let $temp (cut) (())))
(= (chooseKt () $k) (let $temp (cut) ()))
(= (chooseKt (cons $h $t) $k)
   (append (prefix-all $h (chooseKt $t (- $k 1))) (chooseKt $t $k)))
(= (chooseT $L $k) (let $options (chooseKt $L $k) (superpose $options)))
!(collapse (chooseT (0 1 2 3) 2))
!(size-atom (collapse (chooseT (0 1 2 3 4 5 6 7 8 9 10 11 12 13) 7)))
!(let $a (once (chooseT (0 1 2 3 4 5 6 7 8 9 10 11 12 13) 7)) (size-atom $a))
