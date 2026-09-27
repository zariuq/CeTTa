; A Data parameter holds its argument as written.  Comparing the held term
; with == reads it as syntax: nothing inside it runs, whether or not it
; contains variables, and the answer is about the term as written.
(= (exists $d $p) (println! ran-exists))
(: is-nil (-> (Data Atom) Bool))
(= (is-nil $t) (== $t ()))
!(is-nil (exists P (lam y (P y))))
!(is-nil (exists P (lam $y (P $y))))
!(is-nil ())

; The same under if, and between two held terms.
(: is-nil-if (-> (Data Atom) Bool))
(= (is-nil-if $t) (if (== $t ()) True False))
!(is-nil-if (exists P (lam $y (P $y))))
(: same (-> (Data Atom) (Data Atom) Bool))
(= (same $a $b) (== $a $b))
!(same (exists P (lam $y (P $y))) (exists P (lam $y (P $y))))
!(same (exists P (lam $y (P $y))) (exists Q (lam $y (Q $y))))

; The held term is returned as written, and runs only when evaluated.
(: hold (-> (Data Atom) (Data Atom)))
(= (hold $t) $t)
!(hold (exists P (lam $y (P $y))))
!(let $d (hold (exists P (lam $y (P $y)))) (eval $d))
