; PeTTa's sort of one list is SWI's sort/2: the standard order of terms with
; duplicates (==) removed.  Numbers before symbols before strings before
; expressions; a float before an integer of equal value; -0.0 before 0.0.
!(sort (3 1 2 1))
!(sort ())
!(sort (b a b "s" 1 1.0 -0.0 0.0 (f x) (f x) (f y) (g) 2))
!(sort ((p 2) (p 1) (p 2) (p 1 0)))

; inside functions, where the compiled tiers run it
(= (sup $x) (superpose (3 1 2 3 1)))
(= (sorted-answers $x) (sort (collapse (sup $x))))
(= (sorted-let $x) (let $c (collapse (sup $x)) (sort $c)))
(= (sorted-value $xs) (sort $xs))
(= (memo-sorted $k)
   (let $l (sort (collapse (sup $k)))
     (let $_ (add-atom &self (sorted-memo $k $l)) $l)))
!(sorted-answers 0)
!(sorted-let 0)
!(sorted-value (c a b a))
!(memo-sorted 1)
!(match &self (sorted-memo 1 $l) $l)

; a list with variables keeps one copy of each variable
!(size-atom (sort ($x $y $x)))
(= (vars-sorted $a $b) (size-atom (sort ($a $b $a $b))))
!(vars-sorted $u $v)

; sort with a key and an order is SWI's sort/4
!(sort 0 @>= (3 1 2 1))
!(sort 1 @< ((b 2) (a 1) (c 0)))

