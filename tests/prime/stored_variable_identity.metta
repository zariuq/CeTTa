; A term built at run time can hold two distinct variables with one
; spelling: the same equation variable from two calls.  Stored with add-atom
; and read back with match, they stay two variables; a variable that occurs
; twice stays one variable.
(= (fresh-x) $x)
(= (two) (let $a (fresh-x) (let $b (fresh-x) (pair $a $b))))
(= (one-twice) (let $a (fresh-x) (pair $a $a)))

!(let $p (two) (add-atom &self (stored $p)))
!(collapse (match &self (stored (pair 1 2)) matched))
!(match &self (stored $q) (unify $q (pair 1 2) two-variables one-variable))
!(match &self (stored (pair $l $r)) (if (== $l $r) one-variable two-variables))

!(let $p (one-twice) (add-atom &self (repeated $p)))
!(collapse (match &self (repeated (pair 1 2)) matched))
!(collapse (match &self (repeated (pair 1 1)) matched))

; Stored by the same let as the query that reads it back.
!(let $p (two)
   (let $u (add-atom &self (stored-here $p))
     (match &self (stored-here $q) (unify $q (pair 1 2) two-variables one-variable))))

; Controls: an atom as written keeps its variables as written.
!(add-atom &self (written (pair $m $n)))
!(collapse (match &self (written (pair 1 2)) matched))
!(add-atom &self (written-twice (pair $m $m)))
!(collapse (match &self (written-twice (pair 1 2)) matched))
