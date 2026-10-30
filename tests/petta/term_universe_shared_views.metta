; A stored term sharing its subterms, (pair x x) nested 40 times, has 41
; distinct subterms and 2^40 paths: reading, matching and comparing it cost
; its distinct subterms.
(= (dag $n $x) (if (== $n 0) $x (dag (- $n 1) (pair $x $x))))
!(let $t (dag 40 leaf) (add-atom &self (h $t)))
!(match &self (h $x) (== $x $x))
!(match &self (h (pair $a $b)) (== $a $b))
!(match &self (h $x) (match &self (h $x) same))
!(match &self (h $x) (let $y (dag 40 leaf) (== $x $y)))
!(match &self (h (pair (pair $a $b) $c)) (== $a $b))
