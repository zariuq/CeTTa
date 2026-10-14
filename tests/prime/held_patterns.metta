; A pattern that takes a held value apart reads the syntax it holds, in let,
; let* and case as in unify and Data equation heads.  The parts it binds stay
; held: they are returned as written and run only when evaluated.
(: hold (-> (Data Atom) (Data Atom)))
(= (hold $x) $x)
!(let (ex $l) (hold (ex a)) $l)
!(let* (($d (hold (ex a))) ((ex $l) $d)) $l)
!(let $d (hold (ex a)) (case $d (((ex $l) $l))))
!(let $d (hold (ex a)) (unify $d (ex $l) $l none))
!(let (ex $l) (hold (ex (+ 1 2))) $l)
!(let (ex $l) (hold (ex (+ 1 2))) (eval $l))
!(let $d (hold (ex (+ 1 2))) (case $d (((ex $l) $l))))
; A pattern that does not fit falls through; one variable binds the whole.
!(let $d (hold (ex a)) (case $d (((other $l) $l) ($whole matched-whole))))
; Controls: a value that is not held is evaluated as usual.
!(let (ex $l) (ex (+ 1 2)) $l)
!(let $d (ex (+ 1 2)) (case $d (((ex $l) $l))))

; A held value used as a match pattern is the syntax it holds, whether the
; pattern comes from a Data parameter or from a let.
(fact a)
(fact b)
(: find (-> (Data Atom) $r))
(= (find $p) (collapse (match &self $p found){$p}))
(: find-part (-> (Data Atom) $r))
(= (find-part $p) (match &self $p $p){$p})
!(find (fact a))
!(find (fact c))
!(find-part (fact $x))
!(let $p (hold (fact $y)) (match &self $p $y){$p})
