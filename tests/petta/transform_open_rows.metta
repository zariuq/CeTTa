!(import! &self (library lib_transform))
(= (show $s) (sort-atom (collapse (match $s $a $a))))
(= (pack $items) (cons-atom tuple $items))

; A stored row may hold a variable.  What a rule makes from the row keeps that
; variable where the row had it: the made row (st2 (1 $x) (k $x)) holds one
; variable twice, so a later rule that meets (k 5) finds 5 in the other place.
!(bind! &plain (new-space))
!(add-atom &plain (st (1 $x) (k $x)))
!(plain-count (collapse (transform &plain (take (st $acc $k)) (st2 $acc $k) &plain)))
!(plain-made (show &plain))
!(plain-bound (collapse (transform &plain (take (st2 ($one $a) (k 5))) (bound $a))))

; The same when the rule computes a value from the row in a let conjunct: the
; value's variable is the row's, not a copy of it.
!(bind! &computed (new-space))
!(add-atom &computed (st (1 $y) (k $y)))
!(computed-count (collapse (transform &computed (, (take (st $acc $k)) (let $v (pack $acc)))
                             (st2 $v $k) &computed)))
!(computed-made (show &computed))
!(computed-bound (collapse (transform &computed (take (st2 (tuple $one $a) (k 5))) (bound $a))))

; Negative: two rows that each hold a variable of their own are not linked by a
; rule that takes both; meeting 5 in one leaves the other's variable free.
!(bind! &apart (new-space))
!(add-atom &apart (left $p))
!(add-atom &apart (right $q))
!(apart-count (collapse (transform &apart (, (take (left $l)) (take (right $r)) (let $v (pack ($l))))
                          (both $v (k $r)) &apart)))
!(apart-made (show &apart))
!(apart-bound (collapse (transform &apart (take (both (tuple $a) (k 5))) (bound $a))))
